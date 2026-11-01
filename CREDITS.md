# Credits

## FTL

FTL: Faster Than Light is made by [Subset Games](https://subsetgames.com/).
This project contains nothing of the game: it runs your own copy of FTL for
iPad, which you import from your own `.ipa`. It isn't affiliated with or
endorsed by Subset Games, Apple or Nintendo.

## Based on ftl-android

This project is based on [ftl-android](https://github.com/Sidd065/ftl-android)
by **Sidd065**, which runs FTL for iPad on Android. It started as a fork of
ftl-android 0.4.3 (commit `55ee45d`) and has been reworked extensively since:
all of its Android code (the Android app, its Java/JNI layer, the Android
platform code and build) was removed, and what remains was adapted to new
platforms. The git history keeps every upstream commit with its author.

From ftl-android, adapted for this project:

- the Mach-O loader (`native/src/loader/`)
- the Objective-C runtime with Apple's ABI (`native/src/objc/`)
- the Darwin C library layer (`native/src/darwin/`)
- the iOS framework stand-ins: UIKit, Foundation, QuartzCore/EAGL, audio
  (`native/src/fw/`)
- the touch and UX layer: layout, zoom, magnifier, gestures, controller cursor,
  mods' images (`native/src/ux/`)
- the libc++ build with Apple's ABI (`libcxx/`), the headless test harness
  (`native/src/platform/plat_linux.c`), the debugging aids
  (`native/src/debug/`) and most of the inspection scripts in `tools/`

New or changed in this project:

- the Nintendo Switch homebrew port, `ftl.nro` (`native/src/platform/switch/`,
  `switch/`): the clang/lld toolchain for libnx, code memory under Horizon,
  touch, Joy-Cons, the keyboard and error applets, mods applied on the console
  with ftlman's engine (`switch/rust/`, `ftlman/switch.patch`), Hyperspace
  linked in (its Switch target in the iPad fork, `buildscripts/switch/` there), the icon from your `.ipa`
  (`tools/ipa_icon.py`)
- the Linux desktop app, `ftl-linux` (`native/src/platform/linux/`): SDL3
  window, EGL/GLES 3, mouse, keyboard, touchscreen and controller input, audio,
  dialogs, XDG paths, settings.ini, session logs, crash reports, importing the
  game from an `.ipa`, portable folders
- native builds on aarch64 Linux (libc++, the app, the headless harness)
- a loader that works with any host page size (4K, 16K, 64K) and keeps
  executable memory behind one interface (`loader/execmem.h`)
- mods with stock ftlman as a native Linux program (`ftlman/`)
- Hyperspace for Linux: its Linux target in the iPad fork (`buildscripts/linux/`
  there), the build scripts here, and the host side starting it like a drop-in
- removal of the Android app and its code, and of code nothing calls any more
- the documentation in `docs/`

## Other software

| Component | Used for | License |
|---|---|---|
| [Hyperspace](https://github.com/FTL-Hyperspace/FTL-Hyperspace), iPad fork by [Sidd065](https://github.com/Sidd065/FTL-Hyperspace); built from the [`iOS-binary-ports`](https://github.com/The-Dumb-Dino/FTL-Hyperspace-Dino/tree/iOS-binary-ports) branch of [FTL-Hyperspace-Dino](https://github.com/The-Dumb-Dino/FTL-Hyperspace-Dino), which adds the Linux and Switch targets | Optional mod loader, built from source (`hyperspace/source.sh`) | GPL-3.0 |
| [ftlman](https://github.com/afishhh/ftlman) by afishhh | Linux: the mod manager, run as its own program. Switch: its patch engine, linked into `ftl.nro` | GPL-2.0 (`ftlman/LICENSE`); see the note below |
| [LLVM](https://llvm.org/) libc++, libc++abi, libunwind 19.1.7 | The game's C++ runtime, patched (`libcxx/`) and linked in | Apache-2.0 WITH LLVM-exception |
| [stb_image](https://github.com/nothings/stb) by Sean Barrett | PNG decoding (`native/src/third_party/`) | MIT or public domain |
| [SDL3](https://libsdl.org/) | Window, input, audio (the system's) | zlib |
| [zlib](https://zlib.net/) | `ftl.dat` and `.ipa` reading (the system's) | zlib |
| Boost, Lua, minizip (via vcpkg) | Hyperspace's dependencies | BSL-1.0, MIT, zlib |
| [libnx](https://github.com/switchbrew/libnx), devkitPro's newlib, mesa, zlib | The Switch build's system libraries, linked in | ISC; BSD-style; MIT; zlib |
| The Rust standard library and ftlman's crates | ftlman's engine on the Switch | MIT / Apache-2.0 and others |

## License

Copyright © 2026 Sidd065 and the contributors of this project.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, version 3 (see [LICENSE](LICENSE)).

- ftl-android, and with it the code this project took from it, is licensed
  under the GPL, version 3.
- The changes and new files this project adds to it (the commits after
  ftl-android 0.4.3) are licensed under the GPL, version 3 **or (at your
  option) any later version**.

Since the ftl-android parts don't carry "or later", the project as a whole is
distributed under version 3 of the GPL.

**ftlman in `ftl.nro`.** ftlman ships the text of the GPL version 2. On Linux
it runs as a separate program; on the Switch its patch engine is linked into
`ftl.nro` together with GPL-3.0 code (this project and Hyperspace), which
needs ftlman under GPL version 2 "or later". It is used here on the
understanding that its author, afishhh, agrees; his written confirmation is
still pending. If that changes, `FTLMAN= switch/build.sh` builds `ftl.nro`
without it (mods are then patched on a PC). This repository itself only holds
patches and build scripts for ftlman, not its code.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
