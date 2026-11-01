// execmem for the Switch (libnx). Horizon never lets a page be writable and
// executable at once, code only runs from memory the kernel mapped as code,
// and a range's permission can't be changed back and forth. So:
//   1. the image is built in ordinary heap memory (the writable view), while its
//      address in the code region (where it runs) is only reserved; the loader
//      writes and reads through the writable view until mo_protect;
//   2. at mo_protect each range is mapped at its address once:
//      - code (RX): as a code memory object, with a second, writable view
//        elsewhere (svcCreateCodeMemory, MapOwner RW there, the code copied in
//        through it, MapSlave RX here), so code stays patchable while it runs
//        (Hyperspace's hooks);
//      - everything else: with svcMapProcessCodeMemory and then RW once, as the
//        system's module loader (ro) maps .nro data. Read-only data stays
//        writable too (Hyperspace patches vtables in it).
// The iOS binary's segments are 16K aligned on the Switch's 4K pages, so code
// and data never share a page. Both calls take a real handle to the own
// process: Atmosphère's homebrew loader hands one over; without it (other
// loaders, emulators) one is made the way that loader makes it.
#include "../../loader/execmem.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "port_log.h"

#define SWITCH_PAGE 0x1000

static uint8_t *g_src;  // the heap memory behind the mapping
// A real handle to this process, as Atmosphère's hbloader gets one: send the
// "current process" pseudo-handle over an IPC session to ourselves; the kernel
// turns it into a real handle on the way.
static Handle g_self = INVALID_HANDLE;

static void receive_handle(void *arg) {
    Handle session = (Handle)(uintptr_t)arg;
    void *tls = armGetTls();
    hipcMakeRequestInline(tls);
    s32 idx = 0;
    if (R_SUCCEEDED(svcReplyAndReceive(&idx, &session, 1, INVALID_HANDLE, UINT64_MAX))) {
        HipcParsedRequest r = hipcParseRequest(tls);
        if (r.meta.num_copy_handles == 1) g_self = r.data.copy_handles[0];
    }
    svcCloseHandle(session);
}

static Handle self_handle(void) {
    if (g_self != INVALID_HANDLE) return g_self;
    g_self = envGetOwnProcessHandle();
    if (g_self != INVALID_HANDLE) return g_self;
    Handle server, client;
    Thread t;
    if (R_FAILED(svcCreateSession(&server, &client, 0, 0))) return CUR_PROCESS_HANDLE;
    if (R_FAILED(threadCreate(&t, receive_handle, (void *)(uintptr_t)server, NULL, 0x1000, 0x20, -2))) {
        svcCloseHandle(server);
        svcCloseHandle(client);
        return CUR_PROCESS_HANDLE;
    }
    threadStart(&t);
    hipcMakeRequestInline(armGetTls(), .num_copy_handles = 1).copy_handles[0] = CUR_PROCESS_HANDLE;
    svcSendSyncRequest(client);
    svcCloseHandle(client);
    threadWaitForExit(&t);
    threadClose(&t);
    LOGI("execmem: %s a handle to this process", g_self != INVALID_HANDLE ? "made" : "couldn't make");
    return g_self != INVALID_HANDLE ? g_self : CUR_PROCESS_HANDLE;
}
static VirtmemReservation *g_resv;

size_t execmem_page_size(void) { return SWITCH_PAGE; }

static Handle g_proc;

// The ranges mapped so far (mo_protect maps a few: text, data, link-edit, trampolines).
#define MAX_RUNS 16
static struct run {
    size_t off, len;
    bool code;
    uint8_t *rw;  // code: the writable view; data: NULL (the mapping is writable itself)
    Handle cm;    // code: its code memory object
} g_runs[MAX_RUNS];
static int g_nruns;
static uint8_t *g_rx;  // where the image runs (reserved, then mapped)
static size_t g_size;

int execmem_alloc(execmem *m, size_t size, size_t align) {
    if (align < SWITCH_PAGE) align = SWITCH_PAGE;
    size = (size + SWITCH_PAGE - 1) & ~(size_t)(SWITCH_PAGE - 1);
    uint8_t *src = memalign(SWITCH_PAGE, size);
    if (!src) {
        LOGE("execmem: no memory for %zu MB", size >> 20);
        return -1;
    }
    memset(src, 0, size);
    virtmemLock();
    uint8_t *found = virtmemFindCodeMemory(size + align, 0);
    uint8_t *dst = found ? (uint8_t *)(((uintptr_t)found + align - 1) & ~(uintptr_t)(align - 1)) : NULL;
    if (dst) g_resv = virtmemAddReservation(dst, size);
    virtmemUnlock();
    if (!dst || !g_resv) {
        LOGE("execmem: no room in the code region for %zu MB", size >> 20);
        free(src);
        return -1;
    }
    g_src = src;
    g_rx = dst;
    g_size = size;
    m->rw = src;
    m->rx = dst;
    m->size = size;
    LOGI("execmem: %zu MB, built at %p, to run at %p", size >> 20, src, dst);
    return 0;
}

void *execmem_to_rw(const execmem *m, const void *rx) {
    size_t off = (size_t)((const uint8_t *)rx - g_rx);
    for (int i = 0; i < g_nruns; i++)
        if (off >= g_runs[i].off && off < g_runs[i].off + g_runs[i].len)
            return g_runs[i].code ? g_runs[i].rw + (off - g_runs[i].off) : (void *)rx;
    return (uint8_t *)m->rw + off;  // not mapped yet: the heap copy
}

int execmem_write_code(execmem *m, void *rx, const void *src, size_t n) {
    uint8_t *w = execmem_to_rw(m, rx);
    memcpy(w, src, n);
    armDCacheFlush(w, n);
    armICacheInvalidate(rx, n);
    return 0;
}

static int map_code_range(size_t off, size_t len) {
    struct run *r = &g_runs[g_nruns];
    *r = (struct run){off, len, true, NULL, INVALID_HANDLE};
    // A fresh buffer for the code memory object, filled through its writable
    // view once mapped (the order JIT users follow: what a buffer held before
    // svcCreateCodeMemory isn't promised to carry over, and doesn't in Ryujinx).
    uint8_t *backing = memalign(SWITCH_PAGE, len);
    if (!backing) {
        LOGE("execmem: no memory for code +%#zx", off);
        return -1;
    }
    Result rc = svcCreateCodeMemory(&r->cm, backing, len);
    if (R_FAILED(rc)) {
        LOGE("execmem: svcCreateCodeMemory(+%#zx, %#zx) failed: %#x", off, len, rc);
        return -1;
    }
    virtmemLock();
    uint8_t *w = virtmemFindAslr(len, SWITCH_PAGE);
    rc = w ? svcControlCodeMemory(r->cm, CodeMapOperation_MapOwner, w, len, Perm_Rw) : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    if (R_SUCCEEDED(rc)) virtmemAddReservation(w, len);
    virtmemUnlock();
    if (R_FAILED(rc)) {
        LOGE("execmem: mapping the writable view of +%#zx failed: %#x", off, rc);
        return -1;
    }
    memcpy(w, g_src + off, len);
    armDCacheFlush(w, len);
    rc = svcControlCodeMemory(r->cm, CodeMapOperation_MapSlave, g_rx + off, len, Perm_Rx);
    if (R_FAILED(rc)) {
        LOGE("execmem: mapping +%#zx as code failed: %#x", off, rc);
        return -1;
    }
    armICacheInvalidate(g_rx + off, len);
    LOGV("execmem: +%#zx: built %08x, writable view %08x, code view %08x", off, *(u32 *)(g_src + off + 0x100),
         *(u32 *)(w + 0x100), *(u32 *)(g_rx + off + 0x100));
    r->rw = w;
    g_nruns++;
    return 0;
}

static int map_data_range(size_t off, size_t len) {
    struct run *r = &g_runs[g_nruns];
    *r = (struct run){off, len, false, NULL, INVALID_HANDLE};
    armDCacheFlush(g_src + off, len);
    Result rc = svcMapProcessCodeMemory(g_proc, (u64)(g_rx + off), (u64)(g_src + off), len);
    if (R_SUCCEEDED(rc)) rc = svcSetProcessMemoryPermission(g_proc, (u64)(g_rx + off), len, Perm_Rw);
    if (R_FAILED(rc)) {
        LOGE("execmem: mapping +%#zx (%#zx) failed: %#x", off, len, rc);
        return -1;
    }
    g_nruns++;
    return 0;
}

int execmem_protect(execmem *m, size_t off, size_t len, int prot) {
    (void)m;
    if (g_proc == 0) g_proc = self_handle();
    if (!prot) return 0;  // gaps between segments stay unmapped
    if (g_nruns == MAX_RUNS) {
        LOGE("execmem: too many ranges");
        return -1;
    }
    if ((prot & EXECMEM_W) && (prot & EXECMEM_X))
        LOGW("execmem: +%#zx is writable and executable; mapped as code (writable through its second view)", off);
    return (prot & EXECMEM_X) ? map_code_range(off, len) : map_data_range(off, len);
}

void execmem_free(execmem *m) {
    // The game runs until the app exits; the kernel takes it all back then.
    (void)m;
}
