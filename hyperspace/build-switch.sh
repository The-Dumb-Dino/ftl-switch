#!/bin/bash
# Builds Hyperspace for ftl.nro, the Switch homebrew port, as a static library that
# switch/build.sh links in (Horizon has no dynamic loader).
#   hyperspace/build-switch.sh   -> ~/ftl-build/hyperspace-switch/{libHyperspace.a,deps/}
# The same Hyperspace as hyperspace/build-linux.sh (source.sh), whose buildscripts/switch/
# builds the HS_IPAD_SWITCH target (static, no vcpkg) with code memory from the host:
# Horizon never makes code writable, so Hyperspace's hooks are written through the
# port's second, writable view of the game's code (see docs/switch/internals.md).
# HS_REPO and HS_REF pick another clone, branch or commit.
# Needs: switch/setup.sh, libcxx/build.sh switch, hyperspace/build-tools.sh (run here).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${HS_SRC:-$HOME/ftl-build/hs-src}
OUT=${OUT:-$HOME/ftl-build/hyperspace-switch}
TOOLS=${HS_TOOLS:-$HOME/ftl-build/hs-tools}
export DEVKITPRO=${DEVKITPRO:-$HOME/ftl-build/switch/devkitpro}
export FTL_LIBCXX=${FTL_LIBCXX:-$HOME/ftl-build/libcxx/switch}
export FTL_SWITCH_TOOLCHAIN=$HERE/../switch/toolchain.cmake
. "$HERE/source.sh"

[ -f "$FTL_LIBCXX/lib/libc++.a" ] || { echo "run libcxx/build.sh switch first" >&2; exit 1; }
[ -f "$DEVKITPRO/libnx/lib/libnx.a" ] || { echo "run switch/setup.sh first" >&2; exit 1; }
HS_TOOLS=$TOOLS "$HERE/build-tools.sh" >/dev/null
export PATH=$TOOLS/bin:$PATH
export LUA_CPATH="$TOOLS/lib/lua/5.3/?.so;;"

hs_checkout
if [ ! -x "$SRC/buildscripts/switch/build.sh" ]; then
  echo "$REPO ($REF) has no Switch target (buildscripts/switch/): use HS_REPO/HS_REF" >&2
  exit 1
fi

"$SRC/buildscripts/switch/build.sh"

mkdir -p "$OUT/deps"
cp "$SRC/build-switch-release/libHyperspace.a" "$OUT/libHyperspace.a"
rm -rf "$OUT/deps/lib" && cp -r "$SRC/build-switch-deps/lib" "$OUT/deps/lib"
ls -la "$OUT/libHyperspace.a"
