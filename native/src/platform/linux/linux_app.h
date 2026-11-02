// Paths, settings.ini, the .ipa import, zip reading and the mod image overlay:
// shared code from the Linux version this port grew out of, hence the folder
// (platform/switch/ is the Switch app itself).
#pragma once
#include <stdbool.h>
#include <stddef.h>

// ---- paths: everything in /switch/ftl/ on the SD card ----
typedef struct {
    bool portable;         // always (everything in one folder)
    char exe_dir[512];     // /switch/ftl
    char config_dir[512];  // settings.ini
    char data_dir[512];    // mods/
    char game_dir[512];    // game/: FTL, ftl.dat, Info.plist (imported; ftlman patches ftl.dat)
    char saves_dir[512];   // saves/: the game's Documents
    char cache_dir[512];   // cache/
    char state_dir[512];   // logs/: session logs, crash marker
} app_paths;
extern app_paths g_paths;
void paths_init(void);
int mkdirs(const char *path);  // mkdir -p
int copy_file(const char *from, const char *to, int mode);  // 0 on success
int replace_file(const char *from, const char *to);  // rename over an existing file (also on the Switch)

// ---- settings.ini ----
void settings_load(const char *path);
const char *setting_str(const char *key, const char *def);
int setting_int(const char *key, int def);
bool setting_bool(const char *key, bool def);
// "auto" -> -1, true-ish -> 1, false-ish -> 0
int setting_tristate(const char *key);
void settings_write_default(const char *path);  // when there is none yet
void settings_override(const char *key, const char *val);  // for this run (command line)
// Writes these settings into the file (keeping its comments and other lines) and
// uses them from now on. 0 on success.
int settings_save(const char *path, const char *const *keys, const char *const *vals, int n);

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
bool mods_dat_is_pc(void);            // ftl.dat is the PC version's, not the iPad game's
