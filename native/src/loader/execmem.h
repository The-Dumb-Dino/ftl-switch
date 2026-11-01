// Memory for code the port maps or writes at run time: the game image (with
// the near trampolines after it). Kept behind this small interface so a host
// with separate writable and executable views of the same memory (e.g. libnx's
// JIT on the Switch) can implement it; on Linux both views are the same mapping
// and only its protection changes.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { EXECMEM_R = 1, EXECMEM_W = 2, EXECMEM_X = 4 };

typedef struct execmem {
    uint8_t *rw;  // where the port writes (relocations, patches)
    uint8_t *rx;  // where the code runs; the game's slide is computed from this
    size_t size;
} execmem;

// The host's page size (sysconf), cached. Protection works in whole pages.
size_t execmem_page_size(void);

// Reserve `size` bytes aligned to `align` (and at least to the page size),
// zero-filled, readable and writable.
int execmem_alloc(execmem *m, size_t size, size_t align);

// Set the protection (EXECMEM_* bits) of [off, off+len), which must be page
// aligned. Flushes the instruction cache when making memory executable.
int execmem_protect(execmem *m, size_t off, size_t len, int prot);

void execmem_free(execmem *m);

// The writable address of rx. POSIX: the same mapping. Switch: the heap copy
// before execmem_protect; afterwards the code's second, writable view (code
// stays patchable while it runs), and the data itself.
void *execmem_to_rw(const execmem *m, const void *rx);

// Writes n bytes of code at rx while it may be running (through the writable
// view on hosts that have one, else by making the page writable for a moment)
// and makes them visible to instruction fetch. 0 on success.
int execmem_write_code(execmem *m, void *rx, const void *src, size_t n);
