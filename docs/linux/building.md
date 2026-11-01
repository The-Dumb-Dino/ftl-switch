# Building ftl-linux

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

All build output goes to `~/ftl-build/` (outside the repository), and nothing
of the game is needed to build.

## Optional: Hyperspace

    hyperspace/build-linux.sh    # -> ~/ftl-build/hyperspace-linux/Hyperspace.1.6.21.arm64.so

It needs nothing from the package manager beyond the app's own build: Lua 5.3
with lpeg and lfs and swig 4.0 (what Hyperspace's code generator and Lua
bindings use) are built from source into `~/ftl-build/hs-tools` by
`hyperspace/build-tools.sh`, and vcpkg builds Boost, Lua and minizip the first
time (a while). How to install it: [mods.md](mods.md#hyperspace).

## Version string

`native/build-linux.sh` takes the version from `git describe` (so a tag like
`v1.0.0` names a release build), or from `VERSION=` if set. It shows in
`--info` and in the session logs.

## Other hosts

ftlman is upstream ftlman 0.7.4, unchanged, built with its `portable-release`
feature (settings and mods next to the program).

`libcxx/build.sh linux` and `native/build-harness.sh` build natively on an
aarch64 host; on other hosts they cross-compile against `SYSROOT` (default
`~/arm64-root`). How the pieces fit together: [../how-it-works.md](../how-it-works.md).
