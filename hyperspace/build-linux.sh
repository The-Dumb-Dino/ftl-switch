#!/bin/bash
# Builds Hyperspace for ftl-linux: libHyperspace.so for FTL for iPad on aarch64 Linux.
#   hyperspace/build-linux.sh      -> ~/ftl-build/hyperspace-linux/Hyperspace.1.6.21.arm64.so
# The iPad game's Hyperspace (github.com/Sidd065/FTL-Hyperspace, branch android-arm64),
# which builds for Linux too: buildscripts/linux/ in it (HS_IPAD_LINUX, the
# arm64-linux-ftl vcpkg triplet: the same iPad code paths as Android, with the host's
# clang and glibc). HS_REPO and HS_REF pick another clone or branch.
# Needs: libcxx/build.sh linux, cmake, ninja, clang/lld, curl, zip/unzip/tar, and
# hyperspace/build-tools.sh (Lua with lpeg and lfs, swig), which this runs.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${HS_SRC:-$HOME/ftl-build/hs-src}
OUT=${OUT:-$HOME/ftl-build/hyperspace-linux}
TOOLS=${HS_TOOLS:-$HOME/ftl-build/hs-tools}
export FTL_LIBCXX=${FTL_LIBCXX:-$HOME/ftl-build/libcxx/linux}
REPO=${HS_REPO:-https://github.com/Sidd065/FTL-Hyperspace.git}
REF=${HS_REF:-android-arm64}
BUILD_TYPE=${1:-Release}

[ -f "$FTL_LIBCXX/lib/libc++.a" ] || { echo "run libcxx/build.sh linux first" >&2; exit 1; }
HS_TOOLS=$TOOLS "$HERE/build-tools.sh" >/dev/null
export PATH=$TOOLS/bin:$PATH
export LUA_CPATH="$TOOLS/lib/lua/5.3/?.so;;"

[ -d "$SRC/.git" ] || git clone "$REPO" "$SRC"
git -C "$SRC" fetch -q "$REPO" "$REF"
git -C "$SRC" checkout -q -f FETCH_HEAD
if [ ! -x "$SRC/buildscripts/linux/build.sh" ]; then
  echo "$REPO ($REF) has no Linux target (buildscripts/linux/): use HS_REPO/HS_REF" >&2
  exit 1
fi

"$SRC/buildscripts/linux/setup.sh" >/dev/null
"$SRC/buildscripts/linux/build.sh" "$BUILD_TYPE"

mkdir -p "$OUT"
# named like the PC builds (Hyperspace.1.6.13.amd64.so): the game version and the CPU
BUILD=$SRC/build-linux-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
cp "$BUILD/libHyperspace.so" "$OUT/Hyperspace.1.6.21.arm64.so"
ls -la "$OUT/Hyperspace.1.6.21.arm64.so"
