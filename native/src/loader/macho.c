// arm64 Mach-O loader: maps an iOS executable into anonymous memory, applies
// rebases and binds from LC_DYLD_INFO(_ONLY), and runs its initializers.
#include "macho.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "port_log.h"

#define MH_MAGIC_64 0xfeedfacfu
#define FAT_MAGIC 0xcafebabeu
#define CPU_TYPE_ARM64 0x0100000c

#define LC_SEGMENT_64 0x19
#define LC_SYMTAB 0x2
#define LC_LOAD_DYLIB 0xc
#define LC_LOAD_WEAK_DYLIB 0x80000018
#define LC_REEXPORT_DYLIB 0x8000001f
#define LC_DYLD_INFO 0x22
#define LC_DYLD_INFO_ONLY 0x80000022
#define LC_MAIN 0x80000028
#define LC_FUNCTION_STARTS 0x26
#define LC_DYLD_CHAINED_FIXUPS 0x80000034

#define REBASE_OPCODE_MASK 0xF0
#define REBASE_IMMEDIATE_MASK 0x0F
#define BIND_OPCODE_MASK 0xF0
#define BIND_IMMEDIATE_MASK 0x0F

#define S_MOD_INIT_FUNC_POINTERS 0x9

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static uint64_t uleb(const uint8_t **pp, const uint8_t *end) {
    uint64_t r = 0;
    int s = 0;
    const uint8_t *p = *pp;
    while (p < end) {
        uint8_t b = *p++;
        if (s < 64) r |= (uint64_t)(b & 0x7f) << s;
        s += 7;
        if (!(b & 0x80)) break;
    }
    *pp = p;
    return r;
}

static int64_t sleb(const uint8_t **pp, const uint8_t *end) {
    int64_t r = 0;
    int s = 0;
    const uint8_t *p = *pp;
    uint8_t b = 0;
    while (p < end) {
        b = *p++;
        if (s < 64) r |= (int64_t)(b & 0x7f) << s;
        s += 7;
        if (!(b & 0x80)) break;
    }
    if (s < 64 && (b & 0x40)) r |= -((int64_t)1 << s);
    *pp = p;
    return r;
}

static void *read_file(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    uint8_t *buf = malloc((size_t)st.st_size);
    size_t got = 0;
    while (buf && got < (size_t)st.st_size) {
        ssize_t n = read(fd, buf + got, (size_t)st.st_size - got);
        if (n <= 0) { free(buf); buf = NULL; break; }
        got += (size_t)n;
    }
    close(fd);
    *size = got;
    return buf;
}

mo_section *mo_find_section(mo_image *img, const char *seg, const char *sect) {
    for (int i = 0; i < img->nsects; i++)
        if (!strcmp(img->sects[i].sectname, sect) && (!seg || !strcmp(img->sects[i].segname, seg)))
            return &img->sects[i];
    return NULL;
}

int mo_load(const char *path, mo_image *img) {
    memset(img, 0, sizeof(*img));
    strncpy(img->path, path, sizeof(img->path) - 1);
    size_t fsize;
    uint8_t *whole = read_file(path, &fsize);
    if (!whole) { LOGE("macho: cannot read %s: %s", path, strerror(errno)); return -1; }

    // Pick the arm64 slice of a universal binary.
    uint8_t *mh = whole;
    size_t mh_size = fsize;
    if (be32(whole) == FAT_MAGIC) {
        uint32_t n = be32(whole + 4);
        mh = NULL;
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *fa = whole + 8 + i * 20;
            if (be32(fa) == CPU_TYPE_ARM64) {
                uint32_t off = be32(fa + 8), sz = be32(fa + 12);
                if ((uint64_t)off + sz > fsize) break;
                mh = whole + off;
                mh_size = sz;
            }
        }
        if (!mh) { LOGE("macho: no arm64 slice in %s", path); free(whole); return -1; }
    }
    if (rd32(mh) != MH_MAGIC_64 || rd32(mh + 4) != CPU_TYPE_ARM64) {
        LOGE("macho: %s is not an arm64 Mach-O", path);
        free(whole);
        return -1;
    }
    img->file = malloc(mh_size);
    memcpy(img->file, mh, mh_size);
    img->file_size = mh_size;
    free(whole);
    const uint8_t *f = img->file;

    uint32_t ncmds = rd32(f + 16);
    const uint8_t *lc = f + 32;
    uint64_t lo = UINT64_MAX, hi = 0, entryoff = 0;
    int have_main = 0;
    for (uint32_t i = 0; i < ncmds; i++) {
        uint32_t cmd = rd32(lc), cmdsize = rd32(lc + 4);
        switch (cmd) {
        case LC_SEGMENT_64: {
            mo_segment *s = &img->segs[img->nsegs++];
            memcpy(s->name, lc + 8, 16);
            s->vmaddr = rd64(lc + 24); s->vmsize = rd64(lc + 32);
            s->fileoff = rd64(lc + 40); s->filesize = rd64(lc + 48);
            s->initprot = (int)rd32(lc + 60);
            uint32_t nsects = rd32(lc + 64);
            if (strcmp(s->name, "__PAGEZERO") != 0) {
                if (s->vmaddr < lo) lo = s->vmaddr;
                if (s->vmaddr + s->vmsize > hi) hi = s->vmaddr + s->vmsize;
            }
            const uint8_t *sp = lc + 72;
            for (uint32_t j = 0; j < nsects && img->nsects < 64; j++, sp += 80) {
                mo_section *sec = &img->sects[img->nsects++];
                memcpy(sec->sectname, sp, 16);
                memcpy(sec->segname, sp + 16, 16);
                sec->addr = rd64(sp + 32);
                sec->size = rd64(sp + 40);
                sec->flags = rd32(sp + 64);
            }
            break;
        }
        case LC_LOAD_DYLIB:
        case LC_LOAD_WEAK_DYLIB:
        case LC_REEXPORT_DYLIB:
            if (img->ndylibs < 32) {
                uint32_t off = rd32(lc + 8);
                strncpy(img->dylibs[img->ndylibs++], (const char *)lc + off, 159);
            }
            break;
        case LC_DYLD_INFO:
        case LC_DYLD_INFO_ONLY:
            img->rebase_off = rd32(lc + 8); img->rebase_size = rd32(lc + 12);
            img->bind_off = rd32(lc + 16); img->bind_size = rd32(lc + 20);
            img->weak_off = rd32(lc + 24); img->weak_size = rd32(lc + 28);
            img->lazy_off = rd32(lc + 32); img->lazy_size = rd32(lc + 36);
            img->export_off = rd32(lc + 40); img->export_size = rd32(lc + 44);
            break;
        case LC_SYMTAB:
            img->symoff = rd32(lc + 8); img->nsyms = rd32(lc + 12);
            img->stroff = rd32(lc + 16); img->strsize = rd32(lc + 20);
            break;
        case LC_FUNCTION_STARTS:
            img->fstarts_off = rd32(lc + 8); img->fstarts_size = rd32(lc + 12);
            break;
        case LC_MAIN:
            entryoff = rd64(lc + 8);
            have_main = 1;
            break;
        case LC_DYLD_CHAINED_FIXUPS:
            LOGE("macho: chained fixups are not supported");
            return -1;
        }
        lc += cmdsize;
    }
    if (lo == UINT64_MAX) { LOGE("macho: no segments"); return -1; }

    // Reserve one region for all segments, aligned to 16K (iOS pages) or the host
    // page if that is larger. Segments are copied in, never mapped from the file,
    // so any host page size works (Mach-O segments are only 16K aligned).
    size_t ps = execmem_page_size();
    const size_t align = ps > 0x4000 ? ps : 0x4000;
    size_t span = (size_t)(hi - lo);
    // Extra room after the image, on its own pages, for "near" trampolines
    // reachable by a 4-byte b, and for stubs of unresolved imports.
    size_t tramp_off = (span + ps - 1) & ~(ps - 1);
    size_t tramp_size = (0x40000 + ps - 1) & ~(ps - 1);
    if (execmem_alloc(&img->mem, tramp_off + tramp_size, align) != 0) return -1;
    uint8_t *base = img->mem.rx, *wbase = img->mem.rw;
    img->near_tramp = base + tramp_off;
    img->near_tramp_size = tramp_size;

    img->base = base;
    img->vmaddr_min = lo;
    img->slide = (intptr_t)((uintptr_t)base - lo);
    img->map_size = span;

    for (int i = 0; i < img->nsegs; i++) {
        mo_segment *s = &img->segs[i];
        if (!strcmp(s->name, "__PAGEZERO")) continue;
        if (s->fileoff + s->filesize > img->file_size) { LOGE("macho: segment %s out of file", s->name); return -1; }
        memcpy(wbase + (s->vmaddr - lo), f + s->fileoff, (size_t)s->filesize);
    }
    for (int i = 0; i < img->nsects; i++) img->sects[i].addr += (uint64_t)img->slide;
    if (have_main) {
        // entryoff is relative to the start of __TEXT (which maps file offset 0)
        for (int i = 0; i < img->nsegs; i++)
            if (!strcmp(img->segs[i].name, "__TEXT"))
                img->entry = img->segs[i].vmaddr + entryoff + (uint64_t)img->slide;
    }

    // Rebases.
    const uint8_t *p = f + img->rebase_off, *end = p + img->rebase_size;
    uint64_t seg_addr = 0, off = 0;
    int count = 0;
#define REBASE_AT()                                                         \
    do {                                                                    \
        uint64_t *loc = (uint64_t *)(wbase + (seg_addr + off - lo));         \
        *loc += (uint64_t)img->slide;                                       \
        count++;                                                            \
    } while (0)
    while (p < end) {
        uint8_t b = *p++, op = b & REBASE_OPCODE_MASK, imm = b & REBASE_IMMEDIATE_MASK;
        switch (op) {
        case 0x00: p = end; break;  // DONE
        case 0x10: break;           // SET_TYPE_IMM (pointer)
        case 0x20: seg_addr = img->segs[imm].vmaddr; off = uleb(&p, end); break;
        case 0x30: off += uleb(&p, end); break;
        case 0x40: off += imm * 8; break;
        case 0x50: for (int k = 0; k < imm; k++) { REBASE_AT(); off += 8; } break;
        case 0x60: { uint64_t n = uleb(&p, end); for (uint64_t k = 0; k < n; k++) { REBASE_AT(); off += 8; } break; }
        case 0x70: REBASE_AT(); off += 8 + uleb(&p, end); break;
        case 0x80: {
            uint64_t n = uleb(&p, end), skip = uleb(&p, end);
            for (uint64_t k = 0; k < n; k++) { REBASE_AT(); off += 8 + skip; }
            break;
        }
        default: LOGE("macho: bad rebase opcode %#x", b); return -1;
        }
    }
#undef REBASE_AT
    LOGI("macho: loaded %s at %p (slide %#lx), %d rebases", path, base, (long)img->slide, count);
    return 0;
}

// --- unresolved import traps ---------------------------------------------------

static void (*g_trap_handler)(const char *);
void mo_set_trap_handler(void (*fn)(const char *name)) { g_trap_handler = fn; }

void mo_trap_c(const char *name) {
    if (g_trap_handler) g_trap_handler(name);
    LOGE("FATAL: call to unimplemented import %s", name);
    abort();
}

extern void mo_trap_entry(void);
__asm__(
    ".text\n"
    ".globl mo_trap_entry\n"
    ".p2align 2\n"
    "mo_trap_entry:\n"
    "  mov x0, x16\n"
    "  b mo_trap_c\n");

void *mo_tramp_alloc(mo_image *img, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!img->near_tramp || img->near_tramp_used + n > img->near_tramp_size) return NULL;
    void *p = img->near_tramp + img->near_tramp_used;
    img->near_tramp_used += n;
    return p;
}

static void *make_trap_stub(mo_image *img, const char *name) {
    uint32_t *x = mo_tramp_alloc(img, 32);
    if (!x) port_fatal("no room for import stubs");
    uint32_t *s = execmem_to_rw(&img->mem, x);
    s[0] = 0x58000090;  // ldr x16, #16
    s[1] = 0x580000B1;  // ldr x17, #20
    s[2] = 0xD61F0220;  // br x17
    s[3] = 0xD503201F;  // nop
    char *dup = strdup(name);
    memcpy(&s[4], &dup, 8);
    void *h = (void *)mo_trap_entry;
    memcpy(&s[6], &h, 8);
    return x;  // made executable by mo_protect
}

static uint64_t g_zero_data[512];

// --- binds -----------------------------------------------------------------------

static int do_binds(mo_image *img, const uint8_t *p, const uint8_t *end, int lazy, mo_resolver resolve, int *nbound,
                    int *nmissing) {
    uint64_t seg_addr = 0, off = 0;
    int64_t addend = 0;
    int ordinal = 0;
    const char *sym = NULL;
    void *target = NULL;
    int resolved = 0;
    int weak_import = 0;
#define BIND_AT()                                                                         \
    do {                                                                                  \
        if (!resolved) {                                                                  \
            const char *lib = (ordinal > 0 && ordinal <= img->ndylibs) ? img->dylibs[ordinal - 1] : NULL; \
            target = resolve(sym, lib);                                                   \
            if (!target) {                                                                \
                if (!weak_import) (*nmissing)++;                                          \
                LOGW("macho: unresolved %s%s (%s)", sym, weak_import ? " [weak]" : "", lib ? lib : "?"); \
                int looks_data = !strncmp(sym, "_OBJC_", 6) || !strncmp(sym, "_k", 2) ||        \
                                 strstr(sym, "Notification") || strstr(sym, "Key");             \
                target = looks_data ? (void *)g_zero_data : make_trap_stub(img, sym);             \
            }                                                                             \
            resolved = 1;                                                                 \
        }                                                                                 \
        uint64_t *loc = (uint64_t *)(img->mem.rw + (seg_addr + off - img->vmaddr_min));     \
        *loc = (uint64_t)(uintptr_t)target + (uint64_t)addend;                           \
        (*nbound)++;                                                                      \
    } while (0)

    while (p < end) {
        uint8_t b = *p++, op = b & BIND_OPCODE_MASK, imm = b & BIND_IMMEDIATE_MASK;
        switch (op) {
        case 0x00:  // DONE (lazy binds use DONE as a separator between entries)
            if (!lazy) p = end;
            break;
        case 0x10: ordinal = imm; break;
        case 0x20: ordinal = (int)uleb(&p, end); break;
        case 0x30: ordinal = imm ? (int)(int8_t)(0xF0 | imm) : 0; break;
        case 0x40:
            weak_import = imm & 1;
            sym = (const char *)p;
            p += strlen(sym) + 1;
            resolved = 0;
            break;
        case 0x50: break;  // SET_TYPE_IMM
        case 0x60: addend = sleb(&p, end); break;
        case 0x70: seg_addr = img->segs[imm].vmaddr; off = uleb(&p, end); break;
        case 0x80: off += uleb(&p, end); break;
        case 0x90: BIND_AT(); off += 8; break;
        case 0xA0: BIND_AT(); off += 8 + uleb(&p, end); break;
        case 0xB0: BIND_AT(); off += 8 + (uint64_t)imm * 8; break;
        case 0xC0: {
            uint64_t n = uleb(&p, end), skip = uleb(&p, end);
            for (uint64_t k = 0; k < n; k++) { BIND_AT(); off += 8 + skip; }
            break;
        }
        default: LOGE("macho: bad bind opcode %#x", b); return -1;
        }
    }
#undef BIND_AT
    return 0;
}

int mo_bind(mo_image *img, mo_resolver resolve) {
    int nbound = 0, nmissing = 0;
    const uint8_t *f = img->file;
    if (do_binds(img, f + img->bind_off, f + img->bind_off + img->bind_size, 0, resolve, &nbound, &nmissing)) return -1;
    if (do_binds(img, f + img->lazy_off, f + img->lazy_off + img->lazy_size, 1, resolve, &nbound, &nmissing)) return -1;
    if (img->weak_size &&
        do_binds(img, f + img->weak_off, f + img->weak_off + img->weak_size, 0, resolve, &nbound, &nmissing))
        return -1;
    LOGI("macho: %d binds, %d unresolved", nbound, nmissing);
    return 0;
}

void mo_protect(mo_image *img) {
    // Protection is per host page. With pages larger than 16K (64K kernels) a page
    // can hold the end of one segment and the start of the next, e.g. __TEXT and
    // __DATA; it then gets both segments' permissions.
    size_t ps = execmem_page_size(), npages = img->mem.size / ps;
    uint8_t *prot = calloc(npages, 1);
    int mixed = 0;
    for (int i = 0; i < img->nsegs; i++) {
        mo_segment *s = &img->segs[i];
        if (!strcmp(s->name, "__PAGEZERO") || !s->vmsize) continue;
        size_t a = (size_t)(s->vmaddr - img->vmaddr_min), b = a + (size_t)s->vmsize;
        for (size_t pg = a / ps; pg <= (b - 1) / ps && pg < npages; pg++) {
            if (prot[pg] && prot[pg] != (s->initprot & 7)) mixed = 1;
            prot[pg] |= (uint8_t)(s->initprot & 7);
        }
    }
    if (img->near_tramp) {
        size_t a = (size_t)(img->near_tramp - img->mem.rx);
        for (size_t pg = a / ps; pg < (a + img->near_tramp_size) / ps; pg++) prot[pg] = EXECMEM_R | EXECMEM_X;
    }
    if (mixed) LOGI("macho: %zuK pages: segments share pages, those get the union of their permissions", ps / 1024);
    for (size_t i = 0; i < npages;) {
        size_t j = i;
        while (j < npages && prot[j] == prot[i]) j++;
        execmem_protect(&img->mem, i * ps, (j - i) * ps, prot[i]);
        i = j;
    }
    free(prot);
}

struct ProgramVars {
    const void *mh;
    int *NXArgcPtr;
    const char ***NXArgvPtr;
    const char ***environPtr;
    const char **__prognamePtr;
};

void mo_run_initializers(mo_image *img, int argc, const char **argv, const char **envp, const char **apple) {
    static int s_argc;
    static const char **s_argv, **s_envp;
    static const char *s_progname = "FTL";
    s_argc = argc; s_argv = argv; s_envp = envp;
    struct ProgramVars vars = {img->base, &s_argc, &s_argv, &s_envp, &s_progname};
    for (int i = 0; i < img->nsects; i++) {
        mo_section *s = &img->sects[i];
        if ((s->flags & 0xff) != S_MOD_INIT_FUNC_POINTERS) continue;
        uint64_t *fns = (uint64_t *)(uintptr_t)s->addr;
        size_t n = (size_t)(s->size / 8);
        LOGI("macho: running %zu initializers", n);
        for (size_t k = 0; k < n; k++) {
            void (*fn)(int, const char **, const char **, const char **, const struct ProgramVars *) =
                (void (*)(int, const char **, const char **, const char **, const struct ProgramVars *))(uintptr_t)fns[k];
            LOGV("macho:   init %zu at %p (link %#lx)", k, (void *)fn, (unsigned long)((uintptr_t)fn - (uintptr_t)img->slide));
            fn(argc, argv, envp, apple, &vars);
        }
    }
}
