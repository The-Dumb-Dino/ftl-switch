# How the Switch port works

`ftl.nro` is the same port as ftl-linux (the Mach-O loader, the Objective-C
runtime, the iOS frameworks underneath the game; see
[../how-it-works.md](../how-it-works.md)) with a libnx platform layer in
`native/src/platform/switch/`. This document covers what's different on the
Switch: the toolchain, how code memory works under Horizon, and how ftlman and
Hyperspace are built into one executable.

```
native/src/platform/switch/
  main_switch.c     startup: settings, memory check, import, mods, the step screen,
                    the quiet restart into the game, saving on close
  plat_switch.c     EGL/GLES on libnx's mesa, touch, controllers, rumble, audio,
                    the keyboard and error applets, settings
  execmem_switch.c  code memory under Horizon (below)
  mods_switch.c     mods from /switch/ftl/mods/, applied with ftlman's engine
  log_switch.c      session logs and the kernel's debug output
  memdiag_switch.c  memory reports when the heap runs out
  compat_switch.c   C functions newlib lacks
```

## The toolchain

`switch/build.sh` builds with the host's clang and lld (see
[building.md](building.md)). What makes devkitPro's libraries work with them:

- `switch/toolchain.cmake`: devkitA64's flags, except thread-locals are
  emulated (`-femulated-tls`, through libgcc's emutls): libnx keeps the thread
  pointer in memory (gcc's `-mtp=soft`), which clang can't read.
- `switch/switch-lld.ld`: libnx's linker script with the changes lld needs (only
  `.relr.dyn` in `.relr.dyn`, a `PT_TLS` header, read-only data with pointers
  moved, see below) and `__eh_frame_start/_end` for libunwind.
- `libcxx/build.sh switch`: newlib with `_GNU_SOURCE`, libunwind in bare-metal
  mode (it finds `.eh_frame` through linker symbols) with the Mach-O hook
  (`libcxx/llvm-19.1.7-ftl-switch.patch`), so C++ exceptions can still cross
  the game's frames.
- mesa: its GPU compiler is C++ built against libstdc++, whose
  `std::exception` & co. clash with the game's libc++. The build links mesa
  with the libstdc++ it needs into one object (`mesa-private.o`) and makes
  everything but `egl*`/`gl*` local in it, so mesa keeps a private C++ runtime.
  Seven NIR functions that only mesa's nv50 path (older GPUs) refers to get
  stubs.
- The GPU driver service gets 64 MB of transfer memory
  (`__nx_nv_transfermem_size`) instead of libnx's 8 MB: Multiverse's thousands
  of textures ran out of it.

## Relocations and read-only memory

crt0 relocates `ftl.nro` before anything else runs, and the Homebrew loader maps
`.text` and `.rodata` without write access. Some prebuilt libraries (newlib's
locale tables, libstdc++'s typeinfo and vtables inside `mesa-private.o`,
libsysbase's device table) are built without PIC and keep pointers in
`.rodata`; `switch/switch-lld.ld` moves those into the writable segment, and
`switch/check-relocs.py` fails the build if a relocation still lands in
read-only memory. Emulators don't enforce this, so such a build runs there and
crashes on a console before `main`, without a log.

## How the game's code is mapped

Horizon never lets a page be writable and executable at once, code only runs
from memory the kernel mapped as code, and a range's permission can't be
changed back and forth. `execmem_switch.c` (behind the loader's `execmem`
interface):

1. builds the image in ordinary heap memory (the loader's writable view); its
   address in the code region is only reserved, and the loader writes and reads
   through the writable view until `mo_protect` (`mo_wptr`/`mo_rptr`);
2. at `mo_protect` maps each range at its address once:
   - code (RX) as a *code memory object*, the way JIT emulators do: a fresh
     buffer, `svcCreateCodeMemory`, a writable view somewhere else (MapOwner
     RW), the code copied in through it, then the executable view at the
     game's address (MapSlave RX). The writable view stays: code can still be
     patched while it runs (`execmem_write_code`, Hyperspace's hooks);
   - everything else as the system's module loader (ro) maps `.nro` data:
     `svcMapProcessCodeMemory`, then RW once.

Making a fresh code mapping writable first and executable later doesn't work
(it becomes "code data", which the kernel refuses to make executable), and a
buffer's contents before `svcCreateCodeMemory` aren't promised to carry over
(they don't in Ryujinx), hence the order above. The iOS binary's segments are
16K aligned on the Switch's 4K pages, so code and data never share a page.

Both calls take a real handle to the app's own process. Atmosphère's homebrew
loader passes one; without it (other loaders, Ryujinx) one is made as hbloader
makes it: the "current process" pseudo-handle sent over an IPC session to
itself, which the kernel turns into a real handle.

Paths are `/switch/ftl/...` rather than `sdmc:/switch/ftl/...`: the SD card is
libnx's default device, and the game only treats paths that start with `/` as
absolute (with `sdmc:` it looked for its images relative to `.`).

## Startup and exit

`main_switch.c` shows each step on a text console (settings, memory, the game,
mods, preparing mod images). Once the console has been used, the graphics
can't get a clean window in the same run, so `ftl.nro` asks the Homebrew Menu
to start it again with `--quiet` (`envSetNextLoad`), which goes straight into
the game. `appletLockExit` holds a close from the HOME menu until the game has
saved.

## ftlman on the Switch

Rust's standard library has no Switch target, but it supports the 3DS's Horizon
with newlib, which is the C library `ftl.nro` uses too. `switch/rust/` builds
ftlman (`ftlman/switch.patch`: the headless engine as a static library with one C
function, `ftlman_patch`) for `aarch64-nintendo-switch-ftl.json`, that target
for aarch64, with std built from source (`-Zbuild-std`) against:

- `libc` with the Switch added: the types and sizes measured from devkitPro's
  headers (`stat`, `dirent`, the pthread types, 16-bit ids);
- `lua-src` and `getrandom`, which learn the target;
- a few C functions newlib lacks (`getrandom` from libnx, `readv`/`writev`,
  permissions the SD card doesn't have), in `compat_switch.c`.

It runs on its own thread with an 8 MB stack. Its Lua 5.4 and Rust runtime are
kept private in `ftl.nro` (one pre-linked object, `ftlman-private.o`, with only
`ftlman_patch` global), next to Hyperspace's Lua 5.3.

`mods_switch.c` decides when to patch: it keeps a signature of `mods/` and of
`ftl.dat` in `logs/mods.applied`. A change in `mods/` patches again; a changed
`ftl.dat` with the same `mods/` is taken as patched elsewhere (on a PC) and used
as it is.

## Hyperspace

It's the same Hyperspace as for Linux (`hyperspace/linux.patch` on the
[iPad fork](https://github.com/Sidd065/FTL-Hyperspace)) plus
`hyperspace/switch.patch`:

- a static library (`HS_IPAD_SWITCH`), built without vcpkg: only Boost's
  header-only parts are used, Lua 5.3 is compiled with the Switch toolchain,
  minizip and zlib are devkitPro's. It's linked with `--whole-archive`: most of
  Hyperspace's files only register their hooks from static constructors, which
  a normal static link would drop.
- code memory from the host: Horizon never makes code writable, so the hook
  engine (`Arm64Detour`) takes its trampoline pool and writes all code through
  two callbacks the port passes (`alloc_code`, `write_code`, optional new fields
  at the end of `hs_host_info`; hosts without them keep using
  `mmap`/`mprotect`). The pool is part of the near-trampoline area next to the
  game (1.25 MB on the Switch) and the writes go through the game's second,
  writable view of its code (above). Data, read-only data included, stays
  writable on the Switch, so Hyperspace's vtable patches need nothing.
- message boxes: the port passes `show_error`, another optional field at the
  end of `hs_host_info`, which opens the Switch's error applet. Hosts without
  it keep logging the messages only.

The new fields are optional and at the end of the structure, so the same
Hyperspace source still builds for its Android host unchanged.
