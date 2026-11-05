# Building ftl.nro

The Switch build uses the host's clang and lld, not devkitPro's gcc: devkitPro
has no compiler for aarch64 Linux hosts, and the game's C++ library has to be
built with clang anyway (Apple's C++ ABI). devkitPro's *target* packages
(libnx, newlib, mesa, zlib) are built for the Switch and work with any host, so
they're used as they are.

You need clang/lld, CMake, Ninja, Python 3, `patch` and the LLVM 19.1.7
sources (for libc++), plus a nightly Rust toolchain for ftlman. On Fedora:

    sudo dnf install clang lld llvm cmake ninja-build python3 python3-pillow patch rustup
    rustup-init -y --default-toolchain nightly --profile minimal
    mkdir -p ~/src && cd ~/src
    curl -LO https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/llvm-project-19.1.7.src.tar.xz
    tar xf llvm-project-19.1.7.src.tar.xz

On Debian/Ubuntu the packages are `clang lld llvm cmake ninja-build python3
python3-pil patch`, with Rust from [rustup](https://rustup.rs/).

ftlman (`build-ftlman.sh` adds `rust-src`; std must use libc 0.2.191,
which the script checks). Then:

    switch/setup.sh             # once: devkitPro's target packages, elf2nro, nacptool
    libcxx/build.sh switch      # libc++ with Apple's ABI, for newlib + libnx
    hyperspace/build-switch.sh  # optional: Hyperspace, linked in when built
    switch/rust/build-ftlman.sh # optional: ftlman's patch engine, linked in when built
    IPA=FTL.ipa switch/build.sh # -> ~/ftl-build/switch/build/ftl.nro

All output goes to `~/ftl-build/` (outside the repository).
`HYPERSPACE= switch/build.sh` builds without Hyperspace, `FTLMAN=` without
ftlman (then mods can only be patched on a PC).

## The icon

`ftl.nro` carries the game's own icon for the Homebrew Menu, `switch/icon.jpg`
(Subset Games allows using FTL's art). It was made from the `.ipa` with
`tools/ipa_icon.py FTL.ipa switch/icon.jpg`, which takes the largest icon in the
app's asset catalog (the 1024×1024 App Store icon), decodes it (Apple's LZVN
compression, in Python) and scales it to the 256×256 JPEG an `.nro` carries
(Pillow). `FTL_SWITCH_ICON=icon.jpg switch/build.sh` uses another JPEG.

## Version string

`switch/build.sh` takes the version from `git describe` (so a tag like `v1.0.0`
names a release build), or from `VERSION=` if set. The Homebrew Menu shows it
and the logs carry it.

## Releases and the updater

    RELEASE=1 switch/build.sh

builds a release: the same `ftl.nro` plus the built-in updater, which offers
newer releases of
[The-Dumb-Dino/ftl-switch](https://github.com/The-Dumb-Dino/ftl-switch) at start
(see [playing.md](playing.md#updates)). It needs a clean `vX.Y.Z` tag on the
commit, and curl from `switch/setup.sh`. Other builds have no updater, so
self-built and forked versions never offer this repository's releases
(`UPDATE_REPO_OVERRIDE=owner/name` points a release build at another one).

To publish one: tag the commit (`v0.5.0`), build with `RELEASE=1`, and attach
the result to a GitHub release of that tag as an asset named exactly
`ftl.nro`. The updater takes the latest release (not drafts or pre-releases),
compares its tag with its own version, and checks the download against the
size and SHA-256 GitHub lists for the asset.

## Testing in Ryujinx

Ryujinx (1.3.3, the Ryubing builds, including aarch64 Linux) runs `ftl.nro`
with `prod.keys` from your own console in `~/.config/Ryujinx/system/`; no
firmware is needed. Put `ftl.nro` and the `.ipa` in its emulated SD card,
`~/.config/Ryujinx/sdcard/switch/ftl/`. On a 16K-page kernel (Asahi) set
Options → Settings → System → Memory Manager Mode to "Software", and use the
OpenGL backend.

Ryujinx is more forgiving than a console: it doesn't enforce memory permissions
of the `.nro`'s segments, nor the GPU driver's memory limits. A build that runs
there can still crash on a console; see [how-it-works.md](../how-it-works.md#relocations-and-read-only-memory).

Logs reach Ryujinx's own log (as `OutputDebugString`), and the `screenshot`
file (see [playing.md](playing.md#problems)) works there too.

## Crash addresses

Crash backtraces in the session log name the game's frames as `FTL+<address>`
(as a disassembler shows the 1.6.21 binary) and the port's as
`ftl.elf+<offset>`, for `llvm-symbolizer --obj=ftl.elf` on the build's
`~/ftl-build/switch/build/ftl.elf`.
