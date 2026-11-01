// The built-in updater of release builds (FTL_SWITCH_UPDATE_REPO, set by
// RELEASE=1 switch/build.sh): at start, on the text console, it asks GitHub for
// the repository's latest release. When that is newer than this ftl.nro and has
// an ftl.nro asset, it offers it: A updates (download, check, replace, start
// again), B starts as it is, X skips that version (/switch/ftl/update-skip).
// check_updates = false in settings.ini turns it off. Builds without the define
// have none of this.
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "../linux/linux_app.h"
#include "port_log.h"
#include "switch_app.h"

#define API_TIMEOUT 8L
#define MAX_JSON (1 << 20)
#define MAX_NRO (128 << 20)

// ---- the release, from the API's JSON ----------------------------------------

typedef struct {
    char tag[64];
    char url[512];   // browser_download_url of ftl.nro
    char sha256[65]; // its digest, when GitHub gives one
    long long size;
} release;

typedef struct {
    const char *p, *end;
} json;

static void ws(json *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t')) j->p++;
}
static bool eat(json *j, char c) {
    ws(j);
    if (j->p < j->end && *j->p == c) {
        j->p++;
        return true;
    }
    return false;
}
// A string into out (escapes other than \" \\ \/ dropped: none of the fields read has them)
static bool string(json *j, char *out, size_t n) {
    if (!eat(j, '"')) return false;
    size_t l = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            c = *j->p++;
            if (c == 'u') j->p += 4 <= j->end - j->p ? 4 : j->end - j->p;
            if (c != '"' && c != '\\' && c != '/') continue;
        }
        if (out && l + 1 < n) out[l++] = c;
    }
    if (out && n) out[l] = 0;
    return eat(j, '"');
}
static bool skip(json *j, int depth) {
    ws(j);
    if (j->p >= j->end || depth > 64) return false;
    char c = *j->p;
    if (c == '"') return string(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++;
        if (eat(j, close)) return true;
        do {
            if (c == '{' && (!string(j, NULL, 0) || !eat(j, ':'))) return false;
            if (!skip(j, depth + 1)) return false;
        } while (eat(j, ','));
        return eat(j, close);
    }
    while (j->p < j->end && !strchr(",}] \n\r\t", *j->p)) j->p++;  // number, true, false, null
    return true;
}
static long long number(json *j) {
    ws(j);
    long long v = strtoll(j->p, NULL, 10);
    skip(j, 0);
    return v;
}

// One entry of "assets": keeps it when it's ftl.nro
static bool asset(json *j, release *r) {
    char key[32], name[64] = "", url[512] = "", digest[80] = "";
    long long size = -1;
    if (!eat(j, '{')) return false;
    if (!eat(j, '}')) {
        do {
            if (!string(j, key, sizeof key) || !eat(j, ':')) return false;
            bool ok = true;
            if (!strcmp(key, "name")) ok = string(j, name, sizeof name);
            else if (!strcmp(key, "browser_download_url")) ok = string(j, url, sizeof url);
            else if (!strcmp(key, "digest")) ok = string(j, digest, sizeof digest) || (skip(j, 0), true);
            else if (!strcmp(key, "size")) size = number(j);
            else ok = skip(j, 1);
            if (!ok) return false;
        } while (eat(j, ','));
        if (!eat(j, '}')) return false;
    }
    if (!strcmp(name, "ftl.nro") && !strncmp(url, "https://", 8)) {
        snprintf(r->url, sizeof r->url, "%s", url);
        r->size = size;
        r->sha256[0] = 0;
        if (!strncmp(digest, "sha256:", 7) && strlen(digest + 7) == 64) snprintf(r->sha256, sizeof r->sha256, "%s", digest + 7);
    }
    return true;
}

static bool parse_release(const char *text, size_t n, release *r) {
    json j = {text, text + n};
    char key[32];
    memset(r, 0, sizeof *r);
    if (!eat(&j, '{')) return false;
    if (eat(&j, '}')) return false;
    do {
        if (!string(&j, key, sizeof key) || !eat(&j, ':')) return false;
        bool ok;
        if (!strcmp(key, "tag_name")) ok = string(&j, r->tag, sizeof r->tag);
        else if (!strcmp(key, "assets") && eat(&j, '[')) {
            ok = true;
            if (!eat(&j, ']')) {
                do ok = asset(&j, r);
                while (ok && eat(&j, ','));
                ok = ok && eat(&j, ']');
            }
        } else ok = skip(&j, 1);
        if (!ok) return false;
    } while (eat(&j, ','));
    return r->tag[0] != 0;
}

// "v1.2.3" (anything after the numbers ignored); false for other versions (dev builds)
static bool version(const char *s, int v[3]) {
    if (*s == 'v' || *s == 'V') s++;
    return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) == 3;
}
static bool newer(const char *tag, const char *mine) {
    int a[3], b[3];
    if (!version(tag, a) || !version(mine, b)) return false;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

// ---- HTTPS ---------------------------------------------------------------------

typedef struct {
    char *data;
    size_t len;
} buffer;

static size_t to_buffer(char *p, size_t size, size_t n, void *user) {
    buffer *b = user;
    size_t add = size * n;
    if (b->len + add > MAX_JSON) return 0;
    char *d = realloc(b->data, b->len + add + 1);
    if (!d) return 0;
    memcpy(d + b->len, p, add);
    b->len += add;
    d[b->len] = 0;
    b->data = d;
    return add;
}

static CURL *session(const char *url) {
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "ftl.nro/" FTL_SWITCH_VERSION);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    return c;
}

static bool latest_release(release *r, char *err, size_t errn) {
    CURL *c = session("https://api.github.com/repos/" FTL_SWITCH_UPDATE_REPO "/releases/latest");
    if (!c) return false;
    struct curl_slist *h = curl_slist_append(NULL, "Accept: application/vnd.github+json");
    buffer b = {0};
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, API_TIMEOUT);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_buffer);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    CURLcode rc = curl_easy_perform(c);
    bool ok = rc == CURLE_OK && b.data && parse_release(b.data, b.len, r);
    if (rc != CURLE_OK) snprintf(err, errn, "%s", curl_easy_strerror(rc));
    else if (!ok) snprintf(err, errn, "unexpected answer from GitHub");
    free(b.data);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return ok;
}

typedef struct {
    FILE *f;
    long long got;
    Sha256Context sha;
    int shown;
} download;

static size_t to_file(char *p, size_t size, size_t n, void *user) {
    download *d = user;
    size_t add = size * n;
    if (d->got + (long long)add > MAX_NRO || fwrite(p, 1, add, d->f) != add) return 0;
    sha256ContextUpdate(&d->sha, p, add);
    d->got += add;
    return add;
}
static int progress(void *user, curl_off_t total, curl_off_t now, curl_off_t ut, curl_off_t un) {
    (void)ut, (void)un;
    download *d = user;
    int pct = total > 0 ? (int)(now * 100 / total) : -1;
    if (pct >= 0 && pct != d->shown) {
        d->shown = pct;
        printf("\r  %3d%%  (%lld of %lld KB)", pct, (long long)now / 1024, (long long)total / 1024);
        consoleUpdate(NULL);
    }
    return appletMainLoop() ? 0 : 1;  // stop when the app is being closed
}

// Downloads r into path and checks it: size, SHA-256 (when known) and the NRO header
static bool fetch(const release *r, const char *path, char *err, size_t errn) {
    download d = {fopen(path, "wb"), 0, {0}, -1};
    if (!d.f) {
        snprintf(err, errn, "can't write %s", path);
        return false;
    }
    sha256ContextCreate(&d.sha);
    CURL *c = session(r->url);
    CURLcode rc = CURLE_FAILED_INIT;
    if (c) {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_file);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);  // give up when stalled for 30 s
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
        rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
    }
    bool ok = fclose(d.f) == 0 && rc == CURLE_OK;
    printf("\n");
    if (rc != CURLE_OK) {
        snprintf(err, errn, "download failed: %s", curl_easy_strerror(rc));
        return false;
    }
    if (!ok || (r->size >= 0 && d.got != r->size)) {
        snprintf(err, errn, "download incomplete (%lld of %lld bytes)", d.got, r->size);
        return false;
    }
    u8 hash[SHA256_HASH_SIZE];
    sha256ContextGetHash(&d.sha, hash);
    if (r->sha256[0]) {
        char hex[65];
        for (int i = 0; i < SHA256_HASH_SIZE; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
        if (strcasecmp(hex, r->sha256)) {
            snprintf(err, errn, "the download doesn't match the release's checksum");
            return false;
        }
    }
    // An NRO: "NRO0" at 0x10, its size at 0x18
    u8 head[0x20];
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(head, 1, sizeof head, f) : 0;
    if (f) fclose(f);
    u32 nro_size;
    memcpy(&nro_size, head + 0x18, 4);
    if (got != sizeof head || memcmp(head + 0x10, "NRO0", 4) || nro_size > d.got) {
        snprintf(err, errn, "the download isn't an .nro");
        return false;
    }
    return true;
}

// ---- the console ----------------------------------------------------------------

static u64 wait_button(u64 mask) {
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    padUpdate(&pad);  // buttons already held don't count
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad) & mask;
        if (down) return down;
        consoleUpdate(NULL);
    }
    return 0;
}

static void skip_path(char *out, size_t n) { snprintf(out, n, "%s/update-skip", g_paths.config_dir); }

static bool skipped(const char *tag) {
    char p[700], line[64] = "";
    skip_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    bool got = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    line[strcspn(line, "\r\n")] = 0;
    return got && !strcmp(line, tag);
}
static void skip_version(const char *tag) {
    char p[700];
    skip_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "%s\n", tag);
        fclose(f);
    }
}

// Puts the downloaded file in place of self, keeping self when that fails
static bool replace(const char *self, const char *tmp, char *err, size_t errn) {
    char old[1100];
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
    if (rename(self, old) != 0) {
        snprintf(err, errn, "can't rename %s", self);
        return false;
    }
    if (rename(tmp, self) != 0) {
        rename(old, self);
        snprintf(err, errn, "can't put the new ftl.nro in place");
        return false;
    }
    remove(old);
    return true;
}

static bool online(void) {
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return false;
    NifmInternetConnectionType type;
    u32 strength;
    NifmInternetConnectionStatus status;
    bool on = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
              status == NifmInternetConnectionStatus_Connected;
    nifmExit();
    return on;
}

static char g_self[1024];
static bool g_restart;

static bool is_nro(const char *p) { return p && strlen(p) > 4 && !strcasecmp(p + strlen(p) - 4, ".nro"); }

void switch_update_check(const char *argv0) {
    if (!setting_bool("check_updates", true)) return;
    // The loader passes the .nro's path; without it (emulators), the usual place
    const char *self = is_nro(argv0) ? argv0 : "sdmc:/switch/ftl/ftl.nro";
    FILE *f = fopen(self, "rb");
    if (!f) {
        LOGW("update: not checking, can't find ftl.nro itself (%s)", self);
        return;
    }
    fclose(f);
    snprintf(g_self, sizeof g_self, "%s", self);
    int mine[3];
    if (!version(FTL_SWITCH_VERSION, mine)) {
        LOGI("update: not checking, %s isn't a release version", FTL_SWITCH_VERSION);
        return;
    }
    if (!online()) {
        LOGI("update: offline, not checking");
        return;
    }
    if (R_FAILED(socketInitializeDefault())) return;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("Looking for updates...\n");
    consoleUpdate(NULL);

    release r;
    char err[300] = "";
    if (!latest_release(&r, err, sizeof err)) {
        LOGW("update: no answer from GitHub (%s)", err);
        printf("  (couldn't check: %s)\n", err);
    } else if (!newer(r.tag, FTL_SWITCH_VERSION)) {
        LOGI("update: %s is the latest release", FTL_SWITCH_VERSION);
    } else if (!r.url[0]) {
        LOGW("update: release %s has no ftl.nro", r.tag);
    } else if (skipped(r.tag)) {
        LOGI("update: %s available, skipped by the player", r.tag);
        printf("  %s is available (skipped; delete update-skip to be asked again)\n", r.tag);
    } else {
        LOGI("update: %s available (this is %s)", r.tag, FTL_SWITCH_VERSION);
        printf("\n"
               "A new version of FTL for Switch is available: %s\n"
               "(this one is %s)\n"
               "\n"
               "  A  Update now\n"
               "  B  Start without updating\n"
               "  X  Skip this version\n",
               r.tag, FTL_SWITCH_VERSION);
        consoleUpdate(NULL);
        u64 b = wait_button(HidNpadButton_A | HidNpadButton_B | HidNpadButton_X);
        if (b & HidNpadButton_X) {
            skip_version(r.tag);
            printf("\nSkipped %s.\n\n", r.tag);
        } else if (b & HidNpadButton_A) {
            char tmp[1100];
            snprintf(tmp, sizeof tmp, "%s.new", self);
            printf("\nDownloading %s ...\n", r.tag);
            consoleUpdate(NULL);
            if (fetch(&r, tmp, err, sizeof err) && replace(self, tmp, err, sizeof err)) {
                LOGI("update: installed %s", r.tag);
                g_restart = true;
            } else {
                remove(tmp);
                LOGE("update: %s", err);
                printf("\nThe update failed: %s\n"
                       "This version starts instead. Press A to go on.\n", err);
                consoleUpdate(NULL);
                wait_button(HidNpadButton_A);
            }
        } else {
            printf("\n");
        }
        consoleUpdate(NULL);
    }
    curl_global_cleanup();
    socketExit();
}

const char *switch_update_installed(void) { return g_restart ? g_self : NULL; }
