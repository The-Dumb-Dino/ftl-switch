// The settings menu (R on the start screen): settings.ini on the console, so the
// common settings don't need a PC or a file editor. Writes only what was changed
// and keeps the file's comments (settings_save).
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#include "../linux/linux_app.h"
#include "port_log.h"
#include "switch_app.h"

#define MAX_CHOICES 12

typedef struct {
    const char *key;    // NULL: a heading (label)
    const char *label;
    const char *def;    // the value without a line in settings.ini
    const char *values[MAX_CHOICES];  // as written to settings.ini; NULL-terminated
    const char *names[MAX_CHOICES];   // as shown (NULL: the value itself)
    const char *help;
} item;

#define BOOL {"true", "false"}, {"on", "off"}
#define ACTIONS                                                                                              \
    {"none", "tap", "right_click", "back", "pause", "zoom_in", "zoom_out", "reset_zoom", "magnifier", "turbo", \
     "precision"},                                                                                           \
    {"nothing", "tap", "right click", "back", "pause", "zoom in", "zoom out", "reset zoom", "magnifier",      \
     "turbo", "precision"}
#define PAD_HELP "What the button does in the game. The zoom actions need Zoom on."

static const item ITEMS[] = {
    {NULL, "Start", NULL, {0}, {0}, NULL},
    {"hide_start_screen", "Hide the start screen", "false", BOOL,
     "No text screen at start; it still shows when mods are applied, for an update or an error. "
     "L and R still open the menus while FTL starts."},
    {"check_updates", "Look for updates", "true", BOOL, "Offer new versions from GitHub at start (when online)."},
    {"hyperspace", "Start Hyperspace", "true", BOOL,
     "Start Hyperspace when the mods include it. Off: the game without it; mods made for Hyperspace may not "
     "work then."},
    {"resume_run", "Continue the saved run", "false", BOOL, "Go straight into the saved run at start."},
    {"allow_applet_mode", "Allow starting from the Album", "false", BOOL,
     "The Album gives homebrew little memory: FTL may crash there."},

    {NULL, "Picture", NULL, {0}, {0}, NULL},
    {"docked_1080p", "Docked at 1080p", "true", BOOL, "Docked: draw at 1080p (off: 720p, scaled up by the console)."},
    {"render_height", "Render height", "0", {"0", "768", "1536"}, {"automatic", "768", "1536"},
     "The game's own resolution. Automatic fits the screen."},
    {"event_text", "Event text size", "0", {"0", "1", "2"}, {"the game's", "large", "larger"},
     "Text size in events."},

    {NULL, "Touch", NULL, {0}, {0}, NULL},
    {"two_finger_pause", "Two-finger tap pauses", "true", BOOL, "Tap with two fingers to pause the game."},
    {"magnifier", "Magnifier", "false", BOOL, "A loupe over a held finger (ZL shows it either way)."},

    {NULL, "Zoom", NULL, {0}, {0}, NULL},
    {"zoom", "Zoom", "false", BOOL,
     "Pinch and pan, zooming in on the map and the upgrade screen, the minimap and the zoom buttons. "
     "Off: the whole game fits the screen."},
    {"pinch_zoom", "  Pinch to zoom", "true", BOOL, "Two fingers pinch and pan (with Zoom on)."},
    {"auto_zoom", "  Zoom in on map and upgrades", "auto", {"auto", "true", "false"}, {"handheld", "on", "off"},
     "Zoom in on the map and the upgrade screen (with Zoom on)."},
    {"event_zoom", "  Zoom in on events", "false", BOOL, "Zoom in on event text (with Zoom on)."},
    {"minimap", "  Minimap", "true", BOOL, "Shows where you are while zoomed in (with Zoom on)."},

    {NULL, "Controller", NULL, {0}, {0}, NULL},
    {"pad_magnetism", "Cursor drawn to buttons", "true", BOOL, "The cursor is drawn to buttons."},
    {"pad_rumble", "Rumble", "true", BOOL, "Rumble on the controller."},
    {"pad_a", "  A", "tap", ACTIONS, PAD_HELP},
    {"pad_b", "  B", "back", ACTIONS, PAD_HELP},
    {"pad_x", "  X", "pause", ACTIONS, PAD_HELP},
    {"pad_y", "  Y", "none", ACTIONS, PAD_HELP},
    {"pad_l", "  L", "zoom_out", ACTIONS, PAD_HELP},
    {"pad_r", "  R", "zoom_in", ACTIONS, PAD_HELP},
    {"pad_zl", "  ZL", "magnifier", ACTIONS, PAD_HELP},
    {"pad_zr", "  ZR", "right_click", ACTIONS, PAD_HELP},
    {"pad_plus", "  +", "back", ACTIONS, PAD_HELP},
    {"pad_minus", "  -", "reset_zoom", ACTIONS, PAD_HELP},
    {"pad_lstick", "  Left stick press", "none", ACTIONS, PAD_HELP},
    {"pad_rstick", "  Right stick press", "reset_zoom", ACTIONS, PAD_HELP},

    {NULL, "Logs", NULL, {0}, {0}, NULL},
    {"verbose", "Detailed logs", "false", BOOL, "More detail in logs/ (for bug reports)."},
};
#define N_ITEMS ((int)(sizeof ITEMS / sizeof *ITEMS))
#define ROWS 26  // rows shown at once (with the blank lines before headings, the help and
                 // the keys, that fills libnx's 80x45 console)

// The chosen value of each item: an index into values, or -1 for a value from
// settings.ini that isn't one of them (shown as it is until changed)
static int g_cur[N_ITEMS], g_was[N_ITEMS];
static char g_raw[N_ITEMS][64];

static int n_values(const item *it) {
    int n = 0;
    while (n < MAX_CHOICES && it->values[n]) n++;
    return n;
}

static bool is_bool(const item *it) {
    return n_values(it) == 2 && !strcmp(it->values[0], "true") && !strcmp(it->values[1], "false");
}

static int index_of(const item *it, const char *v) {
    int n = n_values(it);
    for (int i = 0; i < n; i++)
        if (!strcasecmp(v, it->values[i])) return i;
    if (is_bool(it)) {
        // what settings_load takes for true and false; "auto" (older files' magnifier) is off
        if (!strcasecmp(v, "1") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) return 0;
        if (!strcasecmp(v, "0") || !strcasecmp(v, "no") || !strcasecmp(v, "off") || !strcasecmp(v, "auto"))
            return strcmp(it->key, "pinch_zoom") ? 1 : 0;  // pinch_zoom: auto is on
    }
    return -1;
}

static const char *shown(int i) {
    const item *it = &ITEMS[i];
    if (g_cur[i] < 0) return g_raw[i];
    const char *name = it->names[g_cur[i]];
    return name ? name : it->values[g_cur[i]];
}

static void draw(int sel, int top, bool from_mods, const char *note) {
    printf("\x1b[2J\x1b[H");  // clear, home
    printf("FTL settings  (/switch/ftl/settings.ini)\n\n");
    for (int i = top; i < N_ITEMS && i < top + ROWS; i++) {
        const item *it = &ITEMS[i];
        if (!it->key) {
            printf("%s%s\n", i > top ? "\n" : "", it->label);
            continue;
        }
        bool changed = g_cur[i] != g_was[i];
        printf("%s %-36.36s %c %-24.24s\n", i == sel ? ">" : " ", it->label, changed ? '*' : ' ', shown(i));
    }
    if (top + ROWS < N_ITEMS) printf("  ...\n");
    // the selected setting's help, on up to two lines
    const char *h = ITEMS[sel].help ? ITEMS[sel].help : "";
    printf("\n");
    for (int l = 0; l < 2; l++) {
        int n = (int)strlen(h);
        if (n > 78) {
            n = 78;
            while (n > 0 && h[n] != ' ') n--;
        }
        printf("%.*s\n", n, h);
        h += n;
        while (*h == ' ') h++;
    }
    printf("\n  Up/Down  choose            A, Left/Right  change     Y  default\n");
    if (from_mods) printf("  +        save              B  back without saving\n");
    else printf("  +        save and start    B  start without saving\n");
#ifdef FTL_SWITCH_FTLMAN
    if (!from_mods) printf("  X        save and open the mod list\n");
#endif
    if (note) printf("\n%s\n", note);
    consoleUpdate(NULL);
}

// The next item that is a setting, going d (+1/-1) from i, wrapping around
static int next(int i, int d) {
    for (int k = 0; k < N_ITEMS; k++) {
        i = (i + d + N_ITEMS) % N_ITEMS;
        if (ITEMS[i].key) return i;
    }
    return i;
}

bool switch_settings_menu(const char *path, bool from_mods) {
    for (int i = 0; i < N_ITEMS; i++) {
        if (!ITEMS[i].key) continue;
        const char *v = setting_str(ITEMS[i].key, ITEMS[i].def);
        g_cur[i] = g_was[i] = index_of(&ITEMS[i], v);
        snprintf(g_raw[i], sizeof g_raw[i], "%s", v);
    }
    LOGI("settings: the settings menu");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    padUpdate(&pad);  // the button pressed to get here doesn't count
    int sel = next(-1, 1), top = 0;
    bool to_mods = false;
    const char *note = NULL;
    draw(sel, top, from_mods, note);
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (!down) {
            consoleUpdate(NULL);
            continue;
        }
        note = NULL;
        const item *it = &ITEMS[sel];
        int nv = n_values(it);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            sel = next(sel, -1);
        } else if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            sel = next(sel, 1);
        } else if (down & (HidNpadButton_A | HidNpadButton_Right | HidNpadButton_StickLRight)) {
            g_cur[sel] = g_cur[sel] < 0 ? 0 : (g_cur[sel] + 1) % nv;
        } else if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
            g_cur[sel] = g_cur[sel] <= 0 ? nv - 1 : g_cur[sel] - 1;
        } else if (down & HidNpadButton_Y) {
            g_cur[sel] = index_of(it, it->def);
#ifdef FTL_SWITCH_FTLMAN
        } else if ((down & HidNpadButton_Plus) || ((down & HidNpadButton_X) && !from_mods)) {
#else
        } else if (down & HidNpadButton_Plus) {
#endif
            to_mods = (down & HidNpadButton_X) && !from_mods;
            const char *keys[N_ITEMS], *vals[N_ITEMS];
            int n = 0;
            for (int i = 0; i < N_ITEMS; i++)
                if (ITEMS[i].key && g_cur[i] != g_was[i] && g_cur[i] >= 0) {
                    keys[n] = ITEMS[i].key;
                    vals[n++] = ITEMS[i].values[g_cur[i]];
                }
            if (n == 0) break;
            if (settings_save(path, keys, vals, n) == 0) {
                for (int i = 0; i < n; i++) LOGI("settings: %s = %s", keys[i], vals[i]);
                break;
            }
            to_mods = false;
            note = "Couldn't write settings.ini: is the SD card full or read-only?";
        } else if (down & HidNpadButton_B) {
            break;
        }
        if (sel <= top) top = sel > 0 && !ITEMS[sel - 1].key ? sel - 1 : sel;  // with its heading
        if (sel >= top + ROWS) top = sel - ROWS + 1;
        draw(sel, top, from_mods, note);
    }
    printf("\x1b[2J\x1b[H");
    consoleUpdate(NULL);
    return to_mods;
}
