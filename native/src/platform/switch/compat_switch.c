// What newlib (devkitA64's) leaves out that the Darwin layer binds the game to,
// and the Hyperspace hooks the port calls (Hyperspace isn't built for the Switch).
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../hyperspace.h"
#include "port_log.h"

// pread: newlib's devices have no positioned read; seek, read and seek back,
// one at a time (the game reads ftl.dat from more than one thread).
static pthread_mutex_t g_pread_lock = PTHREAD_MUTEX_INITIALIZER;
ssize_t pread(int fd, void *buf, size_t n, off_t off) {
    pthread_mutex_lock(&g_pread_lock);
    off_t was = lseek(fd, 0, SEEK_CUR);
    ssize_t r = -1;
    if (was >= 0 && lseek(fd, off, SEEK_SET) == off) {
        r = read(fd, buf, n);
        int e = errno;
        lseek(fd, was, SEEK_SET);
        errno = e;
    }
    pthread_mutex_unlock(&g_pread_lock);
    return r;
}

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE: return 0x1000;
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN: return 3;  // the cores an application may use
    case _SC_PHYS_PAGES:                  // what the app may use: its share, not the console's 4 GB
    case _SC_AVPHYS_PAGES: {
        long free_mb, total_mb, used_mb;
        port_memory(&free_mb, &total_mb, &used_mb);
        return (name == _SC_PHYS_PAGES ? total_mb : free_mb) * 256;
    }
    case _SC_CLK_TCK: return 100;
    default: errno = EINVAL; return -1;
    }
}

int posix_memalign(void **out, size_t align, size_t size) {
    if (align < sizeof(void *) || (align & (align - 1))) return EINVAL;
    void *p = memalign(align, size ? size : 1);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

// ---- for Rust's standard library and ftlman's crates (switch/rust/) ----
#include <dirent.h>
#include <sys/time.h>
// newlib has no sys/uio.h; the layout Rust's libc uses
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#include <switch.h>

// The Switch's random source (libnx seeds it from the kernel's entropy)
ssize_t getrandom(void *buf, size_t n, unsigned flags) {
    (void)flags;
    randomGet(buf, n);
    return (ssize_t)n;
}

// No file descriptors behind newlib's directories here
int dirfd(DIR *d) {
    (void)d;
    errno = ENOTSUP;
    return -1;
}

ssize_t readv(int fd, const struct iovec *iov, int n) {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        ssize_t r = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int n) {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        ssize_t r = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

// The SD card has no Unix permissions; newlib's say "not implemented", which fails
// Rust's fs::copy (it copies them last). Accept them, as the card would ignore them.
#include <sys/stat.h>
int chmod(const char *path, mode_t mode) {
    (void)mode;
    struct stat st;
    return stat(path, &st);
}
int fchmod(int fd, mode_t mode) {
    (void)fd, (void)mode;
    return 0;
}

// File times can't be set on the SD card through newlib; ftlman only tries to keep them.
int futimes(int fd, const struct timeval tv[2]) {
    (void)fd, (void)tv;
    return 0;
}
int lutimes(const char *path, const struct timeval tv[2]) {
    (void)path, (void)tv;
    return 0;
}

// ---- Hyperspace: stubs when it isn't linked in (FTL_SWITCH_HYPERSPACE) ----
#ifndef FTL_SWITCH_HYPERSPACE
int hyperspace_start(const char *lib, const char *log_dir, const char *data_dir, struct mo_image *img, char *err,
                     size_t errsize) {
    (void)lib, (void)log_dir, (void)data_dir, (void)img, (void)err, (void)errsize;
    return -1;
}
bool hyperspace_wants_right_click(void) { return false; }
void hyperspace_right_button(int action, double x, double y) { (void)action, (void)x, (void)y; }
#endif

// ---- mesa ----
// devkitPro's mesa refers to these NIR functions only from its nv50 (older
// NVIDIA GPU) shader path, which the Switch's Maxwell GPU never takes; GNU ld
// drops that code, the whole-archive pre-link of mesa keeps it. Stubs, so it links.
#include <stdlib.h>
#include "port_log.h"
#define NV50_ONLY(name)                                                   \
    void name(void);                                                      \
    void name(void) {                                                     \
        LOGE("mesa called %s, which is only for older GPUs", #name);      \
        abort();                                                          \
    }
NV50_ONLY(nir_deref_instr_create)
NV50_ONLY(nir_function_impl_create)
NV50_ONLY(nir_instr_insert)
NV50_ONLY(nir_intrinsic_instr_create)
NV50_ONLY(nir_local_variable_create)
NV50_ONLY(nir_shader_clone)
NV50_ONLY(nir_ssa_dest_init)
const char nir_intrinsic_infos[1];
