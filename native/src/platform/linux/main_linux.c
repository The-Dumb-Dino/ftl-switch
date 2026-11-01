// ftl-linux: runs FTL for iPad (the player's own copy) on aarch64 Linux.
//   ftl-linux                    play
//   ftl-linux --import <path>    import the game from an .ipa, FTL.app or a folder holding it
//   ftl-linux --ftlman           the mod manager (ftlman) for this game
//   ftl-linux --make-portable <folder> [FTL.ipa]   a folder that runs from anywhere
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../port_main.h"
#include "../../ux/ux.h"
#include "linux_app.h"
#include "port_log.h"

#ifndef FTL_LINUX_VERSION
#define FTL_LINUX_VERSION "dev"
#endif

static void usage(FILE *f) {
    fprintf(f,
            "ftl-linux %s: FTL: Faster Than Light (the iPad version) on ARM Linux\n"
            "\n"
            "Usage:\n"
            "  ftl-linux                      play\n"
            "  ftl-linux --import <path>      import your copy of FTL for iPad: an .ipa, an FTL.app\n"
            "                                 folder, or a folder holding Payload/FTL.app\n"
            "  ftl-linux --ftlman             the mod manager (ftlman), set up for this game: add mods,\n"
            "                                 tick them, Apply, then start the game\n"
            "  ftl-linux --make-portable <folder> [FTL.ipa]\n"
            "                                 a folder with ftl-linux, ftlman and the game that runs from\n"
            "                                 anywhere (without the .ipa: the game imported here)\n"
            "  ftl-linux --paths              where the game, saves, settings and logs are\n"
            "  ftl-linux --info               the imported game's version\n"
            "Options:\n"
            "  --fullscreen / --windowed      override the setting for this start\n"
            "  --verbose                      more detail in the log (and on stderr)\n"
            "\n"
            "Settings: %s/settings.ini%s\n",
            FTL_LINUX_VERSION, g_paths.config_dir, g_paths.portable ? " (portable)" : "");
}

static void print_paths(void) {
    if (g_paths.portable) printf("portable  %s\n", g_paths.exe_dir);
    printf("game      %s\nsaves     %s\nmods      %s/ftlman/mods\nsettings  %s/settings.ini\nlogs      %s\n"
           "cache     %s\n",
           g_paths.game_dir, g_paths.saves_dir, g_paths.data_dir, g_paths.config_dir, g_paths.state_dir,
           g_paths.cache_dir);
}

static int do_import(const char *src) {
    char err[1024];
    printf("Importing FTL from %s\n", src);
    if (import_game(src, g_paths.game_dir, err, sizeof err) != 0) {
        fprintf(stderr, "Import failed: %s\n", err);
        return 1;
    }
    char bin[600], plist[600], ver[64] = "";
    snprintf(bin, sizeof bin, "%s/FTL", g_paths.game_dir);
    snprintf(plist, sizeof plist, "%s/Info.plist", g_paths.game_dir);
    import_plist_version(plist, ver, sizeof ver);
    printf("Imported FTL for iPad %s into %s\n", ver[0] ? ver : "(unknown version)", g_paths.game_dir);
    if (!import_is_known_build(bin))
        printf("Note: this isn't the 1.6.21 build. It runs, but keyboard shortcuts, mods' pictures\n"
               "and the pause gesture only know 1.6.21.\n");
    printf("Start the game with: ftl-linux\n");
    return 0;
}

// No game yet: ask for the .ipa (a file chooser), else say how to import it.
static int pick_and_import(void) {
    char ipa[4096], err[1024];
    printf("The game isn't imported yet: pick your FTL.ipa\n");
    if (sdl_pick_file("FTL: pick your FTL for iPad (.ipa)", "*.ipa *.IPA *.zip|FTL for iPad (.ipa)", ipa, sizeof ipa)) {
        printf("Importing FTL from %s\n", ipa);
        if (import_game(ipa, g_paths.game_dir, err, sizeof err) == 0) return 0;
        char msg[1400];
        snprintf(msg, sizeof msg, "That file can't be used:\n\n%s", err);
        fprintf(stderr, "Import failed: %s\n", err);
        sdl_message_only("FTL", msg);
        return 1;
    }
    char msg[1200];
    snprintf(msg, sizeof msg,
             "The game isn't imported yet.\n\nImport your copy of FTL for iPad (1.6.21, decrypted) with:\n\n"
             "    ftl-linux --import /path/to/FTL.ipa\n\nIt's copied to %s.",
             g_paths.game_dir);
    fprintf(stderr, "%s\n", msg);
    if (!isatty(2)) sdl_message_only("FTL", msg);  // started from a launcher
    return 1;
}

static int info(void) {
    char bin[600], plist[600], ver[64] = "";
    snprintf(bin, sizeof bin, "%s/FTL", g_paths.game_dir);
    snprintf(plist, sizeof plist, "%s/Info.plist", g_paths.game_dir);
    if (!import_game_present(g_paths.game_dir)) {
        printf("No game imported yet (ftl-linux --import <FTL.ipa>).\n");
        return 1;
    }
    import_plist_version(plist, ver, sizeof ver);
    printf("FTL for iPad %s, %s\n", ver[0] ? ver : "(version unknown)",
           import_is_known_build(bin) ? "the 1.6.21 build (all features)" : "another build (no game hooks)");
    return 0;
}

int main(int argc, char **argv) {
    paths_init();
    mkdirs(g_paths.config_dir);
    char settings[600];
    snprintf(settings, sizeof settings, "%s/settings.ini", g_paths.config_dir);
    settings_write_default(settings);
    settings_load(settings);

    int verbose = 0;
    const char *fs = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("ftl-linux %s\n", FTL_LINUX_VERSION);
            return 0;
        } else if (!strcmp(a, "--paths")) {
            print_paths();
            return 0;
        } else if (!strcmp(a, "--info")) {
            return info();
        } else if (!strcmp(a, "--import")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--import needs a path (an .ipa, FTL.app or a folder)\n");
                return 2;
            }
            return do_import(argv[i + 1]);
        } else if (!strcmp(a, "--ftlman")) {
            return ftlman_run();
        } else if (!strcmp(a, "--make-portable")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--make-portable needs a folder (and the .ipa: --make-portable <folder> <FTL.ipa>)\n");
                return 2;
            }
            return portable_create(argv[i + 1], i + 2 < argc ? argv[i + 2] : NULL);
        } else if (!strcmp(a, "--mods")) {
            fprintf(stderr, "Mods are installed with ftlman now: ftl-linux --ftlman\n");
            return 2;
        } else if (!strcmp(a, "--verbose") || !strcmp(a, "-v")) {
            verbose = 1;
        } else if (!strcmp(a, "--fullscreen")) {
            fs = "true";
        } else if (!strcmp(a, "--windowed")) {
            fs = "false";
        } else {
            fprintf(stderr, "Unknown option %s\n\n", a);
            usage(stderr);
            return 2;
        }
    }

    port_verbose = verbose;
    mkdirs(g_paths.state_dir);
    port_log_open(g_paths.state_dir);
    char crash_marker[600];
    snprintf(crash_marker, sizeof crash_marker, "%s/crashed", g_paths.state_dir);
    bool crashed = access(crash_marker, F_OK) == 0;
    unlink(crash_marker);
    if (crashed) LOGW("the last session crashed (its log is in %s)", g_paths.state_dir);

    if (!import_game_present(g_paths.game_dir) && pick_and_import() != 0) return 1;

    if (fs) settings_override("fullscreen", fs);  // for this start only
    char bin[600], plist[600], ver[64] = "";
    snprintf(bin, sizeof bin, "%s/FTL", g_paths.game_dir);
    snprintf(plist, sizeof plist, "%s/Info.plist", g_paths.game_dir);
    import_plist_version(plist, ver, sizeof ver);
    LOGI("ftl-linux %s; game: FTL for iPad %s (%s)", FTL_LINUX_VERSION, ver[0] ? ver : "?",
         import_is_known_build(bin) ? "1.6.21 build" : "other build");

    mkdirs(g_paths.saves_dir);
    mkdirs(g_paths.cache_dir);
    // The game's Info.plist-relative paths and its cwd-relative files land in the saves folder.
    if (chdir(g_paths.saves_dir) != 0) LOGW("chdir %s failed", g_paths.saves_dir);
    if (sdl_app_init() != 0) return 1;
    sdl_app_push_options();
    // After a crash, or once ftlman changed ftl.dat, start at the title screen:
    // the saved run may be what crashed, or may not load with the new mods.
    mods_prepare();
    bool mods_changed = mods_take_changed();
    ux_set_option("resume_run", !crashed && !mods_changed && setting_bool("resume_run", false));

    const char *hs = NULL;
    char hs_path[600];
    const char *hs_lib = setting_str("hyperspace_lib", NULL);
    if (hs_lib && *hs_lib && mods_hyperspace_applied()) {
        snprintf(hs_path, sizeof hs_path, "%s", hs_lib);
        hs = hs_path;
    } else if (mods_hyperspace_applied()) {
        LOGW("the applied mods need Hyperspace, but no hyperspace_lib is set in settings.ini");
        if (mods_changed)
            sdl_show_error("FTL", "The mods applied with ftlman need Hyperspace, which this port doesn't have.\n\n"
                                  "The game may not work right: in ftlman, untick those mods and Apply.");
    }
    port_install_crash_handler();
    port_config cfg = {bin, g_paths.game_dir, g_paths.saves_dir, g_paths.cache_dir, "iPad8,1", verbose,
                       mods_active_index(), hs, g_paths.state_dir};
    int r = port_run_game(&cfg);
    LOGE("the game stopped (%d)", r);
    return r ? r : 1;
}
