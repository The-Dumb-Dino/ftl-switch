// execmem for POSIX hosts (Linux): one anonymous mapping, rw == rx.
#include "execmem.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "port_log.h"

size_t execmem_page_size(void) {
    static size_t ps;
    if (!ps) {
        long v = sysconf(_SC_PAGESIZE);
        ps = v > 0 ? (size_t)v : 4096;
        // Testing: FTL_PAGESIZE=65536 behaves as if pages were that large (a
        // power-of-two multiple of the real size), to check the 64K path here.
        const char *t = getenv("FTL_PAGESIZE");
        size_t want = t ? (size_t)strtoul(t, NULL, 0) : 0;
        if (want > ps && !(want & (want - 1))) ps = want;
    }
    return ps;
}

int execmem_alloc(execmem *m, size_t size, size_t align) {
    size_t ps = execmem_page_size();
    if (align < ps) align = ps;
    size = (size + ps - 1) & ~(ps - 1);
    size_t reserve = size + align;
    uint8_t *raw = mmap(NULL, reserve, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) {
        LOGE("execmem: mmap %zu failed: %s", reserve, strerror(errno));
        return -1;
    }
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1));
    if (base > raw) munmap(raw, (size_t)(base - raw));
    if (raw + reserve > base + size) munmap(base + size, (size_t)(raw + reserve - (base + size)));
    m->rw = m->rx = base;
    m->size = size;
    return 0;
}

int execmem_protect(execmem *m, size_t off, size_t len, int prot) {
    int p = 0;
    if (prot & EXECMEM_R) p |= PROT_READ;
    if (prot & EXECMEM_W) p |= PROT_WRITE;
    if (prot & EXECMEM_X) p |= PROT_EXEC;
    if (prot & EXECMEM_X) __builtin___clear_cache((char *)m->rx + off, (char *)m->rx + off + len);
    if (mprotect(m->rx + off, len, p) != 0) {
        LOGW("execmem: mprotect +%#zx len %#zx prot %d failed: %s", off, len, prot, strerror(errno));
        return -1;
    }
    return 0;
}

void execmem_free(execmem *m) {
    if (m->rx) munmap(m->rx, m->size);
    m->rw = m->rx = NULL;
    m->size = 0;
}
