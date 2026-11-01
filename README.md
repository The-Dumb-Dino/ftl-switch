# ftl-linux

`ftl-linux` runs your own copy of **FTL: Faster Than Light for iPad** (1.6.21)
natively on **aarch64 Linux**: Asahi Linux on Apple Silicon, Raspberry Pi 4/5,
Rockchip boards and so on.

It runs the real iOS game, unmodified: a Mach-O loader, an Objective-C runtime
and just enough of the iOS frameworks underneath it, with an SDL3 window, mouse
and keyboard, touchscreen and controller support on top. Mods work through
ftlman, the PC mod manager, and Hyperspace (Multiverse and other big mods)
is supported.

**Nothing of the game is in this repository**: you import it from your own
`.ipa`.

    ftl-linux --import FTL.ipa      # or just start it and pick the .ipa
    ftl-linux
    ftl-linux --ftlman              # mods, with ftlman
    ftl-linux --make-portable ~/Games/FTL FTL.ipa   # a folder that runs from anywhere

It was developed and tested on an Apple M1 MacBook with Fedora Asahi Remix
(16K-page kernel, KDE Plasma on Wayland), and also runs on 4K-page kernels.

This project is based on [ftl-android](https://github.com/Sidd065/ftl-android)
by Sidd065, whose loader, Objective-C runtime and iOS frameworks it runs on;
see [CREDITS.md](CREDITS.md) for what came from there and what's new. The same
port also runs on the Nintendo Switch as homebrew, in the companion
**ftl-switch** repository.

## Quick start

1. Build it ([docs/linux/building.md](docs/linux/building.md)): clang, CMake,
   SDL3, the LLVM 19.1.7 sources and a nightly Rust for ftlman.

       libcxx/build.sh linux
       ftlman/build-linux.sh
       native/build-linux.sh
       PREFIX=~/.local native/build-linux.sh install

2. Get your game in: FTL for iPad as an `.ipa` whose executable is decrypted
   (the App Store copy is FairPlay-encrypted and can't be used).

       ftl-linux --import ~/Downloads/FTL.ipa

3. Play: `ftl-linux`, or FTL from the app menu.

## Documentation

| | |
|---|---|
| [Building](docs/linux/building.md) | Dependencies, build steps, installing, Hyperspace |
| [Playing](docs/linux/usage.md) | Importing, portable folders, command line, paths, settings, controls, known limitations |
| [Mods and Hyperspace](docs/linux/mods.md) | ftlman, how mods' images are handled, installing Hyperspace |
| [How it works](docs/how-it-works.md) | The loader, the runtime, the frameworks, the hooks |
| [Testing and debugging](docs/testing.md) | Logs, scripted input, the headless harness |

## Reporting bugs

Please attach the session logs: `ftl-linux --paths` shows where they are. The
[bug report form](.github/ISSUE_TEMPLATE/bug_report.yml) says what else helps.

## License

GNU General Public License, version 3 ([LICENSE](LICENSE)). The code from
ftl-android is GPL version 3; what this project adds is also available under
GPL version 3 or any later version. Details, and the licenses of Hyperspace,
ftlman and the other components: [CREDITS.md](CREDITS.md).

FTL: Faster Than Light is made by Subset Games. This project isn't affiliated
with or endorsed by Subset Games or Apple.
