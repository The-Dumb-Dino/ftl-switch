#!/bin/bash
# Builds ftl.nro (Switch homebrew). Run switch/setup.sh once first.
#   switch/build.sh            -> ~/ftl-build/switch/build/ftl.nro
#   IPA=FTL.ipa switch/build.sh   the Homebrew Menu icon from your .ipa (made at build
#                                  time, never committed; tools/ipa_icon.py, needs Pillow)
#   FTL_SWITCH_ICON=icon.jpg switch/build.sh   or your own 256x256 JPEG
#   with Hyperspace when hyperspace/build-switch.sh has built it, and ftlman (mods on the
#   console) when switch/rust/build-ftlman.sh has
#   RELEASE=1 switch/build.sh   a release: with the built-in updater, which offers newer
#                                  releases of UPDATE_REPO at start. Needs a clean vX.Y.Z tag.
#                                  Other builds have no updater.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${SWITCH_ROOT:-$HOME/ftl-build/switch}
export DEVKITPRO=${DEVKITPRO:-$ROOT/devkitpro}
BUILD=${BUILD:-$ROOT/build}
LIBCXX=${LIBCXX_DIR:-$HOME/ftl-build/libcxx/switch}
VERSION=${VERSION:-$(git -C "$HERE" describe --tags --always --dirty 2>/dev/null || echo dev)}
export PATH=$ROOT/tools:$PATH  # elf2nro, nacptool
# The updater (release builds only): the repository whose GitHub releases it offers
UPDATE_REPO=
if [ "${RELEASE:-}" = 1 ]; then
  UPDATE_REPO=${UPDATE_REPO_OVERRIDE:-The-Dumb-Dino/ftl-switch}
  [[ $VERSION =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "RELEASE=1 needs a clean vX.Y.Z tag (this is $VERSION)" >&2; exit 1; }
  [ -f "$DEVKITPRO/portlibs/switch/lib/libcurl.a" ] || { echo "run switch/setup.sh again (curl for the updater)" >&2; exit 1; }
  echo "release $VERSION, updates from github.com/$UPDATE_REPO"
fi
[ -f "$DEVKITPRO/libnx/lib/libnx.a" ] || { echo "run switch/setup.sh first" >&2; exit 1; }
[ -f "$LIBCXX/lib/libc++.a" ] || "$HERE/../libcxx/build.sh" switch
# Hyperspace, if hyperspace/build-switch.sh built it (HYPERSPACE= to build without)
HS=${HYPERSPACE-$HOME/ftl-build/hyperspace-switch}
HS_ARGS=(-DFTL_SWITCH_HYPERSPACE= -DFTL_SWITCH_HS_DEPS=)
if [ -n "$HS" ] && [ -f "$HS/libHyperspace.a" ]; then
  HS_ARGS=(-DFTL_SWITCH_HYPERSPACE="$HS/libHyperspace.a" -DFTL_SWITCH_HS_DEPS="$HS/deps")
  echo "with Hyperspace from $HS"
fi
# ftlman (mods applied on the console), if switch/rust/build-ftlman.sh built it (FTLMAN= to leave it out)
FM=${FTLMAN-$ROOT/ftlman-switch/libftlman_switch.a}
HS_ARGS+=(-DFTL_SWITCH_FTLMAN=)
if [ -n "$FM" ] && [ -f "$FM" ]; then
  HS_ARGS+=(-DFTL_SWITCH_FTLMAN="$FM")
  echo "with ftlman from $FM"
fi
# The icon: FTL_SWITCH_ICON, else made from IPA (or an .ipa in $ROOT) into the build folder
ICON=${FTL_SWITCH_ICON:-}
if [ -z "$ICON" ]; then
  IPA=${IPA:-$(ls "$ROOT"/*.ipa 2>/dev/null | head -1 || true)}
  if [ -n "$IPA" ] && [ -f "$IPA" ]; then
    mkdir -p "$BUILD"
    if [ ! -f "$BUILD/icon.jpg" ] || [ "$IPA" -nt "$BUILD/icon.jpg" ]; then
      python3 "$HERE/../tools/ipa_icon.py" "$IPA" "$BUILD/icon.jpg" || rm -f "$BUILD/icon.jpg"
    fi
    [ -f "$BUILD/icon.jpg" ] && ICON=$BUILD/icon.jpg
  fi
  [ -n "$ICON" ] || echo "note: no icon (IPA=FTL.ipa switch/build.sh makes one from your .ipa)"
fi
cmake -G Ninja -S "$HERE/../native" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$HERE/toolchain.cmake" \
  -DFTL_SWITCH_APP=ON -DCMAKE_BUILD_TYPE=Release -DLIBCXX_DIR="$LIBCXX" \
  -DFTL_SWITCH_VERSION="$VERSION" -DFTL_SWITCH_ICON="$ICON" -DFTL_SWITCH_UPDATE_REPO="$UPDATE_REPO" \
  "${HS_ARGS[@]}" >/dev/null
ninja -C "$BUILD"
echo "built $BUILD/ftl.nro: copy it to sdmc:/switch/ftl/ftl.nro"
