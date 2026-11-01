// ftl.nro: FTL for iPad (the player's own copy) on a Switch running
// Atmosphère, as homebrew. Everything lives in sdmc:/switch/ftl/:
//   ftl.nro           this app
//   FTL.ipa           the player's .ipa: imported into game/ on the first start
//   game/             FTL, ftl.dat, Info.plist (ftl.dat may be one ftlman patched on PC)
//   saves/  logs/  cache/  settings.ini
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>
#include <sys/iosupport.h>
#include <unistd.h>

#include "../../port_main.h"
#include "../../ux/ux.h"
#include "../linux/linux_app.h"
#include "port_log.h"
#include "switch_app.h"

#ifndef FTL_SWITCH_VERSION
#define FTL_SWITCH_VERSION "dev"
#endif

// Before the game's window exists, messages go to libnx's text console: from the
// start, with each step, so a start that stops somewhere shows where. So that the
// GPU driver gets a window the console never touched, ftl.nro then has the Homebrew
// loader start it again with --quiet, which goes straight to the game (where the
// loader can't, as in emulators, it goes on from the console).
static bool g_console, g_console_used;

static void console_on(void) {
    if (g_console) return;
    consoleInit(NULL);
    g_console = g_console_used = true;
    printf("FTL: Faster Than Light for Switch (homebrew port) %s\n\n", FTL_SWITCH_VERSION);
}
// stdout/stderr once the console is closed: the kernel's debug output (shown by
// debuggers and emulators), not the console, which they'd still point at otherwise.
static ssize_t debug_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
    (void)r, (void)fd;
    svcOutputDebugString(ptr, len);
    return (ssize_t)len;
}
static const devoptab_t g_debug_out = {.name = "debug", .write_r = debug_write};

static void console_off(void) {
    if (!g_console) return;
    consoleExit(NULL);
    g_console = false;
    fflush(stdout);
    fflush(stderr);
    devoptab_list[STD_OUT] = &g_debug_out;
    devoptab_list[STD_ERR] = &g_debug_out;
}

// Shows msg and waits for + (console), or uses the error applet (game running).
void switch_show_error(const char *msg) {
    LOGE("%s", msg);
    if (!g_console) {
        ErrorApplicationConfig c;
        if (R_SUCCEEDED(errorApplicationCreate(&c, msg, NULL))) errorApplicationShow(&c);
        return;
    }
    printf("\n%s\n\nPress + to exit.\n", msg);
    consoleUpdate(NULL);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
}

static void step(const char *what) {
    LOGI("start: %s", what);
    if (!g_console) return;
    printf("%s\n", what);
    consoleUpdate(NULL);
}

static _Noreturn void fail(const char *msg) {
    console_on();
    switch_show_error(msg);
    console_off();
    appletUnlockExit();  // taken once the game's window is up (plat_switch.c)
    exit(1);
}

// An .ipa (or .zip) in the app's folder, to import when there's no game yet.
static bool find_ipa(char *out, size_t n) {
    DIR *d = opendir(g_paths.data_dir);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcasecmp(e->d_name + l - 4, ".ipa")) {
            snprintf(out, n, "%s/%s", g_paths.data_dir, e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found;
}

static void import_from_sd(void) {
    char ipa[1024], err[1024];
    if (!find_ipa(ipa, sizeof ipa)) {
        char msg[1024];
        snprintf(msg, sizeof msg,
                 "The game isn't imported yet.\n\n"
                 "Copy your FTL for iPad .ipa (1.6.21, decrypted) to\n    %s/\n"
                 "and start FTL again: it's imported on the first start.",
                 g_paths.data_dir);
        fail(msg);
    }
    console_on();
    LOGI("importing %s", ipa);
    printf("Importing %s ...\n(this takes a minute; the .ipa can be deleted afterwards)\n\n", ipa);
    consoleUpdate(NULL);
    if (import_game(ipa, g_paths.game_dir, err, sizeof err) != 0) {
        char msg[1400];
        snprintf(msg, sizeof msg, "That file can't be used:\n%s", err);
        fail(msg);
    }
    char plist[700], ver[64] = "";
    snprintf(plist, sizeof plist, "%s/Info.plist", g_paths.game_dir);
    import_plist_version(plist, ver, sizeof ver);
    LOGI("imported FTL for iPad %s from %s", ver[0] ? ver : "?", ipa);
    printf("Imported FTL for iPad %s.\n", ver[0] ? ver : "");
    consoleUpdate(NULL);
}

// Start again without the console (see above); false where the loader can't.
static bool restart_quiet(const char *self) {
    if (!self || !self[0] || !envHasNextLoad()) return false;
    char args[1100];
    snprintf(args, sizeof args, "\"%s\" --quiet", self);
    if (R_FAILED(envSetNextLoad(self, args))) return false;
    LOGI("start: restarting without the console");
    return true;
}

int main(int argc, char **argv) {
    bool quiet = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--quiet")) quiet = true;
    if (!quiet) {
        console_on();
        consoleUpdate(NULL);
    }
    paths_init();
    step("Reading settings...");
    mkdirs(g_paths.data_dir);
    mkdirs(g_paths.config_dir);
    char settings[700];
    snprintf(settings, sizeof settings, "%s/settings.ini", g_paths.config_dir);
    settings_write_default(settings);
    settings_load(settings);
    port_verbose = setting_bool("verbose", false);
    mkdirs(g_paths.state_dir);
    port_log_open(g_paths.state_dir);
    printf("Log: %s\n", g_paths.state_dir);
    step("Checking memory...");
    char crash_marker[700];
    snprintf(crash_marker, sizeof crash_marker, "%s/crashed", g_paths.state_dir);
    bool crashed = access(crash_marker, F_OK) == 0;
    unlink(crash_marker);
    if (crashed) LOGW("the last session crashed (its log is in %s)", g_paths.state_dir);

    // The loader maps the game as code, which takes the app's process handle
    // (Atmosphère's homebrew loader passes it; execmem_switch.c falls back).
    if (envGetOwnProcessHandle() == INVALID_HANDLE)
        LOGW("no process handle from the loader: execmem makes one itself");
    // As an applet (the Album), homebrew gets about 440 MB; the game wants more
    // room than that leaves once the GPU's share is taken. A game's slot (hold R
    // while starting a game) has the full memory.
    AppletType at = appletGetAppletType();
    long free_mb, total_mb, game_mb;
    port_memory(&free_mb, &total_mb, &game_mb);
    const char *mode = at == AppletType_Application || at == AppletType_SystemApplication ? "application" : "applet";
    LOGI("ftl.nro %s, %s, %ld MB", FTL_SWITCH_VERSION, mode, total_mb);
    printf("Running as %s, %ld MB of memory\n", mode, total_mb);
    if (total_mb < 1000 && !setting_bool("allow_applet_mode", false))
        fail("FTL needs more memory than the Album gives homebrew.\n\n"
             "Start it with a game's memory instead: hold R while starting any game,\n"
             "then pick FTL in the Homebrew Menu.\n"
             "(To try anyway, set allow_applet_mode = true in settings.ini.)");

    step("Looking for the game...");
    if (!import_game_present(g_paths.game_dir)) import_from_sd();

    char bin[700], plist[700], ver[64] = "";
    snprintf(bin, sizeof bin, "%s/FTL", g_paths.game_dir);
    snprintf(plist, sizeof plist, "%s/Info.plist", g_paths.game_dir);
    import_plist_version(plist, ver, sizeof ver);
    if (!import_is_known_build(bin)) LOGW("not the 1.6.21 build: keys, mods' pictures and the pause gesture are off");
    LOGI("game: FTL for iPad %s", ver[0] ? ver : "?");

    mkdirs(g_paths.saves_dir);
    mkdirs(g_paths.cache_dir);
    if (chdir(g_paths.saves_dir) != 0) LOGW("chdir %s failed", g_paths.saves_dir);
    step("Checking mods...");
#ifdef FTL_SWITCH_FTLMAN
    // Mods in /switch/ftl/mods/ are applied here with ftlman when that set changed (mods_switch.c)
    int mods_switch_apply(void (*console_on)(void));
    mods_switch_apply(console_on);
#endif
    // A ftl.dat patched with ftlman works as on Linux: the image overlay is made here.
    step("Preparing mod images...");
    mods_prepare();
    if (mods_dat_is_pc())
        fail("game/ftl.dat is from the PC version of FTL, which doesn't run here.\n\n"
             "To patch mods on a PC, use the iPad game's ftl.dat: copy\n"
             "    game/ftl.dat.vanilla (or game/ftl.dat if there's none)\n"
             "to the PC, patch it there with ftlman, and copy the result back\n"
             "as game/ftl.dat. Or import the .ipa again.");
    bool mods_changed = mods_take_changed();
    // Hyperspace (linked in) when the applied mods include it, as on Linux
    const char *hs = NULL;
#ifdef FTL_SWITCH_HYPERSPACE
    if (mods_hyperspace_applied()) hs = "linked in";
#else
    if (mods_hyperspace_applied()) LOGW("ftl.dat has Hyperspace mods, but this ftl.nro is built without Hyperspace");
#endif

    step("Starting the game...");
    if (g_console_used && restart_quiet(argc > 0 ? argv[0] : NULL)) {
        console_off();
        port_log_flush();
        return 0;
    }
    console_off();
    if (switch_app_init() != 0) fail("The graphics (EGL / OpenGL ES) didn't start.");
    switch_push_options();
    ux_set_option("resume_run", !crashed && !mods_changed && setting_bool("resume_run", false));

    port_config cfg = {bin, g_paths.game_dir, g_paths.saves_dir, g_paths.cache_dir, "iPad8,1", port_verbose,
                       mods_active_index(), hs, g_paths.state_dir};
    int r = port_run_game(&cfg);
    LOGE("the game stopped (%d)", r);
    port_log_flush();
    appletUnlockExit();
    return r ? r : 1;
}
