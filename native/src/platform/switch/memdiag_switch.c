// What ends the app without a CPU exception, made visible in the log: memory
// running out (every malloc/calloc/realloc/memalign in ftl.nro goes through these
// wrappers, -Wl,--wrap), abort() and failed asserts (mesa, the game's C++ runtime,
// Hyperspace). Each logs what it can and a backtrace before the app goes.
#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "../../port_main.h"
#include "port_log.h"
#include "switch_app.h"

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t k);
void *__real_realloc(void *p, size_t n);
void *__real_memalign(size_t a, size_t n);

static atomic_int g_failures;

static void failed(const char *what, size_t n) {
    int k = atomic_fetch_add(&g_failures, 1);
    if (k >= 20) return;  // the first ones say enough
    long free_mb, total_mb, used_mb;
    port_memory(&free_mb, &total_mb, &used_mb);
    struct mallinfo mi = mallinfo();
    LOGE("out of memory: %s(%zu KB) failed; %ld MB in use, %ld MB free of %ld (malloc: %zu MB in use, %zu MB taken)",
         what, n >> 10, used_mb, free_mb, total_mb, (size_t)mi.uordblks >> 20, (size_t)mi.arena >> 20);
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    port_backtrace_from((uintptr_t)__builtin_return_address(0), 0, fp, "allocation failed");
}

void *__wrap_malloc(size_t n) {
    void *p = __real_malloc(n);
    if (!p && n) failed("malloc", n);
    return p;
}
void *__wrap_calloc(size_t n, size_t k) {
    void *p = __real_calloc(n, k);
    if (!p && n && k) failed("calloc", n * k);
    return p;
}
void *__wrap_realloc(void *q, size_t n) {
    void *p = __real_realloc(q, n);
    if (!p && n) failed("realloc", n);
    return p;
}
void *__wrap_memalign(size_t a, size_t n) {
    void *p = __real_memalign(a, n);
    if (!p && n) failed("memalign", n);
    return p;
}

static _Noreturn void die(const char *why) {
    long free_mb, total_mb, used_mb;
    port_memory(&free_mb, &total_mb, &used_mb);
    LOGE("%s (%ld MB in use, %ld MB free)", why, used_mb, free_mb);
    port_backtrace_from((uintptr_t)__builtin_return_address(0), 0, (uintptr_t)__builtin_frame_address(0), why);
    port_log_mark_crash();
    svcExitProcess();
    __builtin_unreachable();
}

_Noreturn void abort(void) { die("abort() called"); }

_Noreturn void __assert_func(const char *file, int line, const char *func, const char *expr) {
    char why[512];
    snprintf(why, sizeof why, "assertion failed: %s (%s:%d, %s)", expr, file, line, func ? func : "?");
    die(why);
}
