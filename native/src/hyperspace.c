// Hyperspace (the mod loader PC mods like Multiverse build on), when the applied mods
// include it: started once the game is mapped, linked and protected, before any game
// code runs. It hooks the game in memory. Built from the Hyperspace in
// hyperspace/source.sh (hyperspace/build-switch.sh); the test harness loads a
// libHyperspace.so with dlopen instead.
// On the Switch it is linked into ftl.nro (no dynamic loader), and its code memory
// comes from the port: code there is never made writable, it's written through the
// game's second, writable view (execmem_switch.c).
#include "hyperspace.h"

#ifndef __SWITCH__
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <string.h>

#include "hyperspace_host.h"
#include "loader/macho.h"
#include "port_log.h"

int __ftl_find_macho_unwind(uintptr_t pc, uintptr_t *mh, uintptr_t *textlen, uintptr_t *cu, size_t *culen,
                            uintptr_t *eh, size_t *ehlen);

static hs_android_wants_right_click_fn g_wants_right_click;
static hs_android_right_button_fn g_right_button;

static void hs_log(int level, const char *msg) {
    if (level >= 2) LOGE("%s", msg);
    else if (level == 1) LOGW("%s", msg);
    else LOGI("%s", msg);
}

#ifdef __SWITCH__
static mo_image *g_hs_img;
static void *hs_alloc_code(size_t size) { return mo_tramp_alloc(g_hs_img, size); }
static int hs_write_code(void *dst, const void *src, size_t n) { return execmem_write_code(&g_hs_img->mem, dst, src, n); }
// Hyperspace's message boxes (mods use them on purpose too, as a fake error): the
// system's error pop-up, the game waiting until it's closed, as on PC
void switch_show_error(const char *msg);
static void hs_show_error(const char *title, const char *message) {
    char text[2048];
    if (title && title[0] && strcmp(title, "Error") != 0) snprintf(text, sizeof text, "%s\n\n%s", title, message);
    else snprintf(text, sizeof text, "%s", message);
    switch_show_error(text);
}
#endif

int hyperspace_start(const char *lib, const char *log_dir, const char *data_dir, mo_image *img, char *err,
                     size_t errsize) {
#ifdef __SWITCH__
    (void)lib;
    LOGI("hyperspace: starting (linked in)");
    g_hs_img = img;
    hs_android_init_fn init = hs_android_init;
    hs_android_wants_right_click_fn wants_rc = hs_android_wants_right_click;
    hs_android_right_button_fn right_button = hs_android_right_button;
#else
    LOGI("hyperspace: loading %s", lib);
    void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        snprintf(err, errsize, "Hyperspace couldn't be loaded: %s", dlerror());
        return -1;
    }
    hs_android_init_fn init = (hs_android_init_fn)dlsym(h, "hs_android_init");
    if (!init) {
        snprintf(err, errsize, "This libHyperspace.so isn't built for the iPad game.");
        return -1;
    }
    hs_android_wants_right_click_fn wants_rc = (hs_android_wants_right_click_fn)dlsym(h, "hs_android_wants_right_click");
    hs_android_right_button_fn right_button = (hs_android_right_button_fn)dlsym(h, "hs_android_right_button");
#endif
    hs_host_info info = {
        .version = HS_HOST_VERSION,
        .size = sizeof info,
        .mach_header = img->base,
        .slide = img->slide,
        .image_size = img->map_size,
        .log_dir = log_dir,
        .data_dir = data_dir,
        .log = hs_log,
        .find_macho_unwind = __ftl_find_macho_unwind,
#ifdef __SWITCH__
        .alloc_code = hs_alloc_code,
        .write_code = hs_write_code,
        .show_error = hs_show_error,
#endif
    };
    char why[1024] = "";
    if (init(&info, why, sizeof why) != 0) {
        snprintf(err, errsize, "Hyperspace couldn't start: %s", why[0] ? why : "unknown error (see zhl.log)");
        return -1;
    }
    g_wants_right_click = wants_rc;
    g_right_button = right_button;
    LOGI("hyperspace: started");
    return 0;
}

bool hyperspace_wants_right_click(void) {
    return g_wants_right_click && g_right_button && g_wants_right_click();
}

void hyperspace_right_button(int action, double x, double y) {
    if (g_right_button) g_right_button(action, (int)x, (int)y);
}
