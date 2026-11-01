#!/bin/bash
# Builds Hyperspace for ftl-linux: libHyperspace.so for FTL for iPad on aarch64 Linux.
#   hyperspace/build-linux.sh      -> ~/ftl-build/hyperspace-linux/Hyperspace.1.6.21.arm64.so
# The Android port's Hyperspace (github.com/Sidd065/FTL-Hyperspace, android-arm64) at a
# fixed commit, with linux.patch: a Linux target (HS_IPAD_LINUX, the arm64-linux-ftl
# vcpkg triplet) that builds the same iPad code paths with the host's clang and glibc.
# Needs: libcxx/build.sh linux, cmake, ninja, clang/lld, curl, zip/unzip/tar, and
# hyperspace/build-tools.sh (Lua with lpeg and lfs, swig), which this runs.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${HS_SRC:-$HOME/ftl-build/hs-src}
OUT=${OUT:-$HOME/ftl-build/hyperspace-linux}
TOOLS=${HS_TOOLS:-$HOME/ftl-build/hs-tools}
export FTL_LIBCXX=${FTL_LIBCXX:-$HOME/ftl-build/libcxx/linux}
REPO=https://github.com/Sidd065/FTL-Hyperspace.git
COMMIT=d748e93  # android-arm64: keep the picked item in custom stores after buying
BUILD_TYPE=${1:-Release}

[ -f "$FTL_LIBCXX/lib/libc++.a" ] || { echo "run libcxx/build.sh linux first" >&2; exit 1; }
HS_TOOLS=$TOOLS "$HERE/build-tools.sh" >/dev/null
export PATH=$TOOLS/bin:$PATH
export LUA_CPATH="$TOOLS/lib/lua/5.3/?.so;;"

if [ ! -d "$SRC/.git" ]; then
  git clone -b android-arm64 "$REPO" "$SRC"
fi
git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q -f "$COMMIT"
git -C "$SRC" reset -q --hard "$COMMIT"
git -C "$SRC" clean -fq -- .devcontainer  # files the patch adds, from an earlier run
git -C "$SRC" apply "$HERE/linux.patch"

# vcpkg, with our triplet and toolchain (as buildscripts/android/setup.sh does for Android)
VCPKG=$SRC/vcpkg
[ -d "$VCPKG/.git" ] || git clone -q https://github.com/microsoft/vcpkg.git "$VCPKG"
export VCPKG_FORCE_SYSTEM_BINARIES=1  # aarch64 hosts: the system's cmake and ninja
[ -x "$VCPKG/vcpkg" ] || (cd "$VCPKG" && ./bootstrap-vcpkg.sh -disableMetrics)
cp "$SRC/.devcontainer/toolchains/arm64-linux-ftl.cmake" "$VCPKG/scripts/toolchains/"
cp "$SRC/.devcontainer/triplets/arm64-linux-ftl.cmake" "$VCPKG/triplets/"

BUILD=$SRC/build-linux-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-linux-ftl \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG/scripts/toolchains/arm64-linux-ftl.cmake" \
  -DHS_IPAD_LINUX=ON -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSWIG_EXECUTABLE="$TOOLS/bin/swig" -DHS_LUA="$TOOLS/bin/lua"
ninja -C "$BUILD"

mkdir -p "$OUT"
# named like the PC builds (Hyperspace.1.6.13.amd64.so): the game version and the CPU
cp "$BUILD/libHyperspace.so" "$OUT/Hyperspace.1.6.21.arm64.so"
ls -la "$OUT/Hyperspace.1.6.21.arm64.so"
