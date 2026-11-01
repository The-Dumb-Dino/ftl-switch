# How it works

Playing and building are described in the [README](../README.md) and
`docs/switch/`. This document is about how the iPad game runs on the Switch.

The idea, and much of the machinery described here (the Mach-O loader, the
Objective-C runtime, the iOS frameworks and the touch layer), comes from
[ftl-android](https://github.com/Sidd065/ftl-android) by Sidd065. This project
took that core off Android and onto the Nintendo Switch.
[CREDITS.md](../CREDITS.md) lists what came from where.

## The short version

Nothing is recompiled. The original arm64 iOS executable of FTL for iPad runs
as it is, inside a Switch homebrew program (`ftl.nro`):

1. The **Mach-O loader** maps the game's executable into memory and links its
   imports against the port's own replacements.
2. Underneath the game is just enough of iOS for it to run: a C library with
   Darwin's ABI, an **Objective-C runtime** with Apple's ABI, Apple's C++
   library ABI, and small reimplementations of the **iOS frameworks** the game
   uses (UIKit, Foundation, QuartzCore/OpenGL ES, AudioToolbox, ...).
3. A **platform layer** connects all of that to the console: libnx, with
   EGL/GLES 3 on mesa for drawing, touch, controllers, audio and the system
   applets.
4. A **UX layer** adds what the iPad game doesn't have: controller input with
   a cursor, zoom, the magnifier, and help for PC mods.

The game believes it is on an iPad whose screen has the Switch's shape.

## Layers

```
native/src/
  platform/switch/
    main_switch.c     startup: settings, memory check, import, updates, mods,
                      the text screen, the restart into the game, saving on close
    plat_switch.c     EGL/GLES on libnx's mesa, touch, controllers, rumble, audio,
                      the keyboard and error applets
    execmem_switch.c  code memory under Horizon
    mods_switch.c     mods from /switch/ftl/mods/, applied with ftlman's engine;
                      the mod list
    update_switch.c   the updater (release builds)
    log_switch.c      session logs and the kernel's debug output
    memdiag_switch.c  memory reports when the heap runs out
    compat_switch.c   C functions newlib lacks
  platform/linux/     settings.ini, the .ipa import, zip reading, the mod image
                      overlay (from the Linux version this port grew out of)
  platform/plat_linux.c  the headless test harness (testing.md)
  ux/                 layout, zoom, gestures, magnifier, minimap, controller cursor,
                      mods' images and atlases, PNG
  fw/                 UIKit, Foundation, QuartzCore/EAGL, AudioUnit, GameKit and
                      GameController stand-ins
  objc/               Objective-C runtime with Apple's ABI (objc_msgSend in assembly)
  darwin/             libSystem: Darwin structs, pthreads, Mach semaphores, errno
  loader/             Mach-O loader: maps the arm64 slice, rebases, binds imports;
                      executable memory through execmem.h
  port_main.c         boot sequence: frameworks, load, bind, patch, protect, run main()
  patches.c           hooks into the game's code (only for the 1.6.21 build)
  hyperspace.c        starts Hyperspace, when the applied mods include it
```

## Startup

`main_switch.c` shows each step on a text console: settings, memory, the game
(imported from the `.ipa` on the first start), mods, preparing mod images.

- Release builds (`FTL_SWITCH_UPDATE_REPO`) first run `update_switch.c` on that
  console. It asks the GitHub API for the latest release (devkitPro's curl,
  whose TLS is the system's `ssl` service), downloads its `ftl.nro` next to the
  running one as `ftl.nro.new`, checks size, SHA-256 and the NRO header, swaps
  the files (the old one back if that fails) and starts the new one with
  `envSetNextLoad`.
- With mods in `mods/` it waits 1.5 s at the mods step for L, which opens the
  mod list (`switch_mods_menu` in `mods_switch.c`) on that console. It's a
  press, not a hold: a button held while an app starts isn't reported to it.
- Once the console has been used, the graphics can't get a clean window in the
  same run, so `ftl.nro` asks the Homebrew Menu to start it again with
  `--quiet` (`envSetNextLoad`), which goes straight into the game.

Then `port_main.c` boots the game: the frameworks are set up, the executable is
loaded and bound, the 1.6.21 patches are applied, the image is protected, the
game's Objective-C classes are registered, Hyperspace (if wanted) hooks the
game, the game's static initializers run, and finally its `main()` is called,
which calls `UIApplicationMain` like any iOS app.

`appletLockExit` holds a close from the HOME menu until the game has saved.

Paths are `/switch/ftl/...` rather than `sdmc:/switch/ftl/...`: the SD card is
libnx's default device, and the game only treats paths that start with `/` as
absolute (with `sdmc:` it looked for its images relative to `.`).

## The Mach-O loader (`loader/`)

`macho.c` reads the arm64 slice of the executable, copies its segments into
memory, applies rebases and binds the imports (DYLD_INFO-style fixups, which is
what the 1.6.21 build uses). Every import is resolved by name through
`symbols.c`, a table of the port's replacements for libSystem, the frameworks
and the C++ library. `symbols_cxx.c` is generated by `tools/gen_cxx_syms.py`.

Segments are copied rather than mapped from the file, and protections are set
per host page, so the loader works with any page size. The iOS binary's
segments are 16K aligned, so on the Switch's 4K pages code and data never share
a page.

## Code memory under Horizon

Executable memory goes through `loader/execmem.h`: allocate, protect, and
separate write and execute views of the same code. On a POSIX system (the test
harness) one anonymous mapping serves both. Horizon is stricter: a page is
never writable and executable at once, code only runs from memory the kernel
mapped as code, and a range's permission can't be changed back and forth.
`execmem_switch.c`:

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
(they don't in Ryujinx), hence the order above.

Both calls take a real handle to the app's own process. Atmosphère's homebrew
loader passes one; without it (other loaders, Ryujinx) one is made as hbloader
makes it: the "current process" pseudo-handle sent over an IPC session to
itself, which the kernel turns into a real handle.

## The Objective-C runtime (`objc/`)

The game is mostly C++ but talks to iOS through Objective-C. `objc/` is a
runtime with Apple's arm64 objc2 ABI, so the game's compiled class lists,
selector references and method lists can be used as they are:
`objc_runtime.c` registers the game's classes and the port's own framework
classes, and `objc_msgSend.S` is the message dispatcher in assembly with a
lock-free method cache. `objc_api.c` has the less common entry points (ARC
helpers, properties, associated objects).

## The C library and the C++ library (`darwin/`, `libcxx/`)

iOS's C library differs from newlib in struct layouts (`stat`, `dirent`,
`tm`...), errno values, flags and some calling conventions. `darwin/libsystem.c`
translates between the two, and has pthreads and Mach semaphores built on the
host's.

The game was built against Apple's libc++, so the port links its own copy built
to match: the alternate string layout, constructors that return `this`, Darwin
ctype masks, and a libunwind that can read Mach-O compact unwind info, so C++
exceptions can cross the game's frames (`port_main.c` tells it where the game's
`__unwind_info` and `__eh_frame` are). The changes to LLVM 19.1.7 are in
`libcxx/llvm-19.1.7-ftl.patch` and, for the Switch, `-ftl-switch.patch`;
`libcxx/build.sh` applies them.

## The frameworks (`fw/`)

Only what FTL uses, plus a little:

- `uikit.c`: `UIApplicationMain`, the main run loop, a minimal view tree, touch
  events, alerts and text input (forwarded to the platform layer: the Switch's
  keyboard and error applets).
- `foundation.c`: Foundation and CoreFoundation (strings, collections,
  bundles, user defaults, file manager, dates).
- `quartz_eagl.c`: `CAEAGLLayer`, `CADisplayLink` and `EAGLContext` on top of
  GLES 3. The game renders into an FBO that `present.c` puts on the screen
  (with the UX layer's zoom and overlays). BGRA textures are swizzled, and
  PVRTC textures, which iOS GPUs sample natively, are decoded on the CPU
  (`pvrtc.c`).
- `audio_misc.c`: the RemoteIO AudioUnit and AVAudioSession on the platform's
  audio output.
- `gamecontroller.c`: GameController and GameKit stubs (the UX layer drives
  controllers instead).

## Hooks into the game (`patches.c`, `ux/`)

A few features hook specific addresses in the 1.6.21 binary: the logger, the
pause action, the hotkey lookup, menu and window state, the PNG reader and the
`ftl.dat` reader for mods. `ux/game.h` has the addresses and struct offsets.
They're only enabled when the binary's `LC_UUID` matches that build, and
Hyperspace only starts on it. Other versions run without them; the session
log says which version it is and whether it gets them. Hook stubs jump
through `x17`, so Hyperspace can hook the same functions.

The UX layer (`ux/ux.c`, `pad.c`, `overlay.c`, `targets.c`) works on the game
image: it maps touch and controller input onto the game's touch coordinates,
zooms by drawing part of the game's image bigger, and draws the magnifier and
minimap on top. For the controller cursor, the game's thread collects the
clickable things on screen (`targets.c`), so the cursor can be drawn to them.

## The toolchain

`switch/build.sh` builds with the host's clang and lld (see
[switch/building.md](switch/building.md)). What makes devkitPro's libraries
work with them:

- `switch/toolchain.cmake`: devkitA64's flags, except thread-locals are
  emulated (`-femulated-tls`, through libgcc's emutls): libnx keeps the thread
  pointer in memory (gcc's `-mtp=soft`), which clang can't read.
- `switch/switch-lld.ld`: libnx's linker script with the changes lld needs (only
  `.relr.dyn` in `.relr.dyn`, a `PT_TLS` header, read-only data with pointers
  moved, see below) and `__eh_frame_start/_end` for libunwind.
- `libcxx/build.sh switch`: newlib with `_GNU_SOURCE`, libunwind in bare-metal
  mode (it finds `.eh_frame` through linker symbols) with the Mach-O hook, so
  C++ exceptions can still cross the game's frames.
- mesa: its GPU compiler is C++ built against libstdc++, whose
  `std::exception` & co. clash with the game's libc++. The build links mesa
  with the libstdc++ it needs into one object (`mesa-private.o`) and makes
  everything but `egl*`/`gl*` local in it, so mesa keeps a private C++ runtime.
  Seven NIR functions that only mesa's nv50 path (older GPUs) refers to get
  stubs.
- The GPU driver service gets 64 MB of transfer memory
  (`__nx_nv_transfermem_size`) instead of libnx's 8 MB: Multiverse's thousands
  of textures ran out of it.

### Relocations and read-only memory

crt0 relocates `ftl.nro` before anything else runs, and the Homebrew loader maps
`.text` and `.rodata` without write access. Some prebuilt libraries (newlib's
locale tables, libstdc++'s typeinfo and vtables inside `mesa-private.o`,
libsysbase's device table) are built without PIC and keep pointers in
`.rodata`; `switch/switch-lld.ld` moves those into the writable segment, and
`switch/check-relocs.py` fails the build if a relocation still lands in
read-only memory. Emulators don't enforce this, so such a build runs there and
crashes on a console before `main`, without a log.

## Mods

Mods are patched into `ftl.dat` with ftlman, as on PC, and the iPad build needs
some help with what they add:

- It was built without its PNG reader, since its own art is all `.tex`
  textures. `ux/png.c` puts one back (stb_image).
- It looks for `x.tex` before `x.png`, so the ftl.dat reader hides an iPad
  texture when a mod added a PNG of the same name.
- It packs small images into atlases. At launch, when `ftl.dat` changed, the
  atlas lists without the images mods replaced are written into a small overlay
  in the cache, which the reader serves instead (`ux/mods.c`,
  `platform/linux/mods_linux.c`).

### ftlman on the Switch

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

### Patching in memory

Small reads and writes on the SD card are slow, and ftlman's archive library
(silpkg) moves data in 8 KB pieces: Multiverse took 27 minutes patched in place,
over half of it inserting files and repacking. So `ftlman_patch` patches a copy
in memory when it can fit (at most `ftl.dat.vanilla` plus the mods unpacked;
`mods_switch.c` keeps 384 MB of the free memory back): `ftl.dat.vanilla` is read
and `ftl.dat` written once, in 4 MB blocks, with the same result byte for byte.
Multiverse with the Renegade Collection takes about two minutes, most of it
writing `ftl.dat`.

The copy is kept in 16 MB pieces, since the Switch may not give one block of a
gigabyte or two even with that much free. When it can't fit, or memory runs out
on the way (reported as an error, not an abort), it patches the file on the SD
card. Either way mod files are read in 1 MB blocks (through a buffer that keeps
its data when the zip reader jumps between file headers), and `ftl.dat` is
repacked: without it, silpkg 0.1.4 leaves a broken path table when it had to
grow it (it copies the table onto itself).

## Hyperspace

### One Hyperspace for every port of the iPad game

Hyperspace hooks the game's functions at their addresses in the binary. Every
port of FTL for iPad (the Android app, this Switch port and the ARM Linux
version) runs the very same iPad binary, 1.6.21, so they all hook the same
addresses with the same definitions. They therefore share one Hyperspace: the
[`iOS-binary-ports`](https://github.com/The-Dumb-Dino/FTL-Hyperspace-Dino/tree/iOS-binary-ports)
branch of FTL-Hyperspace-Dino, which builds on Sidd065's
[iPad fork](https://github.com/Sidd065/FTL-Hyperspace) and has a build target
for each port (`buildscripts/android/`, `linux/` and `switch/`).
`hyperspace/source.sh` pins the commit this repository builds.

The `hs_android_*` names and the `HS_ANDROID` code paths come from that fork,
which started on Android: the interface is the same for every port.

### Starting it

`hyperspace.c` starts Hyperspace once the game is mapped, linked and protected,
before any game code runs, and calls its `hs_android_init` with where the game
is (`hyperspace_host.h`, shared with Hyperspace). Its logs (zhl.log,
FTL_HS.log) go to the log folder with the session logs. If it fails to start,
the game doesn't start and the reason is shown.

For mods' right clicks, `ux.c` asks it whether a script listens for the right
button (`hs_android_wants_right_click`) and, if so, sends it the touch gesture
(one finger held, a tap with another) and the controller's right click
(`hs_android_right_button`).

### Built into `ftl.nro`

`hyperspace/build-switch.sh` builds it with the fork's `buildscripts/switch/`:

- a static library (`HS_IPAD_SWITCH`), built without vcpkg: only Boost's
  header-only parts are used, Lua 5.3 is compiled with the Switch toolchain,
  minizip and zlib are devkitPro's. It's linked with `--whole-archive`: most of
  Hyperspace's files only register their hooks from static constructors, which
  a normal static link would drop.
- code memory from the host: Horizon never makes code writable, so the hook
  engine (`Arm64Detour`) takes its trampoline pool and writes all code through
  two callbacks the port passes (`alloc_code`, `write_code`, optional fields at
  the end of `hs_host_info`; ports without them keep using `mmap`/`mprotect`).
  The pool is part of the near-trampoline area next to the game (1.25 MB on the
  Switch) and the writes go through the game's second, writable view of its
  code (see [Code memory under Horizon](#code-memory-under-horizon)). Data,
  read-only data included, stays writable on the Switch, so Hyperspace's vtable
  patches need nothing.
- message boxes: the port passes `show_error`, another optional field at the
  end of `hs_host_info`, which opens the Switch's error applet. Ports without
  it keep logging the messages only.
