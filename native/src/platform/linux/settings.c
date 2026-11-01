// Paths (XDG base directories, or the portable folder) and settings.ini for the Linux app.
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "linux_app.h"
#include "port_log.h"

app_paths g_paths;

int mkdirs(const char *path) {
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    return mkdir(p, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

// $VAR if set and absolute, else $HOME/fallback
static void xdg(char *out, size_t n, const char *var, const char *fallback) {
    const char *v = getenv(var);
    const char *home = getenv("HOME");
    if (v && v[0] == '/') snprintf(out, n, "%s/ftl-linux", v);
    else snprintf(out, n, "%s/%s/ftl-linux", home ? home : ".", fallback);
}

void paths_init(void) {
    app_paths *p = &g_paths;
    char self[PATH_MAX];
    ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    self[l > 0 ? l : 0] = 0;
    char *slash = strrchr(self, '/');
    if (slash) *slash = 0;
    snprintf(p->exe_dir, sizeof p->exe_dir, "%s", self[0] ? self : ".");
    char marker[600];
    snprintf(marker, sizeof marker, "%s/portable", p->exe_dir);
    struct stat st;
    p->portable = stat(marker, &st) == 0 && S_ISREG(st.st_mode);
    if (p->portable) {
        snprintf(p->data_dir, sizeof p->data_dir, "%s", p->exe_dir);
        snprintf(p->config_dir, sizeof p->config_dir, "%s/data", p->exe_dir);
        snprintf(p->saves_dir, sizeof p->saves_dir, "%s/data/saves", p->exe_dir);
        snprintf(p->cache_dir, sizeof p->cache_dir, "%s/data/cache", p->exe_dir);
        snprintf(p->state_dir, sizeof p->state_dir, "%s/data/logs", p->exe_dir);
        snprintf(p->game_dir, sizeof p->game_dir, "%s/game", p->exe_dir);
        return;
    }
    xdg(p->config_dir, sizeof p->config_dir, "XDG_CONFIG_HOME", ".config");
    xdg(p->data_dir, sizeof p->data_dir, "XDG_DATA_HOME", ".local/share");
    xdg(p->cache_dir, sizeof p->cache_dir, "XDG_CACHE_HOME", ".cache");
    xdg(p->state_dir, sizeof p->state_dir, "XDG_STATE_HOME", ".local/state");
    snprintf(p->game_dir, sizeof p->game_dir, "%s/game", p->data_dir);
    snprintf(p->saves_dir, sizeof p->saves_dir, "%s/saves", p->data_dir);
}

// ---------------------------------------------------------------------------
#define MAX_SETTINGS 128
static struct {
    char key[64], val[256];
} g_set[MAX_SETTINGS];
static int g_nset;

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

void settings_load(const char *path) {
    FILE *f = fopen(path, "re");
    if (!f) return;
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) {
            LOGW("settings.ini:%d: no '=' in '%s'", lineno, s);
            continue;
        }
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        // a comment after the value (not in quotes)
        if (*v != '"') {
            char *c = strchr(v, '#');
            if (c) *c = 0;
            v = trim(v);
        }
        // a value in quotes keeps its spaces
        size_t vl = strlen(v);
        if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') {
            v[vl - 1] = 0;
            v++;
        }
        int i;
        for (i = 0; i < g_nset; i++)
            if (!strcmp(g_set[i].key, k)) break;
        if (i == g_nset) {
            if (g_nset == MAX_SETTINGS) continue;
            g_nset++;
        }
        snprintf(g_set[i].key, sizeof g_set[i].key, "%s", k);
        snprintf(g_set[i].val, sizeof g_set[i].val, "%s", v);
    }
    fclose(f);
}

void settings_override(const char *key, const char *val) {
    int i;
    for (i = 0; i < g_nset; i++)
        if (!strcmp(g_set[i].key, key)) break;
    if (i == g_nset) {
        if (g_nset == MAX_SETTINGS) return;
        g_nset++;
    }
    snprintf(g_set[i].key, sizeof g_set[i].key, "%s", key);
    snprintf(g_set[i].val, sizeof g_set[i].val, "%s", val);
}

const char *setting_str(const char *key, const char *def) {
    for (int i = 0; i < g_nset; i++)
        if (!strcmp(g_set[i].key, key)) return g_set[i].val;
    return def;
}
int setting_int(const char *key, int def) {
    const char *v = setting_str(key, NULL);
    if (!v || !*v) return def;
    char *end;
    long r = strtol(v, &end, 0);
    return *end ? def : (int)r;
}
static int parse_bool(const char *v) {
    if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return 1;
    if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off")) return 0;
    return -1;
}
bool setting_bool(const char *key, bool def) {
    const char *v = setting_str(key, NULL);
    int b = v ? parse_bool(v) : -1;
    return b < 0 ? def : b;
}
int setting_tristate(const char *key) {
    const char *v = setting_str(key, "auto");
    return parse_bool(v);  // "auto" and anything else: -1
}

static const char DEFAULT_SETTINGS[] =
    "# ftl-linux settings. Lines starting with # are comments; delete a line to get its default.\n"
    "\n"
    "# Window\n"
    "fullscreen = false          # F11 or Alt+Enter switches while playing\n"
    "window_width = 0            # 0: 80% of the screen, with the screen's shape (so fullscreen fits)\n"
    "window_height = 0\n"
    "vsync = true\n"
    "render_height = 0           # 0: automatic (1536 on big windows, else 768); or 768 / 1536\n"
    "pause_on_focus_loss = true  # minimizing always pauses\n"
    "\n"
    "# Touch helpers: auto = off with a mouse, on once a touchscreen is used\n"
    "magnifier = auto            # the loupe over your finger\n"
    "pinch_zoom = auto           # two fingers zoom and pan (the mouse wheel always zooms)\n"
    "auto_zoom = auto            # zoom in on the map and the upgrade screen\n"
    "event_zoom = false          # zoom in on events too\n"
    "minimap = true              # where you are while zoomed in\n"
    "two_finger_pause = true\n"
    "event_text = 0              # event text size: 0 the game's, 1 large, 2 larger\n"
    "resume_run = false          # continue the saved run at launch\n"
    "\n"
    "# Game controller\n"
    "pad_magnetism = true        # the cursor is drawn to buttons\n"
    "pad_rumble = true\n"
    "pad_touchpad = true         # a DualSense/DualShock touchpad moves the cursor\n"
    "# Buttons: pad_<a|b|x|y|lb|rb|lt|rt|l3|r3|start|select|home> = <none|tap|right_click|back|pause|\n"
    "#   zoom_in|zoom_out|reset_zoom|magnifier|turbo|precision>. Defaults:\n"
    "# pad_a = tap\n"
    "# pad_b = back\n"
    "# pad_start = back\n"
    "# pad_x = pause\n"
    "# pad_lb = zoom_out\n"
    "# pad_rb = zoom_in\n"
    "# pad_lt = magnifier\n"
    "# pad_rt = right_click\n"
    "# pad_select = reset_zoom\n"
    "# pad_r3 = reset_zoom\n";

void settings_write_default(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return;
    FILE *f = fopen(path, "we");
    if (!f) return;
    fputs(DEFAULT_SETTINGS, f);
    fclose(f);
}
