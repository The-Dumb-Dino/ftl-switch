// Session logs on the Switch: every line goes to the kernel's debug output
// (shown by debuggers and emulators' logs) and to
// sdmc:/switch/ftl/logs/session-<date>.txt. Not to stdout: that is libnx's text
// console while it's open, and still points at it once it's closed.
// The crash marker works as on Linux: the next start mentions it.
#include <dirent.h>
#include <malloc.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <switch.h>
#include <time.h>
#include <unistd.h>

#include "port_log.h"

#define KEEP_SESSIONS 5
#define MAX_SESSION_BYTES (8 << 20)

static FILE *g_file;
static size_t g_bytes;
static char g_crash_path[600];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void prune(const char *dir, int keep) {
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[128];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 128)
        if (!strncmp(e->d_name, "session-", 8)) names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, n, sizeof *names, by_name);
    for (int i = 0; i < n; i++) {
        if (i < n - keep) {
            char p[600];
            snprintf(p, sizeof p, "%s/%s", dir, names[i]);
            unlink(p);
        }
        free(names[i]);
    }
}

void port_memory(long *free_mb, long *total_mb, long *game_mb) {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    // libnx takes the whole heap at start, so the kernel counts all of it as
    // used: count what malloc has handed out instead (in use now; what it took
    // from the heap and keeps for reuse counts as free).
    u64 heap = 0;
    svcGetInfo(&heap, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
    struct mallinfo mi = mallinfo();
    u64 heap_used = (u64)mi.uordblks;
    extern char *fake_heap_start, *fake_heap_end;
    u64 heap_total = (u64)(fake_heap_end - fake_heap_start);
    u64 outside = used > heap_total ? used - heap_total : 0;  // code, stacks, GPU memory
    u64 in_use = outside + heap_used;
    (void)heap;
    *total_mb = (long)(total >> 20);
    *game_mb = (long)(in_use >> 20);
    *free_mb = (long)((total > in_use ? total - in_use : 0) >> 20);
}

void port_log_open(const char *dir) {
    if (g_file || !dir || !*dir) return;
    mkdir(dir, 0777);
    prune(dir, KEEP_SESSIONS - 1);
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char name[64], path[600];
    strftime(name, sizeof name, "session-%Y%m%d-%H%M%S.txt", &tm);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(g_crash_path, sizeof g_crash_path, "%s/crashed", dir);
    g_file = fopen(path, "w");
    char date[32];
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
    long free_mb, total_mb, game_mb;
    port_memory(&free_mb, &total_mb, &game_mb);
    char fw[32] = "?";
    SetSysFirmwareVersion v;
    if (R_SUCCEEDED(setsysInitialize())) {
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&v))) snprintf(fw, sizeof fw, "%s", v.display_version);
        setsysExit();
    }
    extern char __code_start[] __attribute__((visibility("hidden")));
    LOGI("session started %s on Horizon %s%s, %ld MB for the app (%ld MB free); ftl.elf at %p", date, fw,
         hosversionIsAtmosphere() ? " (Atmosphère)" : "", total_mb, free_mb, (void *)__code_start);
}

void port_log(int prio, const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    char lvl = prio >= PORT_LOG_ERROR ? 'E' : prio == PORT_LOG_WARN ? 'W' : 'I';
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char line[1100];
    int n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03d %c %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec,
                     (int)(tv.tv_usec / 1000), lvl, msg);
    if (n >= (int)sizeof line) n = sizeof line - 1;
    pthread_mutex_lock(&g_lock);
    svcOutputDebugString(line, (u64)n);
    if (g_file && g_bytes <= MAX_SESSION_BYTES) {
        g_bytes += (size_t)n;
        fputs(g_bytes > MAX_SESSION_BYTES ? "(log cut here: too long)\n" : line, g_file);
        // Every line, written through: the last ones before a crash are what's needed
        // (aborts and the system ending the app don't run the crash handler).
        fflush(g_file);
    }
    pthread_mutex_unlock(&g_lock);
}

void port_log_flush(void) {
    if (g_file) fflush(g_file);
}

void port_log_mark_crash(void) {
    if (!g_crash_path[0]) return;
    FILE *f = fopen(g_crash_path, "w");
    if (f) fclose(f);
    port_log_flush();
}
