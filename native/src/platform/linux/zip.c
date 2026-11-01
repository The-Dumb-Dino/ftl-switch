// A small zip reader (central directory, ZIP64, stored and deflated entries),
// enough for .ipa files and mods.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "linux_app.h"

struct zip_file {
    FILE *f;
    zip_entry *e;
    int n;
    char *names;
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

static int readat(FILE *f, uint64_t off, void *buf, size_t n) {
    return fseeko(f, (off_t)off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n ? 0 : -1;
}

zip_file *zip_open(const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "rbe");
    if (!f) {
        snprintf(err, errlen, "Can't open %s", path);
        return NULL;
    }
    fseeko(f, 0, SEEK_END);
    uint64_t size = (uint64_t)ftello(f);
    size_t tail = size < 65557 ? (size_t)size : 65557;
    uint8_t *t = malloc(tail);
    zip_file *z = NULL;
    if (!t || readat(f, size - tail, t, tail)) goto bad;
    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; i--)
        if (le32(t + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) goto bad;
    uint64_t count = le16(t + eocd + 10), cdsize = le32(t + eocd + 12), cdoff = le32(t + eocd + 16);
    if (eocd >= 20 && le32(t + eocd - 20) == 0x07064b50) {  // ZIP64 end of central directory locator
        uint8_t z64[56];
        if (readat(f, le64(t + eocd - 20 + 8), z64, sizeof z64) || le32(z64) != 0x06064b50) goto bad;
        count = le64(z64 + 32);
        cdsize = le64(z64 + 40);
        cdoff = le64(z64 + 48);
    }
    if (cdoff + cdsize > size || count > 1000000) goto bad;
    uint8_t *cd = malloc(cdsize ? cdsize : 1);
    if (!cd || readat(f, cdoff, cd, cdsize)) {
        free(cd);
        goto bad;
    }
    z = calloc(1, sizeof *z);
    z->f = f;
    z->e = calloc(count ? count : 1, sizeof *z->e);
    z->names = malloc(cdsize + count + 1);
    size_t np = 0;
    uint64_t p = 0;
    for (uint64_t i = 0; i < count; i++) {
        if (p + 46 > cdsize || le32(cd + p) != 0x02014b50) break;
        const uint8_t *h = cd + p;
        uint16_t nl = le16(h + 28), xl = le16(h + 30), cl = le16(h + 32);
        if (p + 46 + nl + xl + cl > cdsize) break;
        zip_entry *e = &z->e[z->n++];
        e->method = le16(h + 10);
        e->crc = le32(h + 16);
        e->csize = le32(h + 20);
        e->size = le32(h + 24);
        e->offset = le32(h + 42);
        // ZIP64 extra field: the 0xffffffff fields, in order
        for (const uint8_t *x = h + 46 + nl, *xe = x + xl; x + 4 <= xe;) {
            uint16_t id = le16(x), len = le16(x + 2);
            const uint8_t *d = x + 4, *de = d + len;
            if (id == 1) {
                if (e->size == 0xffffffffu && d + 8 <= de) e->size = le64(d), d += 8;
                if (e->csize == 0xffffffffu && d + 8 <= de) e->csize = le64(d), d += 8;
                if (e->offset == 0xffffffffu && d + 8 <= de) e->offset = le64(d), d += 8;
            }
            x = de;
        }
        memcpy(z->names + np, h + 46, nl);
        z->names[np + nl] = 0;
        e->name = z->names + np;
        np += nl + 1u;
        p += 46u + nl + xl + cl;
    }
    free(cd);
    free(t);
    return z;
bad:
    free(t);
    fclose(f);
    snprintf(err, errlen, "%s isn't a zip file (or it's damaged)", path);
    return NULL;
}

int zip_count(zip_file *z) { return z->n; }
const zip_entry *zip_at(zip_file *z, int i) { return i >= 0 && i < z->n ? &z->e[i] : NULL; }

int zip_extract(zip_file *z, const zip_entry *e, int (*fn)(void *, const void *, size_t), void *ud) {
    uint8_t lh[30];
    if (readat(z->f, e->offset, lh, sizeof lh) || le32(lh) != 0x04034b50) return -1;
    uint64_t data = e->offset + 30 + le16(lh + 26) + le16(lh + 28);
    if (fseeko(z->f, (off_t)data, SEEK_SET)) return -1;
    enum { CHUNK = 1 << 16 };
    uint8_t *in = malloc(CHUNK), *out = malloc(CHUNK);
    uLong crc = crc32(0, NULL, 0);
    uint64_t left = e->csize, produced = 0;
    int rc = -1;
    if (e->method == 0) {
        while (left) {
            size_t n = left < CHUNK ? (size_t)left : CHUNK;
            if (fread(in, 1, n, z->f) != n) goto done;
            crc = crc32(crc, in, (uInt)n);
            if (fn(ud, in, n)) goto done;
            left -= n;
            produced += n;
        }
    } else if (e->method == 8) {
        z_stream s;
        memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) goto done;
        int zr = Z_OK;
        while (zr != Z_STREAM_END) {
            if (!s.avail_in) {
                size_t n = left < CHUNK ? (size_t)left : CHUNK;
                if (!n || fread(in, 1, n, z->f) != n) break;
                left -= n;
                s.next_in = in;
                s.avail_in = (uInt)n;
            }
            s.next_out = out;
            s.avail_out = CHUNK;
            zr = inflate(&s, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) break;
            size_t got = CHUNK - s.avail_out;
            crc = crc32(crc, out, (uInt)got);
            produced += got;
            if (got && fn(ud, out, got)) break;
        }
        inflateEnd(&s);
        if (zr != Z_STREAM_END) goto done;
    } else {
        goto done;
    }
    rc = produced == e->size && crc == e->crc ? 0 : -1;
done:
    free(in);
    free(out);
    return rc;
}

static int to_file(void *ud, const void *buf, size_t n) { return fwrite(buf, 1, n, (FILE *)ud) != n; }

int zip_extract_to(zip_file *z, const zip_entry *e, const char *path) {
    FILE *f = fopen(path, "wbe");
    if (!f) return -1;
    int rc = zip_extract(z, e, to_file, f);
    if (fclose(f) != 0) rc = -1;
    if (rc) remove(path);
    return rc;
}

void zip_close(zip_file *z) {
    if (!z) return;
    fclose(z->f);
    free(z->e);
    free(z->names);
    free(z);
}
