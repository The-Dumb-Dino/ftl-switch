// Linux desktop platform layer (SDL3): the window with an EGL/GLES 3 surface,
// keyboard / mouse / touch / game controllers, audio, and the app lifecycle.
//
// The game runs on the process's main thread, as in the test harness: its run
// loop calls plat_pump_events, which polls SDL, so all window and input work
// stays on the main thread as SDL requires. SDL makes the window and an EGL
// context once; the game's own contexts are made with EGL directly on SDL's
// display and config, and bound to SDL's window surface.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../fw/fw.h"
#include "../../fw/present.h"
#include "../../port_main.h"
#include "../../ux/ux.h"
#include "../platform.h"
#include "linux_app.h"
#include "port_log.h"

extern char **environ;

static SDL_Window *g_win;
static SDL_GLContext g_sdl_ctx;  // SDL's own context: only used so SDL does the swaps
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLSurface g_surf = EGL_NO_SURFACE;
static int g_sw, g_sh;          // the window in pixels
static bool g_vsync = true;
static int g_refresh = 60;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

void sdl_show_error(const char *title, const char *msg) {
    fprintf(stderr, "%s: %s\n", title, msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, g_win);
}

void sdl_message_only(const char *title, const char *msg) {
    if (SDL_Init(SDL_INIT_VIDEO)) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, msg, NULL);
}

static bool have_program(const char *name);

// kdialog (on KDE) or zenity, run to the end: the chosen file.
bool sdl_pick_file(const char *title, const char *filter, char *out, size_t n) {
    char *argv[8];
    char t[300], f[300];
    if (have_program("kdialog") && getenv("KDE_FULL_SESSION")) {
        snprintf(f, sizeof f, "%s", filter);
        argv[0] = "kdialog", argv[1] = "--title", argv[2] = (char *)title, argv[3] = "--getopenfilename";
        argv[4] = getenv("HOME") ? getenv("HOME") : ".", argv[5] = f, argv[6] = NULL;
    } else if (have_program("zenity")) {
        snprintf(t, sizeof t, "--title=%s", title);
        // filter as kdialog has it, "*.a *.b|name"; zenity's is "name | *.a *.b"
        const char *bar = strchr(filter, '|');
        if (bar) snprintf(f, sizeof f, "--file-filter=%s | %.*s", bar + 1, (int)(bar - filter), filter);
        else snprintf(f, sizeof f, "--file-filter=%s", filter);
        argv[0] = "zenity", argv[1] = "--file-selection", argv[2] = t, argv[3] = f, argv[4] = NULL;
    } else {
        return false;
    }
    int fds[2];
    if (pipe(fds) != 0) return false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    pid_t pid;
    int r = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (r) {
        close(fds[0]);
        return false;
    }
    size_t got = 0;
    ssize_t k;
    while (got + 1 < n && (k = read(fds[0], out + got, n - 1 - got)) > 0) got += (size_t)k;
    close(fds[0]);
    out[got] = 0;
    out[strcspn(out, "\r\n")] = 0;
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 && out[0];
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

// The window surface can be current in one thread at a time: it goes to the
// thread that binds a context first (the game's render thread); contexts bound
// on other threads (loading) get none.
static pthread_t g_surf_owner;
static bool g_surf_owned;
static pthread_mutex_t g_surf_lock = PTHREAD_MUTEX_INITIALIZER;

bool plat_gl_make_current(void *ctx) {
    pthread_mutex_lock(&g_surf_lock);
    bool mine = g_surf_owned && pthread_equal(g_surf_owner, pthread_self());
    if (!ctx) {
        if (mine) {
            g_surf_owned = false;
            SDL_GL_MakeCurrent(g_win, NULL);
        }
        pthread_mutex_unlock(&g_surf_lock);
        return eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (!g_surf_owned) {
        g_surf_owned = mine = true;
        g_surf_owner = pthread_self();
    }
    pthread_mutex_unlock(&g_surf_lock);
    EGLSurface s = mine ? g_surf : EGL_NO_SURFACE;
    if (mine) {
        // Swaps go through SDL_GL_SwapWindow, which needs the window current on this
        // thread as far as SDL knows. On Wayland it then waits for the compositor
        // itself, at most 1/20 s, instead of EGL blocking for good while the window
        // can't be seen (locked screen, another desktop).
        SDL_GL_MakeCurrent(g_win, g_sdl_ctx);
        SDL_GL_SetSwapInterval(g_vsync ? 1 : 0);
    }
    if (!eglMakeCurrent(g_dpy, s, s, (EGLContext)ctx)) {
        LOGE("eglMakeCurrent failed: %#x", eglGetError());
        return false;
    }
    if (s != EGL_NO_SURFACE) {
        LOGI("GL: %s / %s / %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    }
    return true;
}

static bool g_hidden;  // minimized or covered: nothing to draw into
bool plat_gl_has_surface(void) { return !g_hidden; }
void plat_gl_surface_size(int *w, int *h) {
    *w = g_sw;
    *h = g_sh;
}
void plat_gl_present(void) {
    if (!SDL_GL_SwapWindow(g_win)) {
        static int n;
        if (n++ < 5) LOGW("swap failed: %s (EGL %#x)", SDL_GetError(), eglGetError());
    }
}
unsigned plat_gl_window_fbo(void) { return 0; }
void *plat_gl_get_proc(const char *name) { return (void *)eglGetProcAddress(name); }

// ---------------------------------------------------------------------------
// audio: the game's render callback feeds an SDL audio stream
static plat_audio_cb g_acb;
static void *g_aud;
static int g_achannels = 2;
static volatile bool g_apaused = true;
static SDL_AudioStream *g_astream;
static int g_apeak;
int sdl_audio_peak(void) { return __atomic_exchange_n(&g_apeak, 0, __ATOMIC_RELAXED); }

static void SDLCALL audio_cb(void *ud, SDL_AudioStream *stream, int additional, int total) {
    (void)ud;
    (void)total;
    int16_t buf[1024 * 2];
    int frame_bytes = 2 * g_achannels;
    while (additional > 0) {
        int frames = additional / frame_bytes;
        if (frames > 1024) frames = 1024;
        if (frames <= 0) frames = 1;
        if (g_apaused || !g_acb) memset(buf, 0, (size_t)frames * frame_bytes);
        else {
            g_acb(g_aud, buf, frames);
            int peak = __atomic_load_n(&g_apeak, __ATOMIC_RELAXED);
            for (int i = 0; i < frames * g_achannels; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
            __atomic_store_n(&g_apeak, peak, __ATOMIC_RELAXED);
        }
        SDL_PutAudioStreamData(stream, buf, frames * frame_bytes);
        additional -= frames * frame_bytes;
    }
}

// Opened on its own thread: a sound server that doesn't answer (seen in VMs)
// then leaves the game silent instead of stuck at startup.
static SDL_AudioSpec g_aspec;
static void *audio_open_thread(void *p) {
    (void)p;
    SDL_AudioStream *st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &g_aspec, audio_cb, NULL);
    if (!st) {
        LOGE("audio: %s", SDL_GetError());
        return NULL;
    }
    SDL_ResumeAudioStreamDevice(st);
    __atomic_store_n(&g_astream, st, __ATOMIC_RELEASE);
    LOGI("audio: %d Hz, %d ch on %s", g_aspec.freq, g_aspec.channels, SDL_GetCurrentAudioDriver());
    return NULL;
}

bool plat_audio_open(int rate, int channels, plat_audio_cb cb, void *ud) {
    g_acb = cb;
    g_aud = ud;
    g_achannels = channels;
    static bool started;
    if (started) return true;
    started = true;
    g_aspec = (SDL_AudioSpec){SDL_AUDIO_S16, channels, rate};
    pthread_t th;
    if (pthread_create(&th, NULL, audio_open_thread, NULL) != 0) return false;
    pthread_detach(th);
    return true;
}
void plat_audio_pause(bool p) { g_apaused = p; }
int plat_audio_native_rate(void) {
    SDL_AudioSpec spec;
    if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL) && spec.freq > 0) return spec.freq;
    return 48000;
}

// ---------------------------------------------------------------------------
// results from dialogs, delivered on the next pump (not inside the call that asked)
static pthread_mutex_t g_qlock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    bool text_ready, text_ok, alert_ready, quit;
    char text[256];
    int alert;
} g_res;

// Text input: zenity or kdialog when there is one, else typed into the window
// (shown in its title bar): Enter accepts, Escape cancels.
static pid_t g_dialog_pid;
static bool g_inline_text;
static char g_inline_buf[256], g_inline_prompt[128];
static char g_title[128] = "FTL: Faster Than Light";

typedef struct {
    char *argv[12];
    char store[8][300];
} dialog_args;

static bool have_program(const char *name) {
    const char *path = getenv("PATH");
    if (!path) return false;
    char *dup = strdup(path), *save, *dir;
    bool found = false;
    for (dir = strtok_r(dup, ":", &save); dir && !found; dir = strtok_r(NULL, ":", &save)) {
        char p[600];
        snprintf(p, sizeof p, "%s/%s", dir, name);
        found = access(p, X_OK) == 0;
    }
    free(dup);
    return found;
}

static void *dialog_thread(void *p) {
    dialog_args *a = p;
    int fds[2];
    if (pipe(fds) != 0) {
        free(a);
        return NULL;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    pid_t pid;
    int rc = posix_spawnp(&pid, a->argv[0], &fa, NULL, a->argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    char out[256] = "";
    bool ok = false;
    if (rc == 0) {
        pthread_mutex_lock(&g_qlock);
        g_dialog_pid = pid;
        pthread_mutex_unlock(&g_qlock);
        size_t n = 0;
        ssize_t r;
        while (n < sizeof out - 1 && (r = read(fds[0], out + n, sizeof out - 1 - n)) > 0) n += (size_t)r;
        out[n] = 0;
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
        int st;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    }
    close(fds[0]);
    pthread_mutex_lock(&g_qlock);
    g_dialog_pid = 0;
    snprintf(g_res.text, sizeof g_res.text, "%s", out);
    g_res.text_ok = ok;
    g_res.text_ready = true;
    pthread_mutex_unlock(&g_qlock);
    free(a);
    return NULL;
}

static void inline_text_title(void) {
    char t[512];
    snprintf(t, sizeof t, "%s %s_   (Enter: OK, Esc: cancel)", g_inline_prompt, g_inline_buf);
    SDL_SetWindowTitle(g_win, t);
}
static void inline_text_end(bool ok) {
    g_inline_text = false;
    SDL_StopTextInput(g_win);
    SDL_SetWindowTitle(g_win, g_title);
    pthread_mutex_lock(&g_qlock);
    snprintf(g_res.text, sizeof g_res.text, "%s", g_inline_buf);
    g_res.text_ok = ok;
    g_res.text_ready = true;
    pthread_mutex_unlock(&g_qlock);
}

void plat_request_text_input(const char *prompt, const char *initial) {
    if (testcmd_text()) {
        pthread_mutex_lock(&g_qlock);
        snprintf(g_res.text, sizeof g_res.text, "%s", testcmd_text());
        g_res.text_ok = g_res.text_ready = true;
        pthread_mutex_unlock(&g_qlock);
        return;
    }
    const char *p = prompt && *prompt ? prompt : "Name:";
    const char *dlg = setting_str("text_dialog", "auto");  // auto, zenity, kdialog, window
    bool zen = !strcmp(dlg, "zenity") || (!strcmp(dlg, "auto") && have_program("zenity"));
    bool kd = !zen && (!strcmp(dlg, "kdialog") || (!strcmp(dlg, "auto") && have_program("kdialog")));
    if ((zen || kd) && SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) zen = kd = false;  // would hide behind
    if (zen || kd) {
        dialog_args *a = calloc(1, sizeof *a);
        int n = 0;
        snprintf(a->store[0], 300, "%s", p);
        snprintf(a->store[1], 300, "%s", initial ? initial : "");
        if (zen) {
            a->argv[n++] = "zenity";
            a->argv[n++] = "--entry";
            a->argv[n++] = "--title=FTL";
            snprintf(a->store[2], 300, "--text=%s", a->store[0]);
            snprintf(a->store[3], 300, "--entry-text=%s", a->store[1]);
            a->argv[n++] = a->store[2];
            a->argv[n++] = a->store[3];
        } else {
            a->argv[n++] = "kdialog";
            a->argv[n++] = "--title";
            a->argv[n++] = "FTL";
            a->argv[n++] = "--inputbox";
            a->argv[n++] = a->store[0];
            a->argv[n++] = a->store[1];
        }
        a->argv[n] = NULL;
        pthread_t th;
        if (pthread_create(&th, NULL, dialog_thread, a) == 0) {
            pthread_detach(th);
            return;
        }
        free(a);
    }
    g_inline_text = true;
    snprintf(g_inline_prompt, sizeof g_inline_prompt, "%s", p);
    snprintf(g_inline_buf, sizeof g_inline_buf, "%s", initial ? initial : "");
    SDL_StartTextInput(g_win);
    inline_text_title();
}
void plat_cancel_text_input(void) {
    pthread_mutex_lock(&g_qlock);
    if (g_dialog_pid > 0) kill(g_dialog_pid, SIGTERM);
    pthread_mutex_unlock(&g_qlock);
    if (g_inline_text) {
        g_inline_text = false;
        SDL_StopTextInput(g_win);
        SDL_SetWindowTitle(g_win, g_title);
    }
}

void plat_show_alert(const char *title, const char *msg, const char **buttons, int n) {
    SDL_MessageBoxButtonData b[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        b[i] = (SDL_MessageBoxButtonData){0, i, buttons[i]};
        if (i == 0) b[i].flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i == n - 1) b[i].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }
    const char *ok = "OK";
    SDL_MessageBoxButtonData one = {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, ok};
    SDL_MessageBoxData d = {SDL_MESSAGEBOX_INFORMATION, g_win, title ? title : "FTL", msg ? msg : "", n ? n : 1,
                            n ? b : &one, NULL};
    int pressed = 0;
    if (!SDL_ShowMessageBox(&d, &pressed) || pressed < 0) pressed = n > 0 ? n - 1 : 0;
    pthread_mutex_lock(&g_qlock);
    g_res.alert = pressed;
    g_res.alert_ready = true;
    pthread_mutex_unlock(&g_qlock);
}

void plat_open_url(const char *url) {
    if (!SDL_OpenURL(url)) LOGW("open %s: %s", url, SDL_GetError());
}

void plat_request_quit(void) {
    SDL_MessageBoxButtonData b[2] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Quit"},
                                     {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"}};
    SDL_MessageBoxData d = {SDL_MESSAGEBOX_INFORMATION, g_win, "FTL", "Quit FTL?", 2, b, NULL};
    int pressed = 0;
    if (SDL_ShowMessageBox(&d, &pressed) && pressed == 1) {
        pthread_mutex_lock(&g_qlock);
        g_res.quit = true;
        pthread_mutex_unlock(&g_qlock);
    }
}

void plat_keep_screen_on(bool on) {
    if (on) SDL_DisableScreenSaver();
    else SDL_EnableScreenSaver();
}
void plat_haptic(int kind) { (void)kind; }

static SDL_Gamepad *g_pad;
static SDL_JoystickID g_pad_id;
static bool g_rumble = true;
void plat_pad_rumble(float strength, int ms) {
    if (!g_pad || !g_rumble) return;
    Uint16 v = (Uint16)(fminf(fmaxf(strength, 0), 1) * 65535);
    SDL_RumbleGamepad(g_pad, v, v, (Uint32)ms);
}

void port_report_fatal(const char *msg) {
    LOGE("fatal: %s", msg);
    sdl_show_error("FTL", msg);
}
void port_on_game_exit(int code) {
    LOGI("game exited (%d)", code);
    SDL_AudioStream *st = __atomic_load_n(&g_astream, __ATOMIC_ACQUIRE);
    if (st) SDL_DestroyAudioStream(st);
    SDL_Quit();
    _exit(code);
}

// ---------------------------------------------------------------------------
// input mapping: SDL -> Android key codes, which ux/ and pad.c speak
static int android_key(const SDL_KeyboardEvent *k) {
    SDL_Keycode c = k->key;
    if (c >= 'a' && c <= 'z') return 29 + (int)(c - 'a');
    if (c >= '0' && c <= '9') return 7 + (int)(c - '0');
    switch (c) {
    case '\'': return 75;
    case ',': return 55;
    case '-': return 69;
    case '.': return 56;
    case '/': return 76;
    case ';': return 74;
    case '=': return 70;
    case '[': return 71;
    case '\\': return 73;
    case ']': return 72;
    case '`': return 68;
    default: break;
    }
    SDL_Scancode s = k->scancode;
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return 131 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return 145 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
    case SDL_SCANCODE_KP_0: return 144;
    case SDL_SCANCODE_KP_ENTER: return 160;
    case SDL_SCANCODE_TAB: return 61;
    case SDL_SCANCODE_RETURN: return 66;
    case SDL_SCANCODE_SPACE: return 62;
    case SDL_SCANCODE_LSHIFT: return 59;
    case SDL_SCANCODE_RSHIFT: return 60;
    case SDL_SCANCODE_LCTRL: return 113;
    case SDL_SCANCODE_RCTRL: return 114;
    case SDL_SCANCODE_LALT: return 57;
    case SDL_SCANCODE_RALT: return 58;
    case SDL_SCANCODE_LGUI: return 117;
    case SDL_SCANCODE_RGUI: return 118;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 143;
    case SDL_SCANCODE_CAPSLOCK: return 115;
    case SDL_SCANCODE_SCROLLLOCK: return 116;
    case SDL_SCANCODE_BACKSPACE: return 67;
    case SDL_SCANCODE_INSERT: return 124;
    case SDL_SCANCODE_DELETE: return 112;
    case SDL_SCANCODE_HOME: return 122;
    case SDL_SCANCODE_END: return 123;
    case SDL_SCANCODE_PAGEUP: return 92;
    case SDL_SCANCODE_PAGEDOWN: return 93;
    case SDL_SCANCODE_UP: return 19;
    case SDL_SCANCODE_DOWN: return 20;
    case SDL_SCANCODE_LEFT: return 21;
    case SDL_SCANCODE_RIGHT: return 22;
    case SDL_SCANCODE_ESCAPE: return 111;
    case SDL_SCANCODE_PAUSE: return 121;
    case SDL_SCANCODE_MENU: return 82;
    default: return 0;
    }
}

static int android_pad_button(int b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return 96;           // A
    case SDL_GAMEPAD_BUTTON_EAST: return 97;            // B
    case SDL_GAMEPAD_BUTTON_WEST: return 99;            // X
    case SDL_GAMEPAD_BUTTON_NORTH: return 100;          // Y
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 102;  // L1
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 103; // R1
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 106;     // THUMBL
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 107;    // THUMBR
    case SDL_GAMEPAD_BUTTON_START: return 108;
    case SDL_GAMEPAD_BUTTON_BACK: return 109;           // SELECT
    case SDL_GAMEPAD_BUTTON_GUIDE: return 110;          // MODE
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 19;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 20;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 21;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 22;
    default: return 0;
    }
}

// settings.ini names for buttons and actions (pad.c's numbering)
static const struct { const char *name; int key; } PAD_NAMES[] = {
    {"a", 96}, {"b", 97}, {"x", 99}, {"y", 100}, {"lb", 102}, {"rb", 103}, {"lt", 104}, {"rt", 105},
    {"l3", 106}, {"r3", 107}, {"start", 108}, {"select", 109}, {"home", 110},
};
static const char *const PAD_ACTIONS[] = {"none", "tap", "right_click", "back", "pause", "zoom_in",
                                          "zoom_out", "reset_zoom", "magnifier", "turbo", "precision"};

// ---------------------------------------------------------------------------
// touch vs mouse: the touch helpers (magnifier, pinch zoom, zooming in on
// windows) follow what the player uses when set to "auto"
static bool g_touch_mode;

static void push_touch_helpers(void) {
    int mag = setting_tristate("magnifier"), pinch = setting_tristate("pinch_zoom"), az = setting_tristate("auto_zoom");
    bool t = g_touch_mode;
    ux_set_option("magnifier", (mag < 0 ? t : mag) ? 1 : 2);
    ux_set_option("pinch_zoom", (pinch < 0 ? t : pinch) ? 1 : 0);
    ux_set_option("window_zoom", (az < 0 ? t : az) ? 1 : 2);
}

void sdl_app_push_options(void) {
    push_touch_helpers();
    ux_set_option("two_finger_pause", setting_bool("two_finger_pause", true));
    ux_set_option("minimap", setting_bool("minimap", true));
    ux_set_option("haptics", 0);
    ux_set_option("event_text", (float)setting_int("event_text", 0));
    ux_set_option("event_zoom", setting_bool("event_zoom", false));
    ux_set_option("pad_magnetism", setting_bool("pad_magnetism", true));
    g_rumble = setting_bool("pad_rumble", true);
    ux_set_option("pad_rumble", g_rumble);
    // buttons: pad.c's defaults unless settings.ini maps any
    bool custom = false;
    for (size_t i = 0; i < sizeof PAD_NAMES / sizeof *PAD_NAMES; i++) {
        char k[32];
        snprintf(k, sizeof k, "pad_%s", PAD_NAMES[i].name);
        if (setting_str(k, NULL)) custom = true;
    }
    if (!custom) return;
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
    ux_set_option("pad_map_reset", 1);
    for (int k = 0; k < 256; k++)
        if (map[k]) {
            char key[32];
            snprintf(key, sizeof key, "pad_btn_%d", k);
            ux_set_option(key, (float)map[k]);
        }
}

// ---------------------------------------------------------------------------
// lifecycle: the game pauses (iOS "resign active") while the window is
// minimized or hidden, and, if set, while it doesn't have the focus
static bool g_focused = true, g_minimized, g_occluded, g_active = true, g_pause_on_focus = true;

static void update_active(void) {
    g_hidden = g_minimized || g_occluded;
    bool want = !g_hidden && (g_focused || !g_pause_on_focus);
    if (want == g_active) return;
    g_active = want;
    LOGI("window %s", want ? "active: resuming" : "inactive: pausing");
    if (want) {
        fw_app_did_become_active();
        plat_audio_pause(false);
    } else {
        fw_app_will_resign_active();
        plat_audio_pause(true);
    }
}

void sdl_toggle_fullscreen(void) {
    bool fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(g_win, !fs);
}

static void resized(void) {
    int w, h;
    SDL_GetWindowSizeInPixels(g_win, &w, &h);
    if (w == g_sw && h == g_sh) return;
    g_sw = w;
    g_sh = h;
    LOGI("window: %dx%d pixels", w, h);
    ux_surface_resized(w, h);
}

static float g_pad_axes[8];
static int g_touchpad_fingers;
static bool g_touchpad_on = true;
static double g_wheel_acc;
static bool g_quitting;

static void open_pad(SDL_JoystickID id) {
    SDL_Gamepad *p = SDL_OpenGamepad(id);
    if (!p) return;
    if (g_pad) SDL_CloseGamepad(g_pad);
    g_pad = p;
    g_pad_id = id;
    const char *name = SDL_GetGamepadName(p);
    LOGI("game controller: %s", name ? name : "?");
    ux_pad_connected(true, name);
}

static void handle_event(const SDL_Event *e) {
    float density = g_win ? SDL_GetWindowPixelDensity(g_win) : 1;
    switch (e->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (!g_quitting) {
            g_quitting = true;
            LOGI("window closed: saving and quitting");
            ux_quit();
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: g_focused = true; update_active(); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: g_focused = false; update_active(); break;
    case SDL_EVENT_WINDOW_MINIMIZED: g_minimized = true; update_active(); break;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED: g_minimized = false; update_active(); break;
    case SDL_EVENT_WINDOW_OCCLUDED: g_occluded = true; update_active(); break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_SHOWN: g_occluded = false; update_active(); break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED: resized(); break;
    case SDL_EVENT_LOW_MEMORY: fw_app_memory_warning(); break;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const SDL_KeyboardEvent *k = &e->key;
        if (g_inline_text) {
            if (!k->down) break;
            if (k->scancode == SDL_SCANCODE_RETURN || k->scancode == SDL_SCANCODE_KP_ENTER) inline_text_end(true);
            else if (k->scancode == SDL_SCANCODE_ESCAPE) inline_text_end(false);
            else if (k->scancode == SDL_SCANCODE_BACKSPACE) {
                size_t n = strlen(g_inline_buf);
                while (n && (g_inline_buf[n - 1] & 0xc0) == 0x80) n--;  // a whole UTF-8 character
                if (n) g_inline_buf[n - 1] = 0;
                inline_text_title();
            }
            break;
        }
        bool alt_enter = k->scancode == SDL_SCANCODE_RETURN && (k->mod & SDL_KMOD_ALT);
        if (k->scancode == SDL_SCANCODE_F11 || alt_enter) {
            if (k->down && !k->repeat) sdl_toggle_fullscreen();
            break;
        }
        if (k->repeat) break;
        int code = android_key(k);
        if (code) ux_handle_key(code, k->down);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (g_inline_text) {
            size_t n = strlen(g_inline_buf);
            snprintf(g_inline_buf + n, sizeof g_inline_buf - n, "%s", e->text.text);
            inline_text_title();
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (e->motion.which == SDL_TOUCH_MOUSEID) break;
        ux_handle_mouse(0, e->motion.x * density, e->motion.y * density, 0);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e->button.which == SDL_TOUCH_MOUSEID) break;
        if (g_touch_mode) {  // a mouse again: the touch helpers go back to their mouse settings
            g_touch_mode = false;
            push_touch_helpers();
        }
        bool d = e->button.down;
        int act = e->button.button == SDL_BUTTON_LEFT ? (d ? 1 : 2)
                : e->button.button == SDL_BUTTON_RIGHT ? (d ? 3 : 4)
                : e->button.button == SDL_BUTTON_MIDDLE ? (d ? 5 : 6) : -1;
        if (act > 0) ux_handle_mouse(act, e->button.x * density, e->button.y * density, 0);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (e->wheel.which == SDL_TOUCH_MOUSEID) break;
        // Touchpads send many small steps: zoom once per notch's worth.
        float y = e->wheel.y * (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        g_wheel_acc += y;
        while (fabs(g_wheel_acc) >= 1) {
            float step = g_wheel_acc > 0 ? 1.0f : -1.0f;
            g_wheel_acc -= step;
            ux_handle_mouse(7, e->wheel.mouse_x * density, e->wheel.mouse_y * density, step);
        }
        break;
    }

    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        if (SDL_GetTouchDeviceType(e->tfinger.touchID) != SDL_TOUCH_DEVICE_DIRECT) break;  // touchpads: mouse
        if (!g_touch_mode) {
            g_touch_mode = true;
            LOGI("touchscreen in use");
            push_touch_helpers();
        }
        int phase = e->type == SDL_EVENT_FINGER_DOWN ? TOUCH_BEGAN
                  : e->type == SDL_EVENT_FINGER_MOTION ? TOUCH_MOVED
                  : e->type == SDL_EVENT_FINGER_UP ? TOUCH_ENDED : TOUCH_CANCELLED;
        ux_handle_touch(phase, (long)(e->tfinger.fingerID & 0x7fffffff), e->tfinger.x * g_sw, e->tfinger.y * g_sh,
                        now_s());
        break;
    }

    case SDL_EVENT_GAMEPAD_ADDED:
        if (!g_pad) open_pad(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (g_pad && e->gdevice.which == g_pad_id) {
            SDL_CloseGamepad(g_pad);
            g_pad = NULL;
            int n = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&n);
            if (n > 0) open_pad(ids[0]);
            else ux_pad_connected(false, NULL);
            SDL_free(ids);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (e->gbutton.which != g_pad_id) open_pad(e->gbutton.which);  // the one used last
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD) {
            if (g_touchpad_on) {
                float x = 0, y = 0;
                SDL_GetGamepadTouchpadFinger(g_pad, 0, 0, NULL, &x, &y, NULL);
                ux_pad_trackpad(g_touchpad_fingers, x, y, e->gbutton.down);
            }
            break;
        }
        int code = android_pad_button(e->gbutton.button);
        if (code) ux_pad_button(code, e->gbutton.down);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        if (e->gaxis.which != g_pad_id) break;
        float v = e->gaxis.value / 32767.0f;
        if (v < -1) v = -1;
        int idx = -1;
        switch (e->gaxis.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: idx = 0; break;
        case SDL_GAMEPAD_AXIS_LEFTY: idx = 1; break;
        case SDL_GAMEPAD_AXIS_RIGHTX: idx = 2; break;
        case SDL_GAMEPAD_AXIS_RIGHTY: idx = 3; break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: idx = 4; break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: idx = 5; break;
        default: break;
        }
        if (idx < 0) break;
        g_pad_axes[idx] = v;
        ux_pad_axes(g_pad_axes);
        break;
    }
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: {
        if (!g_touchpad_on || e->gtouchpad.which != g_pad_id || e->gtouchpad.touchpad != 0) break;
        if (e->type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN) g_touchpad_fingers++;
        if (e->type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP && g_touchpad_fingers > 0) g_touchpad_fingers--;
        if (e->gtouchpad.finger != 0 && e->type == SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION) break;  // follow the first
        bool pressed = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
        ux_pad_trackpad(g_touchpad_fingers, e->gtouchpad.x, e->gtouchpad.y, pressed);
        break;
    }
    default: break;
    }
}

void plat_pump_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(&e);
    pthread_mutex_lock(&g_qlock);
    bool text = g_res.text_ready, text_ok = g_res.text_ok, alert = g_res.alert_ready, quit = g_res.quit;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", g_res.text);
    int button = g_res.alert;
    g_res.text_ready = g_res.alert_ready = g_res.quit = false;
    pthread_mutex_unlock(&g_qlock);
    if (text) fw_text_input_result(buf, text_ok);
    if (alert) fw_alert_result(button);
    if (quit && !g_quitting) {
        g_quitting = true;
        ux_quit();
    }
    testcmd_poll();
    ux_tick();
}
void plat_wake_main(void) {}
void plat_main_thread_started(void) {}

// ---------------------------------------------------------------------------
int sdl_app_init(void) {
    SDL_SetAppMetadata("FTL: Faster Than Light", NULL, "dev.ftlport.linux");
    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");  // touches stay touches
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    LOGI("SDL %d.%d.%d, video %s", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
         SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Default window: 80% of the screen with the screen's shape, so the game's
    // canvas (fixed at start) also fills the screen in fullscreen.
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(disp);
    SDL_Rect usable = {0, 0, 1280, 800};
    SDL_GetDisplayUsableBounds(disp, &usable);
    float cs = SDL_GetDisplayContentScale(disp);
    if (cs <= 0) cs = 1;
    double aspect = mode && mode->h > 0 ? (double)mode->w / mode->h : 16.0 / 10;
    if (mode && mode->refresh_rate > 0) g_refresh = (int)lroundf(mode->refresh_rate);
    int ww = setting_int("window_width", 0), wh = setting_int("window_height", 0);
    if (ww <= 0 || wh <= 0) {
        wh = (int)(usable.h * 0.8);
        ww = (int)lround(wh * aspect);
        if (ww > usable.w * 0.9) {
            ww = (int)(usable.w * 0.9);
            wh = (int)lround(ww / aspect);
        }
    }
    bool fs = setting_bool("fullscreen", false);
    g_vsync = setting_bool("vsync", true);
    g_pause_on_focus = setting_bool("pause_on_focus_loss", true);
    g_touchpad_on = setting_bool("pad_touchpad", true);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fs) flags |= SDL_WINDOW_FULLSCREEN;
    g_win = SDL_CreateWindow(g_title, ww, wh, flags);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowMinimumSize(g_win, 640, 400);
    g_sdl_ctx = SDL_GL_CreateContext(g_win);
    if (!g_sdl_ctx) {
        sdl_show_error("FTL", SDL_GetError());
        return -1;
    }
    g_dpy = SDL_EGL_GetCurrentDisplay();
    g_cfg = SDL_EGL_GetCurrentConfig();
    g_surf = SDL_EGL_GetWindowSurface(g_win);
    if (!g_dpy || !g_cfg || !g_surf) {
        sdl_show_error("FTL", "Couldn't set up OpenGL ES through EGL.");
        return -1;
    }
    // Let go of SDL's context: the game's render thread binds the window surface.
    SDL_GL_MakeCurrent(g_win, NULL);
    SDL_SyncWindow(g_win);  // let a fullscreen request land before the game picks its canvas
    SDL_ShowWindow(g_win);
    SDL_PumpEvents();
    SDL_SyncWindow(g_win);
    SDL_GetWindowSizeInPixels(g_win, &g_sw, &g_sh);
    LOGI("window: %dx%d pixels (%dx%d units, scale %.2f), %d Hz, %s", g_sw, g_sh, ww, wh,
         SDL_GetWindowPixelDensity(g_win), g_refresh, fs ? "fullscreen" : "windowed");
    plat_keep_screen_on(true);

    // The game's canvas follows the window's shape and picks its render size.
    float layout[8] = {0, 0, 0, 0, 96 * cs, (float)setting_int("render_height", 0), 2, 0};
    ux_configure(g_sw, g_sh, g_refresh, layout);
    testcmd_init();
    // Controllers already plugged in arrive as SDL_EVENT_GAMEPAD_ADDED once the game pumps events.
    return 0;
}
