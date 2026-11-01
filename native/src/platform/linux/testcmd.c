// Scripted testing of the real app (FTL_TEST_CMD=/path/to/fifo): commands on a
// FIFO, in window pixels, one per line:
//   click x y | rclick x y | down x y | up x y | move x y | wheel x y steps |
//   key <android keycode> | keydown <code> | keyup <code> | text <reply for text prompts> |
//   back | shot file.png | fullscreen | pause | resume | quit |
//   padattach | padbtn <SDL_GamepadButton> <0|1> | padaxis <SDL_GamepadAxis> <-32768..32767> |
//   audio (logs the output's peak level since the last 'audio')
#define _GNU_SOURCE
#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "../../fw/fw.h"
#include "../../fw/present.h"
#include "../../ux/ux.h"
#include "linux_app.h"
#include "port_log.h"

static int g_fd = -1;
static char g_buf[4096];
static size_t g_len;
static char g_shot[512];
static double g_click_up_at;  // a click's button-up, a few frames later
static float g_click_x, g_click_y;
static int g_click_up_action;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8] = {len >> 24, len >> 16, len >> 8, len, type[0], type[1], type[2], type[3]};
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = (uint32_t)crc32(0, (const Bytef *)type, 4);
    if (len) crc = (uint32_t)crc32(crc, data, len);
    uint8_t c[4] = {crc >> 24, crc >> 16, crc >> 8, crc};
    fwrite(c, 1, 4, f);
}
static void write_png(const char *path, const uint8_t *rgba, int w, int h) {
    size_t raw_len = (size_t)(w * 3 + 1) * h;
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (w * 3 + 1);
        row[0] = 0;
        const uint8_t *src = rgba + (size_t)(h - 1 - y) * w * 4;
        for (int x = 0; x < w; x++) memcpy(row + 1 + x * 3, src + x * 4, 3);
    }
    uLongf clen = compressBound(raw_len);
    uint8_t *comp = malloc(clen);
    compress2(comp, &clen, raw, raw_len, 6);
    FILE *f = fopen(path, "wbe");
    if (f) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        uint8_t ihdr[13] = {w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, 2, 0, 0, 0};
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", comp, (uint32_t)clen);
        png_chunk(f, "IEND", NULL, 0);
        fclose(f);
        LOGI("test: screenshot -> %s", path);
    }
    free(raw);
    free(comp);
}

// presenting thread, just before the swap: the window's back buffer
static void post(int sw, int sh) {
    if (!g_shot[0]) return;
    uint8_t *px = malloc((size_t)sw * sh * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, px);
    write_png(g_shot, px, sw, sh);
    free(px);
    g_shot[0] = 0;
}

void testcmd_init(void) {
    const char *fifo = getenv("FTL_TEST_CMD");
    if (!fifo) return;
    mkfifo(fifo, 0600);
    g_fd = open(fifo, O_RDWR | O_NONBLOCK | O_CLOEXEC);  // RDWR: no EOF when writers come and go
    present_set_post_hook(post);
    LOGI("test: reading commands from %s", fifo);
}

static char g_text_reply[256];
const char *testcmd_text(void) { return g_fd >= 0 && g_text_reply[0] ? g_text_reply : NULL; }

static SDL_Joystick *g_vpad;

static void handle(char *line) {
    float x, y, s;
    int code, n2;
    char a[512];
    if (sscanf(line, "click %f %f", &x, &y) == 2 || sscanf(line, "rclick %f %f", &x, &y) == 2) {
        bool right = line[0] == 'r';
        ux_handle_mouse(0, x, y, 0);
        ux_handle_mouse(right ? 3 : 1, x, y, 0);
        g_click_up_at = now_s() + 0.12;
        g_click_up_action = right ? 4 : 2;
        g_click_x = x;
        g_click_y = y;
    } else if (sscanf(line, "down %f %f", &x, &y) == 2) {
        ux_handle_mouse(0, x, y, 0);
        ux_handle_mouse(1, x, y, 0);
    } else if (sscanf(line, "up %f %f", &x, &y) == 2) {
        ux_handle_mouse(2, x, y, 0);
    } else if (sscanf(line, "move %f %f", &x, &y) == 2) {
        ux_handle_mouse(0, x, y, 0);
    } else if (sscanf(line, "wheel %f %f %f", &x, &y, &s) == 3) {
        for (int i = 0; i < (int)(s < 0 ? -s : s); i++) ux_handle_mouse(7, x, y, s < 0 ? -1 : 1);
    } else if (sscanf(line, "keydown %d", &code) == 1) {
        ux_handle_key(code, true);
    } else if (sscanf(line, "keyup %d", &code) == 1) {
        ux_handle_key(code, false);
    } else if (sscanf(line, "key %d", &code) == 1) {
        ux_handle_key(code, true);
        ux_handle_key(code, false);
    } else if (!strncmp(line, "text ", 5)) {
        snprintf(g_text_reply, sizeof g_text_reply, "%s", line + 5);
    } else if (!strcmp(line, "back")) {
        ux_handle_back();
    } else if (sscanf(line, "shot %511s", a) == 1) {
        snprintf(g_shot, sizeof g_shot, "%s", a);
    } else if (!strcmp(line, "fullscreen")) {
        sdl_toggle_fullscreen();
    } else if (!strcmp(line, "pause")) {
        fw_app_will_resign_active();
    } else if (!strcmp(line, "resume")) {
        fw_app_did_become_active();
    } else if (!strcmp(line, "padattach")) {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.name = "Test Pad";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        g_vpad = id ? SDL_OpenJoystick(id) : NULL;
        LOGI("test: virtual pad %u %s", (unsigned)id, g_vpad ? "attached" : SDL_GetError());
    } else if (sscanf(line, "padbtn %d %d", &code, &n2) == 2) {
        if (g_vpad) SDL_SetJoystickVirtualButton(g_vpad, code, n2 != 0);
    } else if (sscanf(line, "padaxis %d %d", &code, &n2) == 2) {
        if (g_vpad) SDL_SetJoystickVirtualAxis(g_vpad, code, (Sint16)n2);
    } else if (!strcmp(line, "audio")) {
        LOGI("test: audio peak %d", sdl_audio_peak());
    } else if (!strcmp(line, "quit")) {
        ux_quit();
    } else if (*line) {
        LOGW("test: unknown command '%s'", line);
    }
}

void testcmd_poll(void) {
    if (g_fd < 0) return;
    if (g_click_up_at > 0 && now_s() >= g_click_up_at) {
        g_click_up_at = 0;
        ux_handle_mouse(g_click_up_action, g_click_x, g_click_y, 0);
    }
    ssize_t n;
    while ((n = read(g_fd, g_buf + g_len, sizeof g_buf - 1 - g_len)) > 0) g_len += (size_t)n;
    g_buf[g_len] = 0;
    char *nl;
    while ((nl = strchr(g_buf, '\n'))) {
        *nl = 0;
        LOGI("test: %s", g_buf);
        handle(g_buf);
        size_t used = (size_t)(nl + 1 - g_buf);
        memmove(g_buf, nl + 1, g_len - used + 1);
        g_len -= used;
    }
}
