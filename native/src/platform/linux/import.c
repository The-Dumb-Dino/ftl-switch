// Importing the player's own FTL for iPad: the files the port needs (the iOS
// executable FTL and the data package ftl.dat, plus Info.plist for the version)
// from an .ipa (a zip), an extracted FTL.app, or a folder holding Payload/FTL.app.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux_app.h"
#include "port_log.h"

static const char *const FILES[] = {"FTL", "ftl.dat", "Info.plist"};  // the last is optional
// LC_UUID of FTL for iPad 1.6.21 (arm64), the build the game hooks are made for
static const uint8_t KNOWN_UUID[16] = {0x87, 0x3d, 0x2b, 0x34, 0xd7, 0x07, 0x3d, 0x6b,
                                       0xab, 0x83, 0x26, 0x7b, 0xf4, 0xc5, 0xbb, 0xc0};

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// The arm64 slice's load commands (malloc'd; *n bytes, *ncmds commands), or NULL.
static uint8_t *load_commands(FILE *f, uint32_t *ncmds, uint32_t *n) {
    uint8_t h[32];
    long slice = 0;
    if (fseek(f, 0, SEEK_SET) || fread(h, 1, 8, f) != 8) return NULL;
    if (be32(h) == 0xcafebabe) {  // universal binary: find the arm64 slice
        uint32_t nfat = be32(h + 4);
        slice = -1;
        for (uint32_t i = 0; i < nfat && i < 16; i++) {
            uint8_t fa[20];
            if (fread(fa, 1, 20, f) != 20) return NULL;
            if (be32(fa) == 0x0100000c) slice = be32(fa + 8);
        }
        if (slice < 0) return NULL;
    }
    if (fseek(f, slice, SEEK_SET) || fread(h, 1, 32, f) != 32) return NULL;
    if (le32(h) != 0xfeedfacf || le32(h + 4) != 0x0100000c) return NULL;
    *ncmds = le32(h + 16);
    *n = le32(h + 20);
    if (*n > (1 << 20)) return NULL;
    uint8_t *c = malloc(*n);
    if (c && fread(c, 1, *n, f) != *n) {
        free(c);
        return NULL;
    }
    return c;
}

const char *import_check_executable(const char *path) {
    FILE *f = fopen(path, "rbe");
    if (!f) return "Couldn't read the FTL executable.";
    uint32_t ncmds, n;
    uint8_t *c = load_commands(f, &ncmds, &n);
    fclose(f);
    if (!c) return "The FTL executable has no 64-bit (arm64) iOS code.";
    const char *why = NULL;
    for (uint32_t i = 0, p = 0; i < ncmds && p + 8 <= n; i++) {
        uint32_t cmd = le32(c + p), size = le32(c + p + 4);
        if (cmd == 0x2c && p + 20 <= n && le32(c + p + 16) != 0)  // LC_ENCRYPTION_INFO_64 cryptid
            why = "This FTL executable is still FairPlay-encrypted, so it can't run here. "
                  "An unencrypted copy of the app is required.";
        if (!size) break;
        p += size;
    }
    free(c);
    return why;
}

bool import_is_known_build(const char *path) {
    FILE *f = fopen(path, "rbe");
    if (!f) return false;
    uint32_t ncmds, n;
    uint8_t *c = load_commands(f, &ncmds, &n);
    fclose(f);
    bool known = false;
    for (uint32_t i = 0, p = 0; c && i < ncmds && p + 24 <= n; i++) {
        uint32_t cmd = le32(c + p), size = le32(c + p + 4);
        if (cmd == 0x1b) known = !memcmp(c + p + 8, KNOWN_UUID, 16);  // LC_UUID
        if (!size) break;
        p += size;
    }
    free(c);
    return known;
}

static long long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
}

bool import_game_present(const char *dir) {
    char p[600];
    snprintf(p, sizeof p, "%s/FTL", dir);
    long long b = file_size(p);
    snprintf(p, sizeof p, "%s/ftl.dat", dir);
    return b > 1000000 && file_size(p) > 10000000;
}

// ---------------------------------------------------------------------------
// Info.plist: CFBundleShortVersionString
static uint64_t be(const uint8_t *b, size_t at, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | b[at + i];
    return v;
}
// object length and where its contents start (binary plist)
static void bp_len(const uint8_t *b, size_t len, size_t at, uint64_t *n, size_t *start) {
    *n = b[at] & 0x0f;
    *start = at + 1;
    if (*n == 0x0f && at + 1 < len) {
        int sz = 1 << (b[at + 1] & 0x0f);
        *n = be(b, at + 2, sz);
        *start = at + 2 + sz;
    }
}
static bool bp_ascii(const uint8_t *b, size_t len, size_t at, char *out, size_t outn) {
    if (at >= len || (b[at] & 0xf0) != 0x50) return false;
    uint64_t n;
    size_t s;
    bp_len(b, len, at, &n, &s);
    if (s + n > len || n >= outn) return false;
    memcpy(out, b + s, n);
    out[n] = 0;
    return true;
}

bool import_plist_version(const char *path, char *out, size_t outn) {
    FILE *f = fopen(path, "rbe");
    if (!f) return false;
    uint8_t *b = malloc(1 << 20);
    size_t len = fread(b, 1, 1 << 20, f);
    fclose(f);
    bool ok = false;
    if (len > 40 && !memcmp(b, "bplist00", 8)) {
        int off_size = b[len - 26], ref_size = b[len - 25];
        uint64_t count = be(b, len - 24, 8), top = be(b, len - 16, 8), table = be(b, len - 8, 8);
        if (table + count * off_size <= len && top < count) {
            size_t dict = be(b, table + top * off_size, off_size);
            if (dict < len && (b[dict] & 0xf0) == 0xd0) {
                uint64_t n;
                size_t s;
                bp_len(b, len, dict, &n, &s);
                for (uint64_t i = 0; i < n && s + (n + i + 1) * ref_size <= len; i++) {
                    uint64_t k = be(b, s + i * ref_size, ref_size), v = be(b, s + (n + i) * ref_size, ref_size);
                    if (k >= count || v >= count) break;
                    char key[64];
                    if (bp_ascii(b, len, be(b, table + k * off_size, off_size), key, sizeof key) &&
                        !strcmp(key, "CFBundleShortVersionString")) {
                        ok = bp_ascii(b, len, be(b, table + v * off_size, off_size), out, outn);
                        break;
                    }
                }
            }
        }
    } else if (len) {
        b[len < (1 << 20) ? len : len - 1] = 0;
        char *k = strstr((char *)b, "<key>CFBundleShortVersionString</key>");
        char *s = k ? strstr(k, "<string>") : NULL;
        char *e = s ? strstr(s, "</string>") : NULL;
        if (e && (size_t)(e - s - 8) < outn) {
            memcpy(out, s + 8, (size_t)(e - s - 8));
            out[e - s - 8] = 0;
            ok = true;
        }
    }
    free(b);
    return ok;
}

// ---------------------------------------------------------------------------
static void remove_tree(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[1024];
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            remove_tree(p);
        }
        closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

// Is `name` (a zip path) <something>.app/<file>, not under __MACOSX?
static bool is_app_file(const char *name, const char *file) {
    if (!strncmp(name, "__MACOSX", 8)) return false;
    size_t n = strlen(name), fl = strlen(file);
    if (n < fl + 6 || strcmp(name + n - fl, file) || name[n - fl - 1] != '/') return false;
    return !strncmp(name + n - fl - 5, ".app", 4);
}

// A folder holding FTL and ftl.dat: dir itself, or below it (Payload/FTL.app, *.app), 3 levels deep.
static bool find_app_dir(const char *dir, char *out, size_t n, int depth) {
    if (import_game_present(dir)) {
        snprintf(out, n, "%s", dir);
        return true;
    }
    if (depth >= 3) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "__MACOSX")) continue;
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) found = find_app_dir(p, out, n, depth + 1);
    }
    closedir(d);
    return found;
}

int import_game(const char *src, const char *dest, char *err, size_t errlen) {
    char tmp[600], old[600];
    snprintf(tmp, sizeof tmp, "%s.import.tmp", dest);
    snprintf(old, sizeof old, "%s.old", dest);
    remove_tree(tmp);
    char parent[600];
    snprintf(parent, sizeof parent, "%s", dest);
    char *slash = strrchr(parent, '/');
    if (slash) *slash = 0;
    if (mkdirs(parent) || mkdir(tmp, 0755)) {
        snprintf(err, errlen, "Can't create %s: %s", tmp, strerror(errno));
        return -1;
    }
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(err, errlen, "%s: %s", src, strerror(errno));
        return -1;
    }
    bool got[3] = {false, false, false};
    if (S_ISDIR(st.st_mode)) {
        char app[1024];
        if (!find_app_dir(src, app, sizeof app, 0)) {
            snprintf(err, errlen, "Couldn't find FTL and ftl.dat (from FTL.app) in %s.", src);
            remove_tree(tmp);
            return -1;
        }
        printf("Copying the game from %s\n", app);
        for (int i = 0; i < 3; i++) {
            char from[1200], to[700];
            snprintf(from, sizeof from, "%s/%s", app, FILES[i]);
            snprintf(to, sizeof to, "%s/%s", tmp, FILES[i]);
            if (file_size(from) < 0) continue;
            if (copy_file(from, to, i == 0 ? 0755 : 0644)) {
                snprintf(err, errlen, "Couldn't copy %s: %s", from, strerror(errno));
                remove_tree(tmp);
                return -1;
            }
            got[i] = true;
        }
    } else {
        zip_file *z = zip_open(src, err, errlen);
        if (!z) {
            remove_tree(tmp);
            return -1;
        }
        for (int i = 0; i < zip_count(z); i++) {
            const zip_entry *e = zip_at(z, i);
            for (int k = 0; k < 3; k++) {
                if (got[k] || !is_app_file(e->name, FILES[k])) continue;
                char to[700];
                snprintf(to, sizeof to, "%s/%s", tmp, FILES[k]);
                printf("Extracting %s (%.1f MB)\n", e->name, e->size / 1048576.0);
                fflush(stdout);
                if (zip_extract_to(z, e, to)) {
                    snprintf(err, errlen, "Couldn't extract %s from %s (damaged archive or full disk?)", e->name, src);
                    zip_close(z);
                    remove_tree(tmp);
                    return -1;
                }
                got[k] = true;
            }
        }
        zip_close(z);
    }
    if (!got[0] || !got[1]) {
        snprintf(err, errlen,
                 "This doesn't look like FTL for iPad: the files FTL and ftl.dat (from FTL.app) were not found in it.");
        remove_tree(tmp);
        return -1;
    }
    char bin[700];
    snprintf(bin, sizeof bin, "%s/FTL", tmp);
    const char *why = import_check_executable(bin);
    if (why) {
        snprintf(err, errlen, "%s", why);
        remove_tree(tmp);
        return -1;
    }
    chmod(bin, 0644);  // never run directly; the port loads it
    remove_tree(old);
    if (stat(dest, &st) == 0 && rename(dest, old) != 0) {
        snprintf(err, errlen, "Can't replace %s: %s", dest, strerror(errno));
        remove_tree(tmp);
        return -1;
    }
    if (rename(tmp, dest) != 0) {
        snprintf(err, errlen, "Can't move the imported files into %s: %s", dest, strerror(errno));
        rename(old, dest);
        return -1;
    }
    remove_tree(old);
    return 0;
}
