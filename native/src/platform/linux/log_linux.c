// Session logs for the Linux app: every log line goes to stderr and to
// $XDG_STATE_HOME/ftl-linux/session-<date>.txt (the newest few sessions are
// kept). The crash handler leaves a marker so the next start can mention it.
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "port_log.h"

#define KEEP_SESSIONS 5
#define MAX_SESSION_BYTES (8 << 20)

static int g_fd = -1;
static size_t g_bytes;
static char g_crash_path[600];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

// Keep the newest `keep` session logs (their names sort by date).
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

void port_log_open(const char *dir) {
    if (g_fd >= 0 || !dir || !*dir) return;
    mkdir(dir, 0700);
    prune(dir, KEEP_SESSIONS - 1);
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char name[64], path[600];
    strftime(name, sizeof name, "session-%Y%m%d-%H%M%S.txt", &tm);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(g_crash_path, sizeof g_crash_path, "%s/crashed", dir);
    g_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    struct utsname u;
    uname(&u);
    char date[32];
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
    long free_mb, total_mb, game_mb;
    port_memory(&free_mb, &total_mb, &game_mb);
    LOGI("session started %s on %s %s %s, %ldK pages, %ld MB of memory (%ld MB free)", date, u.sysname, u.release,
         u.machine, sysconf(_SC_PAGESIZE) / 1024, total_mb, free_mb);
}

void port_memory(long *free_mb, long *total_mb, long *game_mb) {
    long total = 0, avail = 0, rss = 0;
    char line[128];
    FILE *f = fopen("/proc/meminfo", "re");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            sscanf(line, "MemTotal: %ld", &total);
            sscanf(line, "MemAvailable: %ld", &avail);
        }
        fclose(f);
    }
    f = fopen("/proc/self/status", "re");
    if (f) {
        while (fgets(line, sizeof line, f)) sscanf(line, "VmRSS: %ld", &rss);
        fclose(f);
    }
    *free_mb = avail >> 10;
    *total_mb = total >> 10;
    *game_mb = rss >> 10;
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
    if (n >= (int)sizeof line) {
        n = sizeof line - 1;
        line[n - 1] = '\n';
    }
    pthread_mutex_lock(&g_lock);
    if (port_verbose || prio >= PORT_LOG_WARN || getenv("FTL_LOG_STDERR")) fputs(line, stderr);
    if (g_fd >= 0 && g_bytes <= MAX_SESSION_BYTES) {
        g_bytes += n;  // a soft cap, so a runaway message can't fill the disk
        if (g_bytes > MAX_SESSION_BYTES) {
            static const char cut[] = "(log cut here: too long)\n";
            n = sizeof cut - 1;
            memcpy(line, cut, n);
        }
        ssize_t w = write(g_fd, line, n);
        (void)w;
    }
    pthread_mutex_unlock(&g_lock);
}

void port_log_mark_crash(void) {
    if (!g_crash_path[0]) return;
    int fd = open(g_crash_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0) close(fd);
}
