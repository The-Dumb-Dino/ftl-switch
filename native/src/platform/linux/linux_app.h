// The Linux desktop app (ftl-linux): paths, settings, import, mods, and the
// SDL3 window it runs the game in.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// ---- paths ----
// Installed: the XDG base directories. Portable (a file named "portable" next
// to ftl-linux): everything in ftl-linux's folder.
typedef struct {
    bool portable;
    char exe_dir[512];     // where ftl-linux is
    char config_dir[512];  // $XDG_CONFIG_HOME/ftl-linux, portable: data/: settings.ini
    char data_dir[512];    // $XDG_DATA_HOME/ftl-linux, portable: the folder itself
    char game_dir[512];    //   game/: FTL, ftl.dat, Info.plist (imported; ftlman patches ftl.dat)
    char saves_dir[512];   //   saves/ (portable: data/saves/): the game's Documents
    char cache_dir[512];   // $XDG_CACHE_HOME/ftl-linux, portable: data/cache/
    char state_dir[512];   // $XDG_STATE_HOME/ftl-linux, portable: data/logs/: logs, crash marker
} app_paths;
extern app_paths g_paths;
void paths_init(void);
int mkdirs(const char *path);  // mkdir -p
int copy_file(const char *from, const char *to, int mode);  // 0 on success

// ---- settings.ini ----
void settings_load(const char *path);
const char *setting_str(const char *key, const char *def);
int setting_int(const char *key, int def);
bool setting_bool(const char *key, bool def);
// "auto" -> -1, true-ish -> 1, false-ish -> 0
int setting_tristate(const char *key);
void settings_write_default(const char *path);  // when there is none yet
void settings_override(const char *key, const char *val);  // for this run (command line)

// ---- import ----
// Copies FTL, ftl.dat and Info.plist out of an .ipa/.zip, an FTL.app folder or
// a folder holding Payload/FTL.app into dest_dir. 0 on success; otherwise
// writes the reason to err.
int import_game(const char *src, const char *dest_dir, char *err, size_t errlen);
// Checks an iOS executable can run (arm64, not FairPlay-encrypted). NULL if usable.
const char *import_check_executable(const char *path);
bool import_is_known_build(const char *path);  // FTL for iPad 1.6.21
bool import_game_present(const char *dir);
// CFBundleShortVersionString from an Info.plist (binary or XML); false if unknown.
bool import_plist_version(const char *path, char *out, size_t n);

// ---- zip reading (zip.c) ----
typedef struct zip_file zip_file;
typedef struct {
    const char *name;
    unsigned long long size, csize, offset;  // offset: of the local header
    unsigned method;
    unsigned long crc;
} zip_entry;
zip_file *zip_open(const char *path, char *err, size_t errlen);
int zip_count(zip_file *z);
const zip_entry *zip_at(zip_file *z, int i);
// Streams an entry to fn (return nonzero from fn to stop). 0 on success.
int zip_extract(zip_file *z, const zip_entry *e, int (*fn)(void *ud, const void *buf, size_t n), void *ud);
int zip_extract_to(zip_file *z, const zip_entry *e, const char *path);
void zip_close(zip_file *z);

// ---- mods (mods_linux.c): ftlman patches game/ftl.dat, the port adapts at launch ----
void mods_prepare(void);              // looks at ftl.dat; makes the image overlay if it's modded
const char *mods_active_index(void);  // the overlay to start the game with, or NULL
bool mods_take_changed(void);         // ftl.dat changed since the last start
bool mods_hyperspace_applied(void);
bool find_ftlman(char *out, size_t n);
bool find_hyperspace(char *out, size_t n);  // Hyperspace for the iPad game, if installed
int ftlman_configure(const char *ftlman_dir, const char *game_dir);  // its settings.json points at the game
int ftlman_run(void);                 // ftl-linux --ftlman: ftlman's window (doesn't return on success)

// ---- portable folder (portable.c) ----
// A folder with ftl-linux, ftlman and the game from ipa that runs from anywhere.
int portable_create(const char *folder, const char *ipa);  // ipa may be NULL: copies the imported game

// ---- the window (plat_sdl.c) ----
int sdl_app_init(void);   // window, GL, audio, input; 0 on success
void sdl_app_push_options(void);
void sdl_show_error(const char *title, const char *msg);
void sdl_message_only(const char *title, const char *msg);  // before (without) the game window
// A file chooser before the game window (zenity or kdialog): the chosen path, or false.
bool sdl_pick_file(const char *title, const char *filter, char *out, size_t n);
void sdl_toggle_fullscreen(void);
int sdl_audio_peak(void);  // the highest sample since the last call (tests)

// ---- scripted tests (testcmd.c, FTL_TEST_CMD=<fifo>) ----
void testcmd_init(void);
void testcmd_poll(void);
const char *testcmd_text(void);  // the reply to give text prompts, or NULL
