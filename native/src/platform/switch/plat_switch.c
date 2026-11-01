// Nintendo Switch platform layer (libnx, no SDL): EGL/GLES on the default
// window through mesa (nouveau), touchscreen and controllers through hid,
// sound through audout, the software keyboard for text, and the applet
// lifecycle (HOME menu, sleep, closing from the HOME menu).
//
// As on Linux the game runs on the main thread and its run loop calls
// plat_pump_events, which also lets libnx process applet messages.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../../fw/fw.h"
#include "../../fw/present.h"
#include "../../port_main.h"
#include "../../ux/ux.h"
#include "../platform.h"
#include "../linux/linux_app.h"
#include "port_log.h"
#include "switch_app.h"

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLSurface g_surf = EGL_NO_SURFACE;
static int g_sw = 1280, g_sh = 720;
static bool g_docked;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------------------
// GL
void *plat_gl_create_context(int es_major, void *share) {
    EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, es_major, EGL_NONE};
    EGLContext c = eglCreateContext(g_dpy, g_cfg, share ? (EGLContext)share : EGL_NO_CONTEXT, attrs);
    if (c == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext(ES%d) failed: %#x", es_major, eglGetError());
        return NULL;
    }
    return c;
}

// The window surface goes to the first thread that binds a context (the
// game's render thread), as on Linux; contexts on other threads get none.
static pthread_t g_surf_owner;
static bool g_surf_owned;
static pthread_mutex_t g_surf_lock = PTHREAD_MUTEX_INITIALIZER;

bool plat_gl_make_current(void *ctx) {
    pthread_mutex_lock(&g_surf_lock);
    bool mine = g_surf_owned && pthread_equal(g_surf_owner, pthread_self());
    if (!ctx) {
        if (mine) g_surf_owned = false;
        pthread_mutex_unlock(&g_surf_lock);
        return eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (!g_surf_owned) {
        g_surf_owned = mine = true;
        g_surf_owner = pthread_self();
    }
    pthread_mutex_unlock(&g_surf_lock);
    EGLSurface s = mine ? g_surf : EGL_NO_SURFACE;
    if (!eglMakeCurrent(g_dpy, s, s, (EGLContext)ctx)) {
        LOGE("eglMakeCurrent failed: %#x", eglGetError());
        return false;
    }
    if (s != EGL_NO_SURFACE) {
        eglSwapInterval(g_dpy, 1);
        LOGI("GL: %s / %s / %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    }
    return true;
}

static volatile bool g_in_background;  // HOME menu, sleep: nothing is shown
bool plat_gl_has_surface(void) { return !g_in_background; }
void plat_gl_surface_size(int *w, int *h) {
    *w = g_sw;
    *h = g_sh;
}
void plat_gl_present(void) {
    if (!eglSwapBuffers(g_dpy, g_surf)) {
        static int n;
        if (n++ < 5) LOGW("eglSwapBuffers failed: %#x", eglGetError());
    }
}
unsigned plat_gl_window_fbo(void) { return 0; }
void *plat_gl_get_proc(const char *name) { return (void *)eglGetProcAddress(name); }

static int gl_init(void) {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, NULL, NULL)) {
        LOGE("EGL: no display (%#x)", eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOGE("EGL: no OpenGL ES (%#x)", eglGetError());
        return -1;
    }
    static const EGLint want[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                  EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
                                  EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, want, &g_cfg, 1, &n) || n == 0) {
        LOGE("EGL: no RGBA8 ES3 config (%#x)", eglGetError());
        return -1;
    }
    NWindow *win = nwindowGetDefault();
    nwindowSetDimensions(win, (u32)g_sw, (u32)g_sh);
    g_surf = eglCreateWindowSurface(g_dpy, g_cfg, (EGLNativeWindowType)win, NULL);
    if (g_surf == EGL_NO_SURFACE) {
        LOGE("EGL: no window surface (%#x)", eglGetError());
        return -1;
    }
    LOGI("EGL %s, %dx%d (%s)", eglQueryString(g_dpy, EGL_VERSION), g_sw, g_sh, g_docked ? "docked" : "handheld");
    return 0;
}

// ---------------------------------------------------------------------------
// Screenshots for bug reports (and emulators that don't show the picture): an
// empty file named "screenshot" in the app's folder makes the next frame go to
// screenshot.png there, and the file is removed.
static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8] = {len >> 24, len >> 16, len >> 8, len, type[0], type[1], type[2], type[3]};
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = (uint32_t)crc32(0, (const Bytef *)type, 4);
    if (len) crc = (uint32_t)crc32(crc, data, len);
    uint8_t c[4] = {crc >> 24, crc >> 16, crc >> 8, crc};
    fwrite(c, 1, 4, f);
}

static void save_png(GLuint fbo, int sw, int sh, const char *name) {
    char out[600];
    uint8_t *px = malloc((size_t)sw * sh * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, px);
    size_t raw_len = (size_t)(sw * 3 + 1) * sh;
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < sh; y++) {
        uint8_t *row = raw + (size_t)y * (sw * 3 + 1);
        row[0] = 0;
        const uint8_t *src = px + (size_t)(sh - 1 - y) * sw * 4;
        for (int x = 0; x < sw; x++) memcpy(row + 1 + x * 3, src + x * 4, 3);
    }
    uLongf clen = compressBound(raw_len);
    uint8_t *comp = malloc(clen);
    compress2(comp, &clen, raw, raw_len, 1);
    snprintf(out, sizeof out, "%s/%s", g_paths.data_dir, name);
    FILE *f = fopen(out, "wb");
    if (f) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        uint8_t ihdr[13] = {sw >> 24, sw >> 16, sw >> 8, sw, sh >> 24, sh >> 16, sh >> 8, sh, 8, 2, 0, 0, 0};
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", comp, (uint32_t)clen);
        png_chunk(f, "IEND", NULL, 0);
        fclose(f);
        LOGI("screenshot -> %s", out);
    }
    free(px);
    free(raw);
    free(comp);
}

static void shot_hook(int sw, int sh) {
    static int frames;
    if (++frames % 30) return;  // look for the request twice a second
    char req[600];
    snprintf(req, sizeof req, "%s/screenshot", g_paths.data_dir);
    struct stat st;
    if (stat(req, &st) != 0) return;
    unlink(req);
    GLint game_fbo = 0;  // still bound for reading: the game's own image, before scaling
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &game_fbo);
    glFinish();  // the frame drawn so far, not what the GPU hasn't done yet
    save_png(0, sw, sh, "screenshot.png");
    if (game_fbo) save_png((GLuint)game_fbo, sw, sh, "screenshot-game.png");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)game_fbo);
}

// ---------------------------------------------------------------------------
// audio: audout at its own rate (48 kHz, stereo), fed by the game's callback
#define AUDIO_FRAMES 1024
#define AUDIO_BUFS 3
static plat_audio_cb g_acb;
static void *g_aud;
static int g_achannels = 2;
static volatile bool g_apaused = true;
static AudioOutBuffer g_abuf[AUDIO_BUFS];

static void fill(AudioOutBuffer *b) {
    int16_t *out = b->buffer;
    if (g_apaused || !g_acb) {
        memset(out, 0, b->buffer_size);
    } else if (g_achannels == 2) {
        g_acb(g_aud, out, AUDIO_FRAMES);
    } else {  // mono from the game: both speakers
        int16_t mono[AUDIO_FRAMES];
        g_acb(g_aud, mono, AUDIO_FRAMES);
        for (int i = 0; i < AUDIO_FRAMES; i++) out[2 * i] = out[2 * i + 1] = mono[i];
    }
    b->data_size = AUDIO_FRAMES * 2 * sizeof(int16_t);
    armDCacheFlush(out, b->buffer_size);
}

static void *audio_thread(void *p) {
    (void)p;
    for (int i = 0; i < AUDIO_BUFS; i++) {
        fill(&g_abuf[i]);
        audoutAppendAudioOutBuffer(&g_abuf[i]);
    }
    for (;;) {
        AudioOutBuffer *done = NULL;
        u32 count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&done, &count, UINT64_MAX)) || !done) continue;
        fill(done);
        audoutAppendAudioOutBuffer(done);
    }
    return NULL;
}

bool plat_audio_open(int rate, int channels, plat_audio_cb cb, void *ud) {
    g_acb = cb;
    g_aud = ud;
    g_achannels = channels;
    static bool started;
    if (started) return true;
    started = true;
    if (R_FAILED(audoutInitialize()) || R_FAILED(audoutStartAudioOut())) {
        LOGE("audio: audout didn't start");
        return false;
    }
    if ((int)audoutGetSampleRate() != rate)
        LOGW("audio: the game asked for %d Hz, audout plays %u Hz", rate, audoutGetSampleRate());
    size_t size = (AUDIO_FRAMES * 2 * sizeof(int16_t) + 0xfff) & ~(size_t)0xfff;
    for (int i = 0; i < AUDIO_BUFS; i++) {
        g_abuf[i] = (AudioOutBuffer){.buffer = memalign(0x1000, size), .buffer_size = size};
        memset(g_abuf[i].buffer, 0, size);
    }
    pthread_t th;
    if (pthread_create(&th, NULL, audio_thread, NULL) != 0) return false;
    pthread_detach(th);
    LOGI("audio: %u Hz, %u ch", audoutGetSampleRate(), audoutGetChannelCount());
    return true;
}
void plat_audio_pause(bool p) { g_apaused = p; }
int plat_audio_native_rate(void) { return 48000; }

// ---------------------------------------------------------------------------
// text input (the software keyboard) and alerts, run from plat_pump_events
static struct {
    bool want_text;
    char prompt[128], initial[256];
} g_req;

void plat_request_text_input(const char *prompt, const char *initial) {
    g_req.want_text = true;
    snprintf(g_req.prompt, sizeof g_req.prompt, "%s", prompt && *prompt ? prompt : "Name");
    snprintf(g_req.initial, sizeof g_req.initial, "%s", initial ? initial : "");
}
void plat_cancel_text_input(void) { g_req.want_text = false; }

static void run_keyboard(void) {
    g_req.want_text = false;
    SwkbdConfig kbd;
    char out[256] = "";
    bool ok = false;
    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetHeaderText(&kbd, g_req.prompt);
        swkbdConfigSetInitialText(&kbd, g_req.initial);
        swkbdConfigSetStringLenMax(&kbd, 32);
        ok = R_SUCCEEDED(swkbdShow(&kbd, out, sizeof out));
        swkbdClose(&kbd);
    }
    fw_text_input_result(ok ? out : g_req.initial, ok);
}

// No system dialog with choices is open to homebrew: the message is shown with
// the error applet (OK only) and the first button counts as chosen.
void plat_show_alert(const char *title, const char *msg, const char **buttons, int n) {
    LOGI("alert: %s: %s", title ? title : "", msg ? msg : "");
    ErrorApplicationConfig c;
    char text[2048];
    snprintf(text, sizeof text, "%s\n\n%s", title ? title : "FTL", msg ? msg : "");
    if (R_SUCCEEDED(errorApplicationCreate(&c, text, NULL))) errorApplicationShow(&c);
    (void)buttons;
    (void)n;
    fw_alert_result(0);
}

void plat_open_url(const char *url) { LOGI("open %s: no browser for homebrew", url); }

// Leaving goes through the HOME menu (close the software): a Back on the main
// menu doesn't quit, so the game can't be left by accident.
void plat_request_quit(void) { LOGI("quit asked: close FTL from the HOME menu"); }

void plat_keep_screen_on(bool on) { appletSetMediaPlaybackState(on); }
void plat_haptic(int kind) { (void)kind; }

static HidVibrationDeviceHandle g_vib[2][2];
static bool g_vib_ok, g_rumble = true;
static u64 g_rumble_until;  // armGetSystemTick() when it stops; 0: not rumbling
static void rumble_send(float a) {
    HidVibrationValue v = {.amp_low = a, .freq_low = 160.0f, .amp_high = a, .freq_high = 320.0f};
    HidVibrationValue vv[2] = {v, v};
    hidSendVibrationValues(g_vib[0], vv, 2);
    hidSendVibrationValues(g_vib[1], vv, 2);
}
// The Joy-Cons keep vibrating until told otherwise: stopped here after ms (every frame)
void plat_pad_rumble(float strength, int ms) {
    if (!g_vib_ok || !g_rumble) return;
    rumble_send(fminf(fmaxf(strength, 0), 1));
    g_rumble_until = armGetSystemTick() + armNsToTicks((u64)(ms > 0 ? ms : 1) * 1000000);
}
static void rumble_tick(bool stop_now) {
    if (!g_rumble_until || (!stop_now && armGetSystemTick() < g_rumble_until)) return;
    g_rumble_until = 0;
    rumble_send(0);
}

void port_report_fatal(const char *msg) {
    LOGE("fatal: %s", msg);
    switch_show_error(msg);
}
void port_on_game_exit(int code) {
    LOGI("game exited (%d)", code);
    port_log_flush();
    audoutStopAudioOut();
    appletUnlockExit();
    exit(code);
}

// ---------------------------------------------------------------------------
// controllers: hid -> the port's key codes (pad.c's K_*), which ux/ and pad.c speak. Nintendo's
// A (right) confirms and B (bottom) goes back, as in Switch games.
static PadState g_pad;
static u64 g_held;
static bool g_pad_connected;
static const struct { u64 bit; int key; } BUTTONS[] = {
    {HidNpadButton_A, 96},     {HidNpadButton_B, 97},      {HidNpadButton_X, 99},      {HidNpadButton_Y, 100},
    {HidNpadButton_L, 102},    {HidNpadButton_R, 103},     {HidNpadButton_ZL, 104},    {HidNpadButton_ZR, 105},
    {HidNpadButton_StickL, 106}, {HidNpadButton_StickR, 107}, {HidNpadButton_Plus, 108}, {HidNpadButton_Minus, 109},
    {HidNpadButton_Up, 19},    {HidNpadButton_Down, 20},   {HidNpadButton_Left, 21},   {HidNpadButton_Right, 22},
};

static void poll_pad(void) {
    padUpdate(&g_pad);
    bool conn = padIsConnected(&g_pad);
    if (conn != g_pad_connected) {
        g_pad_connected = conn;
        ux_pad_connected(conn, conn ? (padIsHandheld(&g_pad) ? "Joy-Con (handheld)" : "Controller") : NULL);
    }
    if (!conn) return;
    u64 now = padGetButtons(&g_pad);
    u64 changed = now ^ g_held;
    for (size_t i = 0; i < sizeof BUTTONS / sizeof *BUTTONS; i++)
        if (changed & BUTTONS[i].bit) ux_pad_button(BUTTONS[i].key, (now & BUTTONS[i].bit) != 0);
    g_held = now;
    HidAnalogStickState l = padGetStickPos(&g_pad, 0), r = padGetStickPos(&g_pad, 1);
    float axes[8] = {l.x / 32767.0f, -l.y / 32767.0f, r.x / 32767.0f, -r.y / 32767.0f,
                     (now & HidNpadButton_ZL) ? 1.0f : 0.0f, (now & HidNpadButton_ZR) ? 1.0f : 0.0f, 0, 0};
    ux_pad_axes(axes);
}

// touchscreen (handheld): hid reports up to 16 fingers in 1280x720
static struct {
    u32 id;
    float x, y;
    bool on;
} g_fingers[16];

static void poll_touch(void) {
    HidTouchScreenState st = {0};
    if (!hidGetTouchScreenStates(&st, 1)) return;
    bool seen[16] = {false};
    float sx = g_sw / 1280.0f, sy = g_sh / 720.0f;
    double t = now_s();
    for (int i = 0; i < st.count && i < 16; i++) {
        const HidTouchState *f = &st.touches[i];
        int slot = -1, free_slot = -1;
        for (int k = 0; k < 16; k++) {
            if (g_fingers[k].on && g_fingers[k].id == f->finger_id) slot = k;
            if (!g_fingers[k].on && free_slot < 0) free_slot = k;
        }
        float x = f->x * sx, y = f->y * sy;
        if (slot < 0) {
            if (free_slot < 0) continue;
            slot = free_slot;
            g_fingers[slot] = (typeof(g_fingers[0])){f->finger_id, x, y, true};
            ux_handle_touch(TOUCH_BEGAN, (long)f->finger_id, x, y, t);
        } else if (x != g_fingers[slot].x || y != g_fingers[slot].y) {
            g_fingers[slot].x = x;
            g_fingers[slot].y = y;
            ux_handle_touch(TOUCH_MOVED, (long)f->finger_id, x, y, t);
        }
        seen[slot] = true;
    }
    for (int k = 0; k < 16; k++)
        if (g_fingers[k].on && !seen[k]) {
            g_fingers[k].on = false;
            ux_handle_touch(TOUCH_ENDED, (long)g_fingers[k].id, g_fingers[k].x, g_fingers[k].y, t);
        }
}

// ---------------------------------------------------------------------------
// lifecycle: the game pauses (iOS "resign active") while the HOME menu or
// sleep has the screen
static bool g_active = true, g_quitting;
static AppletHookCookie g_hook;

static void set_active(bool want) {
    g_in_background = !want;
    if (want == g_active) return;
    g_active = want;
    LOGI("%s", want ? "back in the game: resuming" : "in the background: pausing");
    if (want) {
        fw_app_did_become_active();
        plat_audio_pause(false);
    } else {
        fw_app_will_resign_active();
        plat_audio_pause(true);
    }
}

static void applet_hook(AppletHookType type, void *p) {
    (void)p;
    if (type == AppletHookType_OnFocusState) set_active(appletGetFocusState() == AppletFocusState_InFocus);
    else if (type == AppletHookType_OnResume) set_active(true);
    else if (type == AppletHookType_OnExitRequest && !g_quitting) {
        g_quitting = true;
        LOGI("closed from the HOME menu: saving and quitting");
        ux_quit();
    }
}

void plat_pump_events(void) {
    if (!appletMainLoop() && !g_quitting) {
        g_quitting = true;
        LOGI("asked to exit: saving and quitting");
        ux_quit();
    }
    rumble_tick(!g_active);
    if (g_active) {
        poll_pad();
        poll_touch();
    }
    if (g_req.want_text) run_keyboard();
    ux_tick();
}
void plat_wake_main(void) {}
void plat_main_thread_started(void) {}

// ---------------------------------------------------------------------------
static const char *const PAD_ACTIONS[] = {"none", "tap", "right_click", "back", "pause", "zoom_in",
                                          "zoom_out", "reset_zoom", "magnifier", "turbo", "precision"};
static const struct { const char *name; int key; } PAD_NAMES[] = {
    {"a", 96}, {"b", 97}, {"x", 99}, {"y", 100}, {"l", 102}, {"r", 103}, {"zl", 104}, {"zr", 105},
    {"lstick", 106}, {"rstick", 107}, {"plus", 108}, {"minus", 109},
};

void switch_push_options(void) {
    // The Switch's screen shows the game big enough: the magnifier (a held finger) is off
    // unless settings.ini has magnifier = true ("auto", as older files have, is off too),
    // and all of zooming (pinch, the map and upgrade screens, events, the zoom buttons)
    // unless it has zoom = true. ZL still shows the magnifier while held.
    int mag = setting_tristate("magnifier"), pinch = setting_tristate("pinch_zoom"), az = setting_tristate("auto_zoom");
    bool t = !g_docked, zoom = setting_bool("zoom", false);
    ux_set_option("magnifier", mag > 0 ? 1 : 2);
    ux_set_option("pinch_zoom", zoom && (pinch < 0 ? true : pinch) ? 1 : 0);
    ux_set_option("window_zoom", zoom && (az < 0 ? t : az) ? 1 : 2);
    ux_set_option("two_finger_pause", setting_bool("two_finger_pause", true));
    ux_set_option("minimap", zoom && setting_bool("minimap", true));
    ux_set_option("haptics", 0);
    ux_set_option("event_text", (float)setting_int("event_text", 0));
    ux_set_option("event_zoom", zoom && setting_bool("event_zoom", false));
    ux_set_option("pad_magnetism", setting_bool("pad_magnetism", true));
    g_rumble = setting_bool("pad_rumble", true);
    ux_set_option("pad_rumble", g_rumble);
    bool custom = false;
    for (size_t i = 0; i < sizeof PAD_NAMES / sizeof *PAD_NAMES; i++) {
        char k[32];
        snprintf(k, sizeof k, "pad_%s", PAD_NAMES[i].name);
        if (setting_str(k, NULL)) custom = true;
    }
    if (!custom && zoom) return;  // the default buttons
    static const int defaults[][2] = {{96, 1}, {97, 3}, {108, 3}, {99, 4}, {102, 6}, {103, 5}, {104, 8}, {105, 2},
                                      {109, 7}, {107, 7}};
    int map[256] = {0};
    for (size_t i = 0; i < sizeof defaults / sizeof *defaults; i++) map[defaults[i][0]] = defaults[i][1];
    for (size_t i = 0; i < sizeof PAD_NAMES / sizeof *PAD_NAMES; i++) {
        char k[32];
        snprintf(k, sizeof k, "pad_%s", PAD_NAMES[i].name);
        const char *v = setting_str(k, NULL);
        if (!v) continue;
        size_t a;
        for (a = 0; a < sizeof PAD_ACTIONS / sizeof *PAD_ACTIONS; a++)
            if (!strcmp(v, PAD_ACTIONS[a])) break;
        if (a == sizeof PAD_ACTIONS / sizeof *PAD_ACTIONS) {
            LOGW("settings: unknown action '%s' for %s", v, k);
            continue;
        }
        map[PAD_NAMES[i].key] = (int)a;
    }
    if (!zoom)  // the zoom buttons do nothing
        for (int k = 0; k < 256; k++)
            if (map[k] == 5 || map[k] == 6 || map[k] == 7) map[k] = 0;
    ux_set_option("pad_map_reset", 1);
    for (int k = 0; k < 256; k++)
        if (map[k]) {
            char key[32];
            snprintf(key, sizeof key, "pad_btn_%d", k);
            ux_set_option(key, (float)map[k]);
        }
}

int switch_app_init(void) {
    // Docked: draw at 1080p for the TV; handheld: the screen's 720p. Chosen at
    // start (the game's canvas is fixed then); the system scales if that changes.
    g_docked = appletGetOperationMode() == AppletOperationMode_Console;
    if (g_docked && setting_bool("docked_1080p", true)) {
        g_sw = 1920;
        g_sh = 1080;
    }
    if (gl_init() != 0) return -1;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    hidInitializeTouchScreen();
    if (R_SUCCEEDED(hidInitializeVibrationDevices(g_vib[0], 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld)) &&
        R_SUCCEEDED(hidInitializeVibrationDevices(g_vib[1], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual)))
        g_vib_ok = true;

    present_set_post_hook(shot_hook);
    appletHook(&g_hook, applet_hook, NULL);
    // Closing from the HOME menu then asks first (OnExitRequest above) instead of
    // ending the process: the game saves its settings and profile when it quits.
    // The system waits up to 15 s; port_on_game_exit unlocks.
    appletLockExit();
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    plat_keep_screen_on(true);

    // dpi: the handheld screen is 6.2" at 720p; a TV is looked at from afar
    float layout[8] = {0, 0, 0, 0, g_docked ? 96.0f : 237.0f, (float)setting_int("render_height", 0), 0, 0};
    ux_configure(g_sw, g_sh, 60, layout);
    return 0;
}

// ---------------------------------------------------------------------------
// Crashes: libnx calls this for CPU exceptions (no signals on Horizon).
// The GPU driver service's working memory (libnx: 8 MB). Every texture and buffer
// takes some of it; with Multiverse's thousands it ran out (GL_OUT_OF_MEMORY with
// 1.5 GB free, then a crash in Mesa's nouveau_mm_allocate at "Generating world").
u32 __nx_nv_transfermem_size = 0x4000000;  // 64 MB

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
u32 __nx_exception_ignoredebug = 1;

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    char why[96];
    snprintf(why, sizeof why, "exception %#x at far %#llx", ctx->error_desc, (unsigned long long)ctx->far.x);
    port_backtrace_from(ctx->pc.x, ctx->lr.x, ctx->fp.x, why);
    for (int i = 0; i < 28; i += 4)
        LOGE("  x%-2d %016llx %016llx %016llx %016llx", i, (unsigned long long)ctx->cpu_gprs[i].x,
             (unsigned long long)ctx->cpu_gprs[i + 1].x, (unsigned long long)ctx->cpu_gprs[i + 2].x,
             (unsigned long long)ctx->cpu_gprs[i + 3].x);
    port_log_mark_crash();
    svcExitProcess();
}
