#!/bin/bash
# Build ftl-linux, the desktop app for aarch64 Linux (SDL3). Needs libcxx/build.sh linux first.
#   native/build-linux.sh             -> ~/ftl-build/linux/ftl-linux
#   PREFIX=~/.local native/build-linux.sh install   -> bin/ftl-linux, libexec/ftl-linux/{ftlman,Hyperspace*.so},
#                                                      share/applications/ftl-linux.desktop
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$HOME/ftl-build/linux}
LIBCXX=${LIBCXX_DIR:-$HOME/ftl-build/libcxx/linux}
FTLMAN=${FTLMAN:-$HOME/ftl-build/ftlman-linux/ftlman}
[ -x "$FTLMAN" ] || FTLMAN=
HYPERSPACE=${HYPERSPACE:-$HOME/ftl-build/hyperspace-linux/Hyperspace.1.6.21.arm64.so}
[ -f "$HYPERSPACE" ] || HYPERSPACE=
VERSION=${VERSION:-$(git -C "$HERE" describe --tags --always --dirty 2>/dev/null || echo dev)}
if [ ! -f "$LIBCXX/lib/libc++.a" ]; then
  echo "libc++ with Apple's ABI is missing: run libcxx/build.sh linux first" >&2
  exit 1
fi
cmake -G Ninja -S "$HERE" -B "$BUILD" \
  -DCMAKE_C_COMPILER=clang -DCMAKE_ASM_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="${PREFIX:-$HOME/.local}" \
  -DFTL_LINUX_VERSION="$VERSION" -DLIBCXX_DIR="$LIBCXX" -DFTLMAN_BIN="$FTLMAN" -DHYPERSPACE_SO="$HYPERSPACE" >/dev/null
ninja -C "$BUILD"
# ftl-linux finds ftlman next to itself, so the build folder works as it is
if [ -n "$FTLMAN" ]; then ln -sf "$FTLMAN" "$BUILD/ftlman"; else echo "note: no ftlman (ftlman/build-linux.sh): mods can't be applied"; fi
if [ -n "$HYPERSPACE" ]; then ln -sf "$HYPERSPACE" "$BUILD/$(basename "$HYPERSPACE")"; fi
if [ "${1:-}" = install ]; then ninja -C "$BUILD" install; fi
echo "built $BUILD/ftl-linux"
