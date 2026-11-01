#!/bin/bash
# Builds Hyperspace for ftl.nro, the Switch homebrew port, as a static library that
# switch/build.sh links in (Horizon has no dynamic loader).
#   hyperspace/build-switch.sh   -> ~/ftl-build/hyperspace-switch/{libHyperspace.a,deps/}
# The same fork and commit as hyperspace/build-linux.sh, with linux.patch and then
# switch.patch: an HS_IPAD_SWITCH target (static, no vcpkg), and code memory from the
# host: Horizon never makes code writable, so Hyperspace's hooks are written through
# the port's second, writable view of the game's code (see docs/switch/internals.md).
# Needs: switch/setup.sh, libcxx/build.sh switch, hyperspace/build-tools.sh (run here).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${HS_SRC:-$HOME/ftl-build/hs-src}
OUT=${OUT:-$HOME/ftl-build/hyperspace-switch}
TOOLS=${HS_TOOLS:-$HOME/ftl-build/hs-tools}
export DEVKITPRO=${DEVKITPRO:-$HOME/ftl-build/switch/devkitpro}
export FTL_LIBCXX=${FTL_LIBCXX:-$HOME/ftl-build/libcxx/switch}
export FTL_SWITCH_TOOLCHAIN=$HERE/../switch/toolchain.cmake
REPO=https://github.com/Sidd065/FTL-Hyperspace.git
COMMIT=d748e93
BOOST=1.87.0

[ -f "$FTL_LIBCXX/lib/libc++.a" ] || { echo "run libcxx/build.sh switch first" >&2; exit 1; }
[ -f "$DEVKITPRO/libnx/lib/libnx.a" ] || { echo "run switch/setup.sh first" >&2; exit 1; }
HS_TOOLS=$TOOLS "$HERE/build-tools.sh" >/dev/null
export PATH=$TOOLS/bin:$PATH
export LUA_CPATH="$TOOLS/lib/lua/5.3/?.so;;"

[ -d "$SRC/.git" ] || git clone -b android-arm64 "$REPO" "$SRC"
git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q -f "$COMMIT"
git -C "$SRC" reset -q --hard "$COMMIT"
git -C "$SRC" clean -fq -- .devcontainer
git -C "$SRC" apply "$HERE/linux.patch"
git -C "$SRC" apply "$HERE/switch.patch"

# Dependencies, built for the Switch: Boost's headers (only header-only parts are used),
# Lua 5.3 (the sources build-tools.sh fetched), minizip's headers (devkitPro's library)
DEPS=$OUT/deps
mkdir -p "$DEPS/include" "$DEPS/lib" "$DEPS/obj" "$OUT/src"
if [ ! -d "$DEPS/include/boost" ]; then
  T=boost_${BOOST//./_}
  [ -f "$OUT/src/$T.tar.bz2" ] || curl -fsSL -o "$OUT/src/$T.tar.bz2" "https://archives.boost.io/release/$BOOST/source/$T.tar.bz2"
  tar xf "$OUT/src/$T.tar.bz2" -C "$OUT/src" "$T/boost"
  mv "$OUT/src/$T/boost" "$DEPS/include/boost"
fi
L=$TOOLS/src/lua-5.3.6/src
cp "$L"/{lua.h,luaconf.h,lualib.h,lauxlib.h,lua.hpp} "$DEPS/include/"
cp -r "$DEVKITPRO/portlibs/switch/include/minizip" "$DEPS/include/"
NEWLIB=$DEVKITPRO/devkitA64/aarch64-none-elf
CF=(--target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -femulated-tls
    -ffunction-sections -fdata-sections -D__SWITCH__ -nostdinc -isystem "$(clang -print-resource-dir)/include"
    -isystem "$DEVKITPRO/libnx/include" -isystem "$NEWLIB/include" -O2 -fsigned-char)
rm -f "$DEPS"/obj/*.o
for f in "$L"/*.c; do
  b=$(basename "$f" .c)
  [ "$b" = lua ] || [ "$b" = luac ] || clang "${CF[@]}" -c "$f" -o "$DEPS/obj/$b.o"
done
rm -f "$DEPS/lib/liblua.a" && llvm-ar rcs "$DEPS/lib/liblua.a" "$DEPS"/obj/*.o

BUILD=$SRC/build-switch
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$SRC/.devcontainer/toolchains/switch-ftl.cmake" \
  -DHS_IPAD_SWITCH=ON -DHS_DEPS="$DEPS" -DCMAKE_BUILD_TYPE=Release \
  -DSWIG_EXECUTABLE="$TOOLS/bin/swig" -DHS_LUA="$TOOLS/bin/lua"
ninja -C "$BUILD"
cp "$BUILD/libHyperspace.a" "$OUT/libHyperspace.a"
ls -la "$OUT/libHyperspace.a"
