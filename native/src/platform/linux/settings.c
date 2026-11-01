// Paths (the app's folder on the SD card) and settings.ini. Shared code from the
// Linux version this port grew out of, hence the folder.
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
        if (s[-1] == ':') continue;  // a device root ("sdmc:/" on the Switch)
        *s = 0;
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    return mkdir(p, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

void paths_init(void) {
    app_paths *p = &g_paths;
    // Everything in the app's own folder on the SD card, like a portable folder.
    // Written without "sdmc:": the SD card is libnx's default device, and the
    // game only takes paths that start with "/" as absolute.
    p->portable = true;
    snprintf(p->exe_dir, sizeof p->exe_dir, "/switch/ftl");
    snprintf(p->data_dir, sizeof p->data_dir, "%s", p->exe_dir);
    snprintf(p->config_dir, sizeof p->config_dir, "%s", p->exe_dir);
    snprintf(p->game_dir, sizeof p->game_dir, "%s/game", p->exe_dir);
    snprintf(p->saves_dir, sizeof p->saves_dir, "%s/saves", p->exe_dir);
    snprintf(p->cache_dir, sizeof p->cache_dir, "%s/cache", p->exe_dir);
    snprintf(p->state_dir, sizeof p->state_dir, "%s/logs", p->exe_dir);
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
    "# FTL for Switch settings. Lines starting with # are comments; delete a line to get its default.\n"
    "\n"
    "docked_1080p = true         # docked: draw at 1080p (else 720p, scaled up by the console)\n"
    "render_height = 0           # 0: automatic; or 768 / 1536\n"
    "\n"
    "# Touch helpers\n"
    "magnifier = false           # the loupe over a held finger (ZL shows it either way)\n"
    "two_finger_pause = true\n"
    "event_text = 0              # event text size: 0 the game's, 1 large, 2 larger\n"
    "resume_run = false          # continue the saved run at launch\n"
    "\n"
    "# Controllers (A confirms, B goes back)\n"
    "pad_magnetism = true        # the cursor is drawn to buttons\n"
    "pad_rumble = true\n"
    "# Buttons: pad_<a|b|x|y|l|r|zl|zr|lstick|rstick|plus|minus> = <none|tap|right_click|back|pause|\n"
    "#   zoom_in|zoom_out|reset_zoom|magnifier|turbo|precision>. Defaults:\n"
    "# pad_a = tap\n"
    "# pad_b = back\n"
    "# pad_plus = back\n"
    "# pad_x = pause\n"
    "# pad_zl = magnifier\n"
    "# pad_zr = right_click\n"
    "\n"
    "# Zoom (off: the whole game fits the screen). zoom = true turns on two-finger pinch\n"
    "# and pan, zooming in on the map and the upgrade screen, the minimap, and L/R zoom,\n"
    "# -/right stick reset. Then pinch_zoom, auto_zoom, event_zoom, minimap = true/false\n"
    "# pick among them.\n"
    "zoom = false\n"
    "\n"
    "allow_applet_mode = false   # start from the Album too (little memory: may crash)\n"
    "check_updates = true        # release builds: offer new versions from GitHub at start\n"
    "verbose = false             # more detail in logs/\n";

void settings_write_default(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return;
    FILE *f = fopen(path, "we");
    if (!f) return;
    fputs(DEFAULT_SETTINGS, f);
    fclose(f);
}
