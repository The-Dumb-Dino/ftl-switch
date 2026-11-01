#pragma once
#include <stdio.h>

#ifdef FTL_LINUX_APP
// stderr, plus the session log file once port_log_open has run (platform/linux/log_linux.c)
enum { PORT_LOG_INFO = 4, PORT_LOG_WARN = 5, PORT_LOG_ERROR = 6 };
void port_log(int prio, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void port_log_open(const char *dir);
void port_log_mark_crash(void);  // async-signal-safe
// Memory in MB: free on the machine, its total, and what the game holds (0 where unknown)
void port_memory(long *free_mb, long *total_mb, long *game_mb);
#define LOGI(...) port_log(PORT_LOG_INFO, __VA_ARGS__)
#define LOGW(...) port_log(PORT_LOG_WARN, __VA_ARGS__)
#define LOGE(...) port_log(PORT_LOG_ERROR, __VA_ARGS__)
#else
#define LOGI(...) do { fprintf(stderr, "[I] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOGW(...) do { fprintf(stderr, "[W] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOGE(...) do { fprintf(stderr, "[E] " __VA_ARGS__); fputc('\n', stderr); } while (0)
static inline void port_log_mark_crash(void) {}
static inline void port_memory(long *free_mb, long *total_mb, long *game_mb) { *free_mb = *total_mb = *game_mb = 0; }
#endif

extern int port_verbose;
#define LOGV(...) do { if (port_verbose) LOGI(__VA_ARGS__); } while (0)

_Noreturn void port_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
