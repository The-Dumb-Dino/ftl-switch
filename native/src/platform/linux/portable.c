// ftl-linux --make-portable <folder> [FTL.ipa]: a folder that has everything
// and runs from wherever it's put (another disk, a USB stick, another user):
//   ftl-linux, portable (the marker), game/ (from the .ipa), ftlman/ftlman,
//   data/ (settings, saves, logs, cache; made on the first start),
//   mod-manager, FTL.desktop, README.txt
// Without an .ipa, the game already imported here is copied in.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux_app.h"

static int write_text(const char *dir, const char *name, const char *text, int mode) {
    char p[1100], tmp[1200];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "we");
    if (!f) return -1;
    int bad = fputs(text, f) < 0;
    if (fclose(f)) bad = 1;
    if (bad || chmod(tmp, mode) || rename(tmp, p)) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static bool same_file(const char *a, const char *b) {
    struct stat x, y;
    return stat(a, &x) == 0 && stat(b, &y) == 0 && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
}

// Copies from to dest (through dest.tmp), unless they're the same file.
static int put(const char *from, const char *dest, int mode) {
    if (same_file(from, dest)) return 0;
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", dest);
    if (copy_file(from, tmp, mode) || rename(tmp, dest)) {
        fprintf(stderr, "Couldn't copy %s to %s: %s\n", from, dest, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return 0;
}

static bool empty_dir(const char *p) {
    DIR *d = opendir(p);
    if (!d) return false;
    struct dirent *e;
    bool empty = true;
    while (empty && (e = readdir(d))) empty = !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..");
    closedir(d);
    return empty;
}

static const char MARKER[] =
    "This file makes ftl-linux portable: it keeps its settings, saves and logs in\n"
    "data/ next to it and runs the game in game/, instead of using ~/.config and\n"
    "~/.local/share. Delete it to use the installed paths again.\n";

static const char MOD_MANAGER[] =
    "#!/bin/sh\n"
    "# ftlman, the mod manager, set up for the game in this folder.\n"
    "exec \"$(dirname \"$(readlink -f \"$0\")\")/ftl-linux\" --ftlman \"$@\"\n";

// %k: the .desktop file's own path, so it works wherever the folder is.
static const char DESKTOP[] =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=FTL: Faster Than Light\n"
    "Comment=FTL for iPad (your own copy) on ARM Linux, portable\n"
    "Exec=sh -c 'exec \"$(dirname \"$1\")/ftl-linux\"' sh %k\n"
    "Icon=applications-games\n"
    "Terminal=false\n"
    "Categories=Game;StrategyGame;\n"
    "StartupWMClass=dev.ftlport.linux\n"
    "Actions=mods;\n"
    "\n"
    "[Desktop Action mods]\n"
    "Name=Mod Manager (ftlman)\n"
    "Exec=sh -c 'exec \"$(dirname \"$1\")/ftl-linux\" --ftlman' sh %k\n";

static const char README[] =
    "FTL: Faster Than Light (the iPad version) for ARM Linux, portable\n"
    "==================================================================\n"
    "\n"
    "Play:      ./ftl-linux       (or open FTL.desktop; F11 switches to fullscreen)\n"
    "Mods:      ./mod-manager     (ftlman: add mods, tick them, Apply; then play)\n"
    "Options:   ./ftl-linux --help\n"
    "\n"
    "  game/       the game, imported from your FTL.ipa. ftlman patches game/ftl.dat\n"
    "              and keeps the original as game/ftl.dat.vanilla.\n"
    "  ftlman/     ftlman, its settings and mods/ (put .ftl/.zip mods there)\n"
    "  data/       settings.ini, saves/, logs/, cache/\n"
    "  portable    keeps all of this in this folder (see the file)\n"
    "\n"
    "Moving the folder is fine; copy it whole. It needs a 64-bit ARM Linux\n"
    "(aarch64) with SDL3 and OpenGL ES (Mesa); any kernel page size.\n"
    "Mods that need Hyperspace (Multiverse and others) don't work: Hyperspace is\n"
    "made for the PC game. Turn off ftlman's Hyperspace installer if it asks.\n"
    "\n"
    "The game files are your own copy: don't share this folder with game/ in it.\n";

int portable_create(const char *folder, const char *ipa) {
    char dir[PATH_MAX];
    if (mkdirs(folder) || !realpath(folder, dir)) {
        fprintf(stderr, "Can't make the folder %s: %s\n", folder, strerror(errno));
        return 1;
    }
    char p[PATH_MAX + 64], q[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/portable", dir);
    bool update = access(p, F_OK) == 0;
    if (!update && !empty_dir(dir)) {
        fprintf(stderr, "%s isn't empty (and isn't a portable FTL folder): pick a new or empty folder\n", dir);
        return 1;
    }
    char self[PATH_MAX];
    ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    if (l <= 0) return 1;
    self[l] = 0;

    // the game first: the slow part, and the one that can fail
    snprintf(q, sizeof q, "%s/game", dir);
    char err[1024];
    if (ipa) {
        printf("Importing FTL from %s\n", ipa);
        if (import_game(ipa, q, err, sizeof err)) {
            fprintf(stderr, "Import failed: %s\n", err);
            return 1;
        }
    } else if (!update || !import_game_present(q)) {
        if (!import_game_present(g_paths.game_dir)) {
            fprintf(stderr, "No game to put in: give the .ipa (ftl-linux --make-portable <folder> <FTL.ipa>)\n");
            return 1;
        }
        // the game as imported, without mods (ftlman's backup is the original)
        printf("Copying the game from %s\n", g_paths.game_dir);
        char tmp[PATH_MAX + 64];
        snprintf(tmp, sizeof tmp, "%s/game.tmp", dir);
        mkdirs(tmp);
        const char *names[][2] = {{"FTL", "FTL"}, {"Info.plist", "Info.plist"}, {"ftl.dat", "ftl.dat"}};
        for (int i = 0; i < 3; i++) {
            char from[PATH_MAX + 64], to[PATH_MAX + 64];
            snprintf(from, sizeof from, "%s/%s", g_paths.game_dir, names[i][0]);
            if (i == 2) {
                snprintf(to, sizeof to, "%s/ftl.dat.vanilla", g_paths.game_dir);
                if (access(to, R_OK) == 0) snprintf(from, sizeof from, "%s", to);
            }
            snprintf(to, sizeof to, "%s/%s", tmp, names[i][1]);
            if (put(from, to, i == 0 ? 0755 : 0644)) return 1;
        }
        if (rename(tmp, q)) {
            fprintf(stderr, "Couldn't move the game into %s: %s\n", q, strerror(errno));
            return 1;
        }
    }

    snprintf(p, sizeof p, "%s/ftl-linux", dir);
    if (put(self, p, 0755)) return 1;
    char ftlman[PATH_MAX];
    snprintf(q, sizeof q, "%s/ftlman", dir);
    mkdirs(q);
    snprintf(p, sizeof p, "%s/ftlman/mods", dir);
    mkdirs(p);
    bool have_ftlman = find_ftlman(ftlman, sizeof ftlman);
    snprintf(p, sizeof p, "%s/ftlman/ftlman", dir);
    if (have_ftlman && put(ftlman, p, 0755)) return 1;
    snprintf(p, sizeof p, "%s/game", dir);
    ftlman_configure(q, p);
    snprintf(p, sizeof p, "%s/data", dir);
    mkdirs(p);
    if (write_text(dir, "portable", MARKER, 0644) || write_text(dir, "mod-manager", MOD_MANAGER, 0755) ||
        write_text(dir, "FTL.desktop", DESKTOP, 0755) || write_text(dir, "README.txt", README, 0644)) {
        fprintf(stderr, "Couldn't write into %s\n", dir);
        return 1;
    }
    printf("%s portable FTL in %s\n", update ? "Updated the" : "Made a", dir);
    if (!have_ftlman) printf("Note: ftlman wasn't found, so the folder has no mod manager (ftlman/build-linux.sh).\n");
    printf("Play: %s/ftl-linux   Mods: %s/mod-manager\n", dir, dir);
    return 0;
}
