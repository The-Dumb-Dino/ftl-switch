// Mods on the console: .ftl/.zip files (or mod folders) in
// /switch/ftl/mods/, applied with ftlman itself (its patch engine, built for the
// Switch and linked in: switch/rust/). The original ftl.dat stays as
// ftl.dat.vanilla, which every patch starts from. ftl.nro applies the mods that are
// on at startup whenever they changed; the mod list (switch_mods_menu, L on the start
// screen) switches them on and off and orders them. Then mods_prepare (mods_linux.c) sees the
// new ftl.dat, makes the image overlay and, with Hyperspace among them, the game
// starts with it.
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>
#include <unistd.h>

#include "../linux/linux_app.h"
#include "port_log.h"
#include "switch_app.h"
#include "switch_mods.h"

// memory: bytes ftlman may use to patch a copy of ftl.dat in memory (switch/rust/, ftlman/switch.patch)
int ftlman_patch(const char *data_dir, const char *const *mods, size_t n, size_t memory,
                 void (*log)(int level, const char *msg), char *err, size_t errlen);

// Kept free while ftlman patches in memory: its XML and Lua work, the mods' zip
// reading and the console
#define PATCH_RESERVE_MB 384

static bool is_mod(const char *dir, const char *name) {
    if (name[0] == '.') return false;
    size_t l = strlen(name);
    if (l > 4 && (!strcasecmp(name + l - 4, ".ftl") || !strcasecmp(name + l - 4, ".zip"))) return true;
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);  // an unpacked mod
}

static int by_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static bool is_hyperspace(const char *name) { return !strncasecmp(name, "Hyperspace", 10); }

static void mods_dir(char *out, size_t n) { snprintf(out, n, "%s/mods", g_paths.data_dir); }

int switch_mods_list(switch_mod *mods, int max) {
    char dir[600];
    mods_dir(dir, sizeof dir);
    mkdirs(dir);
    static char found[SWITCH_MAX_MODS][256];
    int nfound = 0, n = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && nfound < SWITCH_MAX_MODS)
        if (is_mod(dir, e->d_name)) snprintf(found[nfound++], 256, "%s", e->d_name);
    closedir(d);
    qsort(found, nfound, sizeof *found, by_name);
    bool used[SWITCH_MAX_MODS] = {false};
    char p[1024], line[300];
    snprintf(p, sizeof p, "%s/order.txt", dir);
    FILE *f = fopen(p, "r");
    if (f) {
        while (fgets(line, sizeof line, f) && n < max) {
            line[strcspn(line, "\r\n")] = 0;
            bool off = line[0] == '-';
            const char *name = off ? line + 1 : line;
            for (int i = 0; i < nfound; i++)
                if (!used[i] && !strcasecmp(found[i], name)) {
                    snprintf(mods[n].name, sizeof mods[n].name, "%s", found[i]);
                    mods[n++].on = !off;
                    used[i] = true;
                }
        }
        fclose(f);
    }
    for (int i = 0; i < nfound && n < max; i++)
        if (!used[i]) {  // new: on
            snprintf(mods[n].name, sizeof mods[n].name, "%s", found[i]);
            mods[n++].on = true;
        }
    // Hyperspace first (the mods that build on it expect its files before theirs)
    for (int i = 1; i < n; i++)
        if (is_hyperspace(mods[i].name)) {
            switch_mod h = mods[i];
            memmove(mods + 1, mods, (size_t)i * sizeof *mods);
            mods[0] = h;
        }
    return n;
}

int switch_mods_save(const switch_mod *mods, int n) {
    char dir[600], p[700], tmp[720];
    mods_dir(dir, sizeof dir);
    snprintf(p, sizeof p, "%s/order.txt", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    for (int i = 0; i < n; i++) fprintf(f, "%s%s\n", mods[i].on ? "" : "-", mods[i].name);
    if (fclose(f) != 0) return -1;
    return replace_file(tmp, p);
}

// What applying gives: the mods that are on (names, sizes, times), then a last line
// with ftl.dat's size. Kept in logs/mods.applied after each apply.
static void signature(const switch_mod *mods, int n, char *out, size_t len) {
    char dir[600];
    mods_dir(dir, sizeof dir);
    size_t o = 0;
    for (int i = 0; i < n && o < len; i++) {
        if (!mods[i].on) continue;
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", dir, mods[i].name);
        struct stat st = {0};
        stat(p, &st);
        o += (size_t)snprintf(out + o, len - o, "%s %lld %lld\n", mods[i].name, (long long)st.st_size,
                              (long long)st.st_mtime);
    }
    char dat[700];
    snprintf(dat, sizeof dat, "%s/ftl.dat", g_paths.game_dir);
    struct stat st = {0};
    if (o < len && stat(dat, &st) == 0) snprintf(out + o, len - o, "ftl.dat %lld\n", (long long)st.st_size);
}

static void stamp_path(char *out, size_t n) { snprintf(out, n, "%s/mods.applied", g_paths.state_dir); }

static char *read_text(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    static char buf[32768];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strdup(buf);
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static int count_on(const switch_mod *mods, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) k += mods[i].on;
    return k;
}

// A signature's ftl.dat line (the last one), or "" without one
static const char *dat_line(const char *sig) {
    if (!strncmp(sig, "ftl.dat ", 8)) return sig;
    const char *p = strstr(sig, "\nftl.dat ");
    return p ? p + 1 : sig + strlen(sig);
}

// Whether ftl.dat, replaced from outside, is the original game's again: the copy
// kept before patching is gone (renamed back to ftl.dat), or ftl.dat has its size
// (any mod changes the size).
static bool dat_is_original(void) {
    char van[700], dat[700];
    snprintf(van, sizeof van, "%s/ftl.dat.vanilla", g_paths.game_dir);
    snprintf(dat, sizeof dat, "%s/ftl.dat", g_paths.game_dir);
    struct stat v, d;
    if (stat(dat, &d) != 0) return false;
    return stat(van, &v) != 0 || v.st_size == d.st_size;
}

// Only mods/ decides: a different set of mods there is applied (or, with none left
// that were applied here, the original put back). An ftl.dat replaced from outside
// (big mods like Multiverse patched with ftlman on a PC, which is much faster) is
// used as it is, Hyperspace included when it has it (mods_linux.c looks inside),
// unless it's the original game's again.
bool switch_mods_changed(const switch_mod *mods, int n) {
    char sp[700];
    stamp_path(sp, sizeof sp);
    char *last = read_text(sp);
    static char now[32768];
    signature(mods, n, now, sizeof now);
    bool changed, on = count_on(mods, n) > 0;
    if (!last || !strcmp(last, "failed\n")) {
        changed = on;  // nothing applied here yet (the last try failed and put the original back)
    } else {
        const char *dl = dat_line(last), *dn = dat_line(now);
        bool same_mods = (size_t)(dl - last) == (size_t)(dn - now) && !strncmp(last, now, (size_t)(dn - now));
        bool same_dat = !strcmp(dl, dn);
        bool had_mods = dl != last;
        if (same_mods) changed = on && !same_dat && dat_is_original();
        else if (on) changed = true;
        else changed = had_mods && same_dat;  // all taken off: the original back, unless ftl.dat was replaced
        if (changed && same_mods) LOGI("mods: ftl.dat is the original again: applying the mods again");
        if (!changed && !same_dat) LOGI("mods: ftl.dat was replaced (patched on a PC?): starting with it as it is");
        if (!changed) write_text(sp, now);
    }
    free(last);
    return changed;
}

static void (*g_print)(const char *line);

static void ftlman_log(int level, const char *msg) {
    if (level >= 2) LOGE("%s", msg);
    else if (level == 1) LOGW("%s", msg);
    else LOGI("%s", msg);
    if ((level == 0 || level >= 2) && g_print) g_print(msg);
}

static struct {
    const char **paths;
    int n, rc;
    char err[1024];
} g_job;

// ftlman on its own thread: its XML and Lua work wants more stack than the main thread has.
static void *patch_thread(void *p) {
    (void)p;
    long free_mb, total_mb, used_mb;
    port_memory(&free_mb, &total_mb, &used_mb);
    size_t memory = free_mb > PATCH_RESERVE_MB ? (size_t)(free_mb - PATCH_RESERVE_MB) << 20 : 0;
    g_job.rc = ftlman_patch(g_paths.game_dir, g_job.paths, (size_t)g_job.n, memory, ftlman_log, g_job.err,
                            sizeof g_job.err);
    return NULL;
}

static int restore_vanilla(void) {
    char van[700], dat[700], tmp[720];
    snprintf(van, sizeof van, "%s/ftl.dat.vanilla", g_paths.game_dir);
    snprintf(dat, sizeof dat, "%s/ftl.dat", g_paths.game_dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", dat);
    if (access(van, R_OK) != 0) return 0;  // never patched here
    if (copy_file(van, tmp, 0644) != 0 || replace_file(tmp, dat) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int switch_mods_apply(const switch_mod *mods, int n, void (*print)(const char *line), char *err, int errlen) {
    char dir[600], sp[700], line[700];
    mods_dir(dir, sizeof dir);
    stamp_path(sp, sizeof sp);
    g_print = print;
    static char now[32768];
    if (count_on(mods, n) == 0) {
        if (print) print("No mods on: back to the original game.");
        LOGI("mods: none on; restoring the original ftl.dat");
        if (restore_vanilla() != 0) {
            snprintf(err, (size_t)errlen, "Couldn't put the original ftl.dat back.");
            return -1;
        }
    } else {
        static const char *paths[SWITCH_MAX_MODS];
        static char full[SWITCH_MAX_MODS][600];
        int k = 0;
        for (int i = 0; i < n; i++)
            if (mods[i].on) {
                snprintf(full[k], sizeof full[k], "%s/%s", dir, mods[i].name);
                paths[k] = full[k];
                k++;
            }
        snprintf(line, sizeof line, "Applying %d mod%s with ftlman:", k, k == 1 ? "" : "s");
        if (print) print(line);
        LOGI("mods: applying %d mods", k);
        g_job.paths = paths;
        g_job.n = k;
        g_job.rc = -1;
        g_job.err[0] = 0;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 8 << 20);
        pthread_t th;
        if (pthread_create(&th, &a, patch_thread, NULL) == 0) pthread_join(th, NULL);
        else snprintf(g_job.err, sizeof g_job.err, "couldn't start ftlman");
        if (g_job.rc != 0) {
            // ftlman may have patched ftl.dat in place: put the original back rather than leave half a patch
            restore_vanilla();
            LOGE("mods: %s", g_job.err);
            snprintf(err, (size_t)errlen, "%s", g_job.err[0] ? g_job.err : "(see logs/)");
            write_text(sp, "failed\n");  // try again next time
            return -1;
        }
        if (print) print("Done.");
    }
    signature(mods, n, now, sizeof now);  // with the new ftl.dat
    write_text(sp, now);
    return 0;
}

// ---- the mod list (L on the start screen) ------------------------------------

#define MENU_ROWS 30  // mods shown at once; libnx's console is 80x45

static void menu_draw(const switch_mod *mods, int n, int sel, int top, bool dirty, const char *note) {
    char dir[600], p[900];
    mods_dir(dir, sizeof dir);
    printf("\x1b[2J\x1b[H");  // clear, home
    printf("FTL mods  (/switch/ftl/mods/)\n\n");
    if (n == 0) printf("  No mods yet: put .ftl or .zip files in /switch/ftl/mods/.\n");
    for (int i = top; i < n && i < top + MENU_ROWS; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, mods[i].name);
        struct stat st;
        char size[16] = "";
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) snprintf(size, sizeof size, "%5.1f MB", st.st_size / 1048576.0);
        printf("%s%3d [%c] %-56.56s %s\n", i == sel ? ">" : " ", i + 1, mods[i].on ? 'x' : ' ', mods[i].name, size);
    }
    if (n > MENU_ROWS) printf("      (%d-%d of %d)\n", top + 1, top + MENU_ROWS < n ? top + MENU_ROWS : n, n);
    int on = count_on(mods, n);
    printf("\n%d of %d on%s\n", on, n, on == 0 ? ": the original game" : "");
    printf(dirty ? "Changed: + applies the changes and starts, X applies them now.\n\n" : "\n");
    printf("  Up/Down  choose            A  on/off\n"
           "  L / R    move up / down    -  all off (the original game)\n"
           "  +        apply and start   X  apply now (always rebuilds ftl.dat)\n"
           "  Y        settings          B  start without patching\n");
    if (note) printf("\n%s\n", note);
    consoleUpdate(NULL);
}

static void console_print(const char *line);

// X in the mod list: applies the mods that are on now, changed or not (also when
// ftl.dat was changed by hand), and says how it went
static const char *apply_now(const switch_mod *mods, int n) {
    static char note[1200];
    printf("\x1b[2J\x1b[H");
    LOGI("mods: applying now (the mod list's X)");
    for (int i = 0, k = 0; i < n; i++)
        if (mods[i].on) printf("  %d. %s\n", ++k, mods[i].name);
    char err[1024];
    if (switch_mods_apply(mods, n, console_print, err, sizeof err) != 0)
        snprintf(note, sizeof note, "The mods couldn't be applied:\n%s\nThe original game is back.", err);
    else if (count_on(mods, n) == 0)
        snprintf(note, sizeof note, "The original game is back. Press + or B to start.");
    else
        snprintf(note, sizeof note, "Applied. Press + or B to start.");
    return note;
}

bool switch_mods_menu(void) {
    static switch_mod mods[SWITCH_MAX_MODS];
    int n = switch_mods_list(mods, SWITCH_MAX_MODS);
    LOGI("mods: the mod list (%d mods)", n);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    padUpdate(&pad);  // L, held to get here, doesn't count
    int sel = 0, top = 0;
    bool dirty = false, applied = false;
    const char *note = NULL;
    menu_draw(mods, n, sel, top, dirty, note);
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (!down) {
            consoleUpdate(NULL);
            continue;
        }
        note = NULL;
        // Hyperspace stays first: the mods that build on it expect its files before theirs
        bool hs_first = n > 0 && is_hyperspace(mods[0].name);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            sel = sel > 0 ? sel - 1 : (n ? n - 1 : 0);
        } else if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            sel = sel < n - 1 ? sel + 1 : 0;
        } else if ((down & HidNpadButton_A) && n) {
            mods[sel].on = !mods[sel].on;
            dirty = true;
        } else if ((down & (HidNpadButton_L | HidNpadButton_R)) && n) {
            int to = sel + ((down & HidNpadButton_L) ? -1 : 1);
            if (hs_first && (sel == 0 || to == 0)) {
                note = "Hyperspace stays first: the mods that need it build on its files.";
            } else if (to >= 0 && to < n) {
                switch_mod t = mods[sel];
                mods[sel] = mods[to];
                mods[to] = t;
                sel = to;
                dirty = true;
            }
        } else if (down & HidNpadButton_Minus) {
            for (int i = 0; i < n; i++) mods[i].on = false;
            dirty = true;
        } else if (down & (HidNpadButton_Plus | HidNpadButton_X)) {
            if (dirty && switch_mods_save(mods, n) != 0) {
                note = "Couldn't write mods/order.txt: is the SD card full or read-only?";
            } else {
                if (dirty) LOGI("mods: %d of %d on, saved to mods/order.txt", count_on(mods, n), n);
                applied |= dirty;
                dirty = false;
                if (down & HidNpadButton_Plus) break;  // startup applies them
                note = apply_now(mods, n);
            }
        } else if (down & HidNpadButton_Y) {
            char settings[700];
            snprintf(settings, sizeof settings, "%s/settings.ini", g_paths.config_dir);
            switch_settings_menu(settings, true);
        } else if (down & HidNpadButton_B) {
            break;  // without what changed since the last + or X
        }
        if (sel < top) top = sel;
        if (sel >= top + MENU_ROWS) top = sel - MENU_ROWS + 1;
        menu_draw(mods, n, sel, top, dirty, note);
    }
    printf("\x1b[2J\x1b[H");
    consoleUpdate(NULL);
    return applied;
}

// ftl.nro at startup: applies the mods that are on when they changed since last time.
static void console_print(const char *line) {
    printf("%s\n", line);
    consoleUpdate(NULL);
}

int mods_switch_apply(void (*console_on)(void)) {
    static switch_mod mods[SWITCH_MAX_MODS];
    int n = switch_mods_list(mods, SWITCH_MAX_MODS);
    if (!switch_mods_changed(mods, n)) return 0;
    console_on();
    for (int i = 0, k = 0; i < n; i++)
        if (mods[i].on) printf("  %d. %s\n", ++k, mods[i].name);
    char err[1024];
    if (switch_mods_apply(mods, n, console_print, err, sizeof err) != 0) {
        char msg[1400];
        snprintf(msg, sizeof msg,
                 "The mods couldn't be applied:\n%s\n\nPlaying without them. Switch the mod off in the mod list "
                 "(L on the start screen) or remove it, and start FTL again.",
                 err);
        switch_show_error(msg);
        return -1;
    }
    return 0;
}
