# Development

Building, options and paths are in the [README](../README.md). This is how the
port works.

## Layers

```
platform/linux/      ftl-linux: SDL3 window, EGL, input, audio, dialogs, settings.ini,
                     import, portable folders, ftlman, logs (main_linux.c, plat_sdl.c)
ux/                  layout, zoom, gestures, magnifier, minimap, hotkeys, controller
                     cursor, mods' images and atlases, PNG
fw/                  UIKit, Foundation, QuartzCore/EAGL, AudioUnit, GameKit stubs;
                     EAGL maps onto GLES 3 (the game renders to an FBO that gets
                     blitted to the window), BGRA swizzling, PVRTC decoded on the CPU
objc/                Objective-C runtime with Apple's ABI (objc_msgSend in assembly)
darwin/              libSystem: Darwin structs, pthreads, Mach semaphores, errno
loader/              Mach-O loader: maps the arm64 slice, rebases, binds imports;
                     executable memory through execmem.h
```

The original arm64 iOS executable runs inside `ftl-linux`: the Mach-O loader
maps it into memory, and just enough of iOS is reimplemented underneath it for
the game to run. The game believes it's on an iPad whose screen has the window's
aspect ratio. It lays out a canvas 768 units tall and as wide as the window,
rendered at 1536 pixels on big windows so text stays sharp when zoomed.

The game was built against Apple's libc++, so the port links its own copy built
to match (alternate string layout, constructors that return `this`, Darwin
ctype masks, and libunwind that can read Mach-O compact unwind info). The
changes to LLVM are in `libcxx/llvm-19.1.7-ftl.patch`; `libcxx/build.sh`
applies them.

A few features hook specific addresses in the 1.6.21 binary (the logger, the
pause action, hotkey lookup, menu and window state, the PNG reader and the
ftl.dat reader for mods). They're only enabled when the binary's LC_UUID matches
that build, and Hyperspace only starts on it. Other versions run without them;
`ftl-linux --info` shows the version and whether it gets them.

## Mods

ftlman patches `ftl.dat` as on PC. The iPad build needs some help with what mods
add:

- It was built without its PNG reader, since its own art is all `.tex`
  textures. `ux/png.c` puts one back (stb_image).
- It looks for `x.tex` before `x.png`, so the ftl.dat reader hides an iPad
  texture when a mod added a PNG of the same name.
- It packs small images into atlases. At launch, when `ftl.dat` changed, the
  atlas lists without the images mods replaced are written into a small overlay
  in the cache, which the reader serves instead (`ux/mods.c`).

## Hyperspace

`hyperspace.c` loads Hyperspace's library once the game is mapped, linked and
protected, before any game code runs, and calls its `hs_android_init` with where
the game is (`hyperspace_host.h`, shared with Hyperspace's iPad branch, whose
names these are). Its logs (zhl.log, FTL_HS.log) go to the log folder with the
session logs. If it fails to start, the game doesn't start and the reason is
shown. For mods' right clicks, `ux.c` asks it whether a script listens for the
right button (`hs_android_wants_right_click`) and, if so, sends it the touch
gesture (one finger held, a tap with another) and the mouse's right button
(`hs_android_right_button`). `hyperspace/` builds it (`linux.patch` on top of
the fork).

## Testing

- `FTL_TEST_CMD=/path/to/fifo ftl-linux` takes scripted input and screenshots
  (see the README).
- `native/build-harness.sh` builds `ftlrun`, a headless harness (Mesa's
  surfaceless EGL, guarded allocations) that runs the game natively on aarch64
  or under `qemu-aarch64` elsewhere. It takes commands from a FIFO (taps, drags,
  multi-finger gestures, screenshots, text, quit) and can record audio to a WAV
  file.
- `tools/` has Mach-O and Objective-C inspection scripts (`macho.py`,
  `machinfo.py`, `objcdump.py`, `imports.py`), the C++ symbol table generator
  (`gen_cxx_syms.py`) and `.ipa`/package helpers (`pkgextract.py`, `pkglist.py`).
