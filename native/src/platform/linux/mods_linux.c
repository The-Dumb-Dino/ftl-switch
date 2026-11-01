// What a patched ftl.dat needs at launch: ftlman (mods_switch.c, or on a PC)
// patches the game's ftl.dat and keeps the original as ftl.dat.vanilla, as it
// does with FTL on PC. Mods add PC-style .png images, while the iPad build keeps
// small images in atlases, so when ftl.dat has mod images the port needs the
// atlas lists without them (see ux/mods.c). That overlay is made here at launch
// from whatever ftl.dat is there, and kept in the cache until ftl.dat changes
// again. (Shared code from the Linux version this port grew out of.)
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "linux_app.h"
#include "port_log.h"

static void path_in(char *out, size_t n, const char *dir, const char *name) { snprintf(out, n, "%s/%s", dir, name); }
static bool ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcasecmp(s + a - b, suf);
}
static bool prefix_ci(const char *s, const char *p) { return !strncasecmp(s, p, strlen(p)); }

int copy_file(const char *from, const char *to, int mode) {
    FILE *in = fopen(from, "rbe");
    int fd = in ? open(to, O_WRONLY | O_CREAT | O_TRUNC, mode) : -1;  // no exec, and libnx refuses O_CLOEXEC
    FILE *out = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (!in || !out) {
        if (in) fclose(in);
        if (fd >= 0) close(fd);
        return -1;
    }
    char *buf = malloc(1 << 20);
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, 1 << 20, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) rc = -1;
    if (ferror(in)) rc = -1;
    free(buf);
    fclose(in);
    if (fflush(out) || fsync(fileno(out))) rc = -1;
    if (fclose(out)) rc = -1;
    return rc;
}

// rename(from, to) over an existing to: the Switch's file system refuses an
// existing target, so it's removed first.
int replace_file(const char *from, const char *to) {
    unlink(to);
    return rename(from, to);
}

static int write_file(const char *path, const char *text) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "we");
    if (!f) return -1;
    int bad = fputs(text, f) < 0;
    if (fclose(f)) bad = 1;
    if (bad || replace_file(tmp, path)) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static char *read_file(const char *path, size_t max) {
    FILE *f = fopen(path, "re");
    if (!f) return NULL;
    char *buf = malloc(max + 1);
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

// ---------------------------------------------------------------------------
// ftl.dat ("PKG\n", big-endian): 16-byte header, 20-byte index entries (name
// hash, flags | path offset, data offset, stored size, real size), path table,
// then the files, zlib-compressed when flag 1 is set. Names compare without case.
typedef struct {
    char *name;
    uint32_t off, size, len;
    bool deflated;
} pkg_entry;
typedef struct {
    FILE *f;
    pkg_entry *e;
    uint32_t n;
    char *paths;
} pkg;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int pkg_open(pkg *k, const char *path) {
    memset(k, 0, sizeof *k);
    k->f = fopen(path, "rbe");
    uint8_t h[16];
    if (!k->f || fread(h, 1, 16, k->f) != 16 || be32(h) != 0x504B470A || h[4] != 0 || h[5] != 16 || h[6] != 0 ||
        h[7] != 20)
        goto bad;
    k->n = be32(h + 8);
    uint32_t psize = be32(h + 12);
    if (k->n > 1000000 || psize > (64u << 20)) goto bad;
    uint8_t *ix = malloc((size_t)k->n * 20);
    k->paths = malloc(psize + 1);
    k->e = calloc(k->n ? k->n : 1, sizeof *k->e);
    if (fread(ix, 20, k->n, k->f) != k->n || fread(k->paths, 1, psize, k->f) != psize) {
        free(ix);
        goto bad;
    }
    k->paths[psize] = 0;
    for (uint32_t i = 0; i < k->n; i++) {
        const uint8_t *x = ix + i * 20;
        uint32_t fp = be32(x + 4);
        k->e[i].name = k->paths + ((fp & 0xffffff) < psize ? (fp & 0xffffff) : psize);
        k->e[i].deflated = (fp >> 24) & 1;
        k->e[i].off = be32(x + 8);
        k->e[i].size = be32(x + 12);
        k->e[i].len = be32(x + 16);
    }
    free(ix);
    return 0;
bad:
    if (k->f) fclose(k->f);
    free(k->e);
    free(k->paths);
    memset(k, 0, sizeof *k);
    return -1;
}
static void pkg_close(pkg *k) {
    if (k->f) fclose(k->f);
    free(k->e);
    free(k->paths);
}
static const pkg_entry *pkg_find(pkg *k, const char *name) {
    for (uint32_t i = 0; i < k->n; i++)
        if (!strcasecmp(k->e[i].name, name)) return &k->e[i];
    return NULL;
}
static char *pkg_read(pkg *k, const pkg_entry *e, size_t *len) {
    uint8_t *raw = malloc(e->size + 1);
    if (fseeko(k->f, e->off, SEEK_SET) || fread(raw, 1, e->size, k->f) != e->size) {
        free(raw);
        return NULL;
    }
    if (!e->deflated) {
        raw[e->size] = 0;
        *len = e->size;
        return (char *)raw;
    }
    uLongf n = e->len;
    char *out = malloc(e->len + 1);
    if (uncompress((Bytef *)out, &n, raw, e->size) != Z_OK) {
        free(raw);
        free(out);
        return NULL;
    }
    free(raw);
    out[n] = 0;
    *len = n;
    return out;
}
// The atlas lists without the images a mod put in as .png, into dir/overlay.*.
// *nimages: how many such images (0: nothing to do, no overlay written).
static int write_overlay(pkg *k, const char *dir, int *nimages) {
    const char **images = calloc(k->n ? k->n : 1, sizeof *images);
    uint32_t nimg = 0;
    for (uint32_t i = 0; i < k->n; i++)
        if (prefix_ci(k->e[i].name, "img/") && ends_with(k->e[i].name, ".png")) images[nimg++] = k->e[i].name;
    *nimages = (int)nimg;
    if (!nimg) {
        free(images);
        return 0;
    }
    char p[900];
    path_in(p, sizeof p, dir, "overlay.dat.tmp");
    FILE *dat = fopen(p, "wbe");
    path_in(p, sizeof p, dir, "overlay.idx.tmp");
    FILE *idx = fopen(p, "we");
    int rc = dat && idx ? 0 : -1;
    unsigned long long off = 0;
    for (uint32_t i = 0; !rc && i < k->n; i++) {
        const pkg_entry *a = &k->e[i];
        if (!prefix_ci(a->name, "atlas/") || !ends_with(a->name, ".txt")) continue;
        size_t len;
        char *text = pkg_read(k, a, &len);
        if (!text) continue;
        char *out = malloc(len + 1);
        size_t on = 0;
        bool changed = false;
        char *line = text;
        for (;;) {
            char *nl = strchr(line, '\n');
            size_t ll = nl ? (size_t)(nl - line) : strlen(line);
            // first word of the line: the image's name
            const char *w = line;
            while (w < line + ll && isspace((unsigned char)*w)) w++;
            const char *we = w;
            while (we < line + ll && !isspace((unsigned char)*we)) we++;
            bool drop = false;
            if (we > w)
                for (uint32_t m = 0; m < nimg && !drop; m++)
                    drop = strlen(images[m]) == (size_t)(we - w) && !strncasecmp(images[m], w, (size_t)(we - w));
            if (drop) {
                changed = true;
            } else {
                if (on) out[on++] = '\n';
                memcpy(out + on, line, ll);
                on += ll;
            }
            if (!nl) break;
            line = nl + 1;
        }
        if (changed) {
            uLongf cl = compressBound(on);
            uint8_t *z = malloc(cl);
            if (compress2(z, &cl, (Bytef *)out, on, 6) != Z_OK || fwrite(z, 1, cl, dat) != cl) rc = -1;
            fprintf(idx, "%llu\t%lu\t%zu\t0\t%s\n", off, (unsigned long)cl, on, a->name);
            off += cl;
            free(z);
        }
        free(out);
        free(text);
    }
    free(images);
    if (dat && fclose(dat)) rc = -1;
    if (idx && fclose(idx)) rc = -1;
    char from[900], to[900];
    for (int i = 0; i < 2 && !rc; i++) {
        const char *name = i ? "overlay.idx" : "overlay.dat";  // the index last: it marks a complete overlay
        snprintf(from, sizeof from, "%s/%s.tmp", dir, name);
        path_in(to, sizeof to, dir, name);
        if (replace_file(from, to)) rc = -1;
    }
    return rc;
}

// ftl.dat as it is now: changes whenever ftlman writes it.
static void dat_stamp(const char *dat, char *out, size_t n) {
    struct stat st;
    if (stat(dat, &st)) {
        snprintf(out, n, "none\n");
        return;
    }
    snprintf(out, n, "%lld %lld.%09ld %llu\n", (long long)st.st_size, (long long)st.st_mtim.tv_sec,
             st.st_mtim.tv_nsec, (unsigned long long)st.st_ino);
}

static char g_index[800];
static bool g_modded, g_hyperspace, g_changed, g_pc_dat;

void mods_prepare(void) {
    char dat[700], dir[700], p[900], stamp[128];
    path_in(dat, sizeof dat, g_paths.game_dir, "ftl.dat");
    path_in(dir, sizeof dir, g_paths.cache_dir, "mods");
    mkdirs(dir);
    dat_stamp(dat, stamp, sizeof stamp);
    // The stamp of the ftl.dat the last start used (state: it survives a cleared cache).
    char last[700];
    path_in(last, sizeof last, g_paths.state_dir, "ftl.dat.stamp");
    char *was = read_file(last, 127);
    g_changed = was && strcmp(was, stamp);
    if (!was || g_changed) write_file(last, stamp);
    free(was);

    path_in(p, sizeof p, dir, "stamp");
    char *made = read_file(p, 127);
    char info[900];
    path_in(info, sizeof info, dir, "info");
    if (made && !strcmp(made, stamp)) {  // made for this ftl.dat already
        free(made);
        char *i = read_file(info, 63);
        int nimg = 0, hs = 0;
        if (i && sscanf(i, "%d %d", &nimg, &hs) == 2) {
            free(i);
            g_hyperspace = hs;
            g_modded = nimg > 0 || hs;
            path_in(g_index, sizeof g_index, dir, "overlay.idx");
            if (nimg == 0 || access(g_index, R_OK)) g_index[0] = 0;
            return;
        }
        free(i);
    } else {
        free(made);
    }

    unlink(p);
    char old[900];
    path_in(old, sizeof old, dir, "overlay.idx");
    unlink(old);
    path_in(old, sizeof old, dir, "overlay.dat");
    unlink(old);
    pkg k;
    if (pkg_open(&k, dat)) {
        LOGW("mods: %s isn't readable as ftl.dat", dat);
        return;
    }
    // The iPad game's ftl.dat has texture atlases; the PC version's (Steam, GOG, or one
    // patched there from it) has none and can't run here
    if (!pkg_find(&k, "atlas/achievements.tex") && !pkg_find(&k, "atlas/people/engi.txt")) {
        pkg_close(&k);
        g_pc_dat = true;
        LOGE("mods: %s is the PC version's ftl.dat (no iPad atlases)", dat);
        return;
    }
    int nimg = 0;
    g_hyperspace = pkg_find(&k, "data/hyperspace.xml") != NULL;
    int rc = write_overlay(&k, dir, &nimg);
    pkg_close(&k);
    if (rc) {
        LOGE("mods: couldn't write the image overlay into %s; mod images may not show", dir);
        return;
    }
    g_modded = nimg > 0 || g_hyperspace;
    if (nimg) path_in(g_index, sizeof g_index, dir, "overlay.idx");
    char line[64];
    snprintf(line, sizeof line, "%d %d\n", nimg, g_hyperspace);
    if (write_file(info, line) == 0) write_file(p, stamp);
    if (g_modded) LOGI("mods: ftl.dat is modded (%d mod images%s)", nimg, g_hyperspace ? ", Hyperspace" : "");
}

const char *mods_active_index(void) { return g_index[0] ? g_index : NULL; }
bool mods_hyperspace_applied(void) { return g_hyperspace; }
bool mods_take_changed(void) { return g_changed; }
bool mods_dat_is_pc(void) { return g_pc_dat; }
