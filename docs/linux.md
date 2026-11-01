# ARM Linux

`ftl-linux` runs your own copy of FTL for iPad (1.6.21) natively on aarch64
Linux: Asahi Linux on Apple Silicon, Raspberry Pi 4/5, Rockchip boards and so
on. It is the same port as the Android app (the Mach-O loader, the
Objective-C runtime and the iOS frameworks underneath the game), with an SDL3
frontend instead of the Android activity. Nothing of the game is in this
repository: you import it from your own `.ipa`.

It was developed and tested on an Apple M1 MacBook with Fedora Asahi Remix
(16K-page kernel, KDE Plasma on Wayland), and also runs on 4K-page kernels.

## Building

You need clang/lld, CMake, Ninja, SDL3, the EGL and GLES development files,
zlib, Python 3, `patch`, the LLVM 19.1.7 sources, and a nightly Rust toolchain
for ftlman (the mod manager; it links fontconfig, which every desktop has, and
needs no -devel package for it). On Fedora:

    sudo dnf install clang lld llvm cmake ninja-build python3 patch \
        SDL3-devel mesa-libEGL-devel mesa-libGLES-devel zlib-ng-compat-devel rustup
    rustup-init -y --default-toolchain nightly --profile minimal
    mkdir -p ~/src && cd ~/src
    curl -LO https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/llvm-project-19.1.7.src.tar.xz
    tar xf llvm-project-19.1.7.src.tar.xz

On Debian/Ubuntu/Raspberry Pi OS the packages are `clang lld cmake ninja-build
python3 patch libsdl3-dev libegl-dev libgles-dev zlib1g-dev` (SDL3 is in Debian
13 and Ubuntu 25.04 or newer; otherwise build SDL3 from source).

Then, from the repository:

    libcxx/build.sh linux           # libc++ with Apple's arm64 ABI -> ~/ftl-build/libcxx/linux
    ftlman/build-linux.sh           # ftlman, the mod manager      -> ~/ftl-build/ftlman-linux/ftlman
    native/build-linux.sh           # the app                      -> ~/ftl-build/linux/ftl-linux
    PREFIX=~/.local native/build-linux.sh install

`install` puts `ftl-linux` in `$PREFIX/bin`, ftlman in
`$PREFIX/libexec/ftl-linux/` and a desktop entry in
`$PREFIX/share/applications/`. The build folder works as it is too
(`~/ftl-build/linux/ftl-linux`, with ftlman linked next to it).

ftlman is upstream ftlman 0.7.4, unchanged, built with its `portable-release`
feature (settings and mods next to the program). The Android build's
`headless.patch` isn't used on Linux: ftlman's own window installs the mods.

`libcxx/build.sh linux` and `native/build-harness.sh` build natively on an
aarch64 host; on other hosts they cross-compile against `SYSROOT` (default
`~/arm64-root`) as before.

## Getting the game in

You need FTL for iPad as an `.ipa` (or the extracted `FTL.app`) whose arm64
executable is decrypted, as for the Android app. Version 1.6.21 gets all
features; other builds run without the extras that hook the game's code.

    ftl-linux --import ~/Downloads/FTL.ipa
    ftl-linux --info                # FTL for iPad 1.6.21, the 1.6.21 build (all features)
    ftl-linux

Or just start `ftl-linux` (or FTL from the app menu): without a game it asks
for the `.ipa` with a file chooser (kdialog on KDE, else zenity) and imports it.

`--import` also takes an `FTL.app` folder or a folder holding `Payload/FTL.app`.
It copies `FTL`, `ftl.dat` and `Info.plist` and refuses a FairPlay-encrypted
executable. Importing again replaces the copy, mods included.

## A portable folder

    ftl-linux --make-portable ~/Games/FTL ~/Downloads/FTL.ipa

makes a folder with everything in it, which runs from wherever it's put (another
disk, a USB stick, another account on an aarch64 Linux with SDL3):

    FTL/
      ftl-linux        the game (double-click FTL.desktop, or run it)
      mod-manager      ftlman for this folder's game
      FTL.desktop      a launcher that finds the folder itself; right-click for the mod manager
      portable         the marker: with it, ftl-linux keeps everything in its own folder
      game/            FTL, ftl.dat, Info.plist from the .ipa (ftlman adds ftl.dat.vanilla)
      ftlman/          ftlman, its settings.json and mods/
      data/            settings.ini, saves/, logs/, cache/ (made on the first start)
      README.txt

Without the `.ipa` the game already imported is copied in (its unmodded
`ftl.dat`). Running `--make-portable` again on the same folder updates
`ftl-linux` and ftlman and keeps the game, saves and mods; with an `.ipa` it
imports the game again. A folder moved elsewhere keeps working: ftl-linux finds
its files next to itself, and points ftlman at the folder's game each time the
mod manager starts. Don't share a folder with `game/` in it: that's your copy of
the game.

## Command line

| Command | |
|---|---|
| `ftl-linux` | Play |
| `ftl-linux --import <path>` | Import the game (`.ipa`, `FTL.app`, or a folder with `Payload/FTL.app`) |
| `ftl-linux --info` | The imported game's version |
| `ftl-linux --paths` | Where the game, saves, settings and logs are |
| `ftl-linux --ftlman` | The mod manager (ftlman), set up for this game |
| `ftl-linux --make-portable <folder> [FTL.ipa]` | A portable folder (see above) |
| `--fullscreen`, `--windowed` | Override the setting for this start |
| `--verbose` | More detail in the log, and the log on stderr |

## Paths

Installed, the XDG base directories are honoured (defaults in brackets). In a
portable folder everything is in the folder (see above).

| What | Where |
|---|---|
| Imported game (`FTL`, `ftl.dat`, `Info.plist`) | `$XDG_DATA_HOME/ftl-linux/game` (`~/.local/share/...`) |
| Saves and the game's own settings | `$XDG_DATA_HOME/ftl-linux/saves` |
| ftlman (a copy), its settings and `mods/` | `$XDG_DATA_HOME/ftl-linux/ftlman` |
| Settings of the port | `$XDG_CONFIG_HOME/ftl-linux/settings.ini` (`~/.config/...`) |
| Session logs, crash marker | `$XDG_STATE_HOME/ftl-linux` (`~/.local/state/...`) |
| Cache (the mod image overlay) | `$XDG_CACHE_HOME/ftl-linux` |

The last five session logs are kept. If the game crashed, the next start says
so in the log and starts at the title screen instead of continuing the run.

## Settings

`settings.ini` is written with comments the first time the game starts. Delete
a line to get its default.

| Key | Default | |
|---|---|---|
| `fullscreen` | `false` | Start in fullscreen (F11 or Alt+Enter switch while playing) |
| `window_width`, `window_height` | `0` | Window size in desktop units; 0: 80% of the screen, with the screen's shape |
| `vsync` | `true` | |
| `render_height` | `0` | 0: automatic (1536 pixels on windows taller than 900, else 768); or `768`, `1536` |
| `pause_on_focus_loss` | `true` | Pause when another window gets the focus (minimizing always pauses) |
| `magnifier` | `auto` | The loupe over your finger. `auto`: off with a mouse, on with a touchscreen |
| `pinch_zoom` | `auto` | Two fingers zoom and pan. The mouse wheel always zooms |
| `auto_zoom` | `auto` | Zoom in on the beacon map and the upgrade screen |
| `event_zoom` | `false` | Zoom in on events too |
| `minimap` | `true` | Shows where you are while zoomed in |
| `two_finger_pause` | `true` | A two-finger tap pauses (touchscreens) |
| `event_text` | `0` | Event text size: 0 the game's, 1 large, 2 larger |
| `resume_run` | `false` | Continue the saved run at launch |
| `pad_magnetism`, `pad_rumble`, `pad_touchpad` | `true` | Controller cursor pull, rumble, DualSense/DualShock touchpad |
| `pad_<button>` | see below | Controller buttons (`a b x y lb rb lt rt l3 r3 start select home`) |
| `text_dialog` | `auto` | Text prompts (renaming the ship): `zenity`, `kdialog`, or `window` |
| `ftlman` | | Path to ftlman, if it isn't in `ftlman/` next to `ftl-linux`, next to it, or in `libexec` |
| `hyperspace_lib` | | A Linux build of Hyperspace (see below) |

Controller actions for `pad_<button>`: `none tap right_click back pause zoom_in
zoom_out reset_zoom magnifier turbo precision`. The defaults are the Android
app's: A tap, B and Start back, X pause, LB/RB zoom out/in, LT magnifier, RT
right click, Select and R3 reset the zoom.

## Playing with a mouse and keyboard

The iPad interface is made for touch, so the left mouse button acts as a
finger: click to tap, hold and drag to drag. On top of that:

| Input | |
|---|---|
| Mouse wheel (or two-finger scroll on a touchpad) | Zoom in and out at the pointer |
| Middle button, held | Drag the zoomed-in view around |
| Right button | Back: close the window or open the game menu (a right click for mods that listen for one) |
| Escape | Close the window or open the menu |
| F11, Alt+Enter | Fullscreen on/off |
| The PC version's keys | Space pause, J jump, 1–8 weapons, A/S/D/F/W... systems, Shift removes power, F1–F8 crew, V autofire, Z/X doors, U/C/I ship screens, T/R teleporter, L lockdown |

Hotkeys show in tooltips once a key has been pressed. Closing the window saves
and quits like iOS does when an app ends; minimizing the window pauses the game.

The game draws its canvas 768 units tall and as wide as the window's shape, at
1536 pixels on big windows, so it is sharp on HiDPI displays (the window is
created at its real pixel size). The shape is fixed when the game starts: the
default window has the screen's shape so fullscreen fills the screen. A window
resized to another shape shows the game with bars.

## Touchscreens and controllers

Touchscreens get the Android app's gestures: pinch and two-finger drag to zoom
and pan, a two-finger tap pauses, and for mods that use one, one finger held and
a tap with another is a right click. Once a touchscreen is used, the touch helpers set to `auto` switch
on, and a mouse click switches them back off.

Controllers go through SDL3's gamepad support (Xbox, PlayStation, Switch Pro,
8BitDo and others SDL knows), with the same cursor and buttons as on Android;
see the README's controller table. So far this was tested with SDL's virtual
controller (stick, buttons, menu navigation), not with a physical one.

## Mods

Mods are installed with [ftlman](https://github.com/afishhh/ftlman), the PC
mod manager, in its own window:

    ftl-linux --ftlman              # or ./mod-manager in a portable folder,
                                    # or "Mod Manager" in FTL's launcher menu

Put `.ftl`/`.zip` mods in its `mods/` folder (`--paths` shows where), tick
them, press **Apply**, close ftlman and start the game. ftlman patches the
game's `ftl.dat` in place and keeps the original as `ftl.dat.vanilla`, exactly
as with FTL on PC; applying with nothing ticked puts the original back.

The game notices on its own: at launch, when `ftl.dat` changed, ftl-linux looks
at it and, if mods added images (the iPad build keeps small images in atlases),
writes the atlas lists without them into the cache, so the mods' images show
(`ux/mods.c`). After `ftl.dat` changes the game starts at the title screen,
since a run saved with other mods may not load.

`--ftlman` gives ftlman settings for this port the first time: the game's
folder as FTL's, and its Hyperspace installer and self-update off (both are
for the PC game and ftlman's PC builds). If you also use ftlman for FTL on PC
and have `~/.config/ftlman/settings.json`, ftlman uses that instead; point its
FTL directory at the game folder then.

Tested: FTL Event Tags and the KRS Obsidian Cruiser (both from the README's
list) apply together, and a run with them goes through sector 1 into sector 2.
Mod text in Windows-1252 rather than UTF-8 is up to ftlman now (the Android
app converts it before ftlman sees it).

**Hyperspace** (needed by Multiverse and other big mods) is not part of the
Linux build yet. The port's side is ready: `hyperspace.c` loads the library with
`dlopen` and calls `hs_android_init` (`hyperspace_host.h`) whenever
`hyperspace_lib` in settings.ini points to one and the applied mods include
Hyperspace; without it the game warns once after such mods were applied.
Building it means porting the `android-arm64` branch of FTL-Hyperspace to an
`aarch64-linux-gnu` target: a vcpkg triplet and toolchain like
`.devcontainer/{triplets,toolchains}/arm64-android-ftl.cmake` that use the host
clang and `libcxx/build.sh linux`'s libc++ instead of the NDK, the
`HS_ANDROID` code paths enabled for it (they're about the iPad game, not
Android, apart from logging in `src/android/AndroidHost.cpp`), and the build
tools its generator needs (swig, Lua 5.3 with lpeg and lfs).

## Page sizes

The Mach-O loader copies the game's segments into anonymous memory and sets
protections per host page, so it runs on 4K, 16K and 64K kernels. On 64K
kernels the page holding the end of `__TEXT` and the start of `__DATA` gets both
permissions (RWX), as the game's code addresses its data PC-relatively and the
segments can't be moved apart. The ELF parts are linked with
`max-page-size=65536`. Executable memory goes through `loader/execmem.h`
(allocate, protect, separate write/execute views), for hosts that need W^X
mappings.

Tested on the 16K Asahi kernel, on a 4K kernel (Asahi's `muvm` micro-VM), and
with `FTL_PAGESIZE=65536`, which makes the loader act as if pages were 64K.

## Testing and debugging

- `FTL_TEST_CMD=/path/to/fifo ftl-linux` takes commands on a FIFO, in window
  pixels: `click x y`, `rclick x y`, `down/up/move x y`, `wheel x y steps`,
  `key <Android key code>`, `text <reply to text prompts>`, `shot file.png`,
  `fullscreen`, `pause`, `resume`, `quit`, and a virtual controller:
  `padattach`, `padbtn <SDL button> 0|1`, `padaxis <SDL axis> <value>`;
  `audio` logs the output's peak level. With the screen locked, add
  `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` for the controller commands.
- `FTL_LOG_STDERR=1` (or `--verbose`) copies the log to stderr.
- `native/build-harness.sh` still builds the headless harness (surfaceless EGL),
  which now runs natively on aarch64.

## Known limitations

- **Hovering** shows nothing: the iPad build has no mouse-over tooltips, so
  information shows when you tap, as on the iPad.
- **Window shape**: the canvas is fixed at start; resizing to another shape
  shows bars. Restart to use the new shape.
- **Text prompts** use zenity or kdialog when installed and not in fullscreen;
  otherwise you type into the window (the title bar shows the text).
- **Fractional scaling**: with a fractional scale the window can be a pixel off
  the screen's size (2561×1601 on a 2560×1600 panel at 150%); the game is scaled
  by that pixel.
- **Hyperspace** isn't built for Linux yet (see Mods).
- Only the iPad 1.6.21 build gets the hooks (keys, mods' images, pause gesture);
  other builds run without them, as on Android.
- No Flatpak or AppImage yet (the portable folder is the self-contained way for now).
