#!/bin/bash
# Builds ftlman, the PC mod manager, as a native Linux program (with its window),
# for ftl-linux's portable folder and `ftl-linux --ftlman`.
#   ftlman/build-linux.sh   -> ~/ftl-build/ftlman-linux/ftlman
# Upstream ftlman at a fixed release, unchanged; only the "portable-release"
# feature, so it keeps its settings and mods next to itself. (The Android build,
# build.sh, needs headless.patch; Linux doesn't.) Needs a nightly Rust toolchain
# (rustup) and fontconfig (fontconfig-devel, or just the library: see below).
set -euo pipefail
SRC=${FTLMAN_SRC:-$HOME/ftl-build/ftlman-src}
OUT=${OUT:-$HOME/ftl-build/ftlman-linux}
REPO=https://github.com/afishhh/ftlman
COMMIT=7ad94795f9eff7f4bf005ea40d9bb2cf4d57ec88  # v0.7.4
CARGO=${CARGO:-$(command -v cargo || echo "$HOME/.cargo/bin/cargo")}

if [ ! -d "$SRC/.git" ]; then
  git clone "$REPO" "$SRC"
fi
git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q -f "$COMMIT"
git -C "$SRC" reset -q --hard "$COMMIT"  # drops the Android build's patch, if that tree was used for it

# ftlman links fontconfig (its window finds fonts with it) but needs no headers:
# without fontconfig-devel, point pkg-config at the installed library.
if ! pkg-config --exists fontconfig 2>/dev/null; then
  LIB=
  for l in /usr/lib64 /usr/lib/aarch64-linux-gnu /usr/lib; do
    if [ -f "$l/libfontconfig.so.1" ]; then LIB=$l/libfontconfig.so.1; break; fi
  done
  [ -n "$LIB" ] || { echo "fontconfig is missing (install fontconfig-devel)" >&2; exit 1; }
  PC=$OUT/.pkgconfig
  mkdir -p "$PC"
  ln -sf "$LIB" "$PC/libfontconfig.so"
  printf 'Name: fontconfig\nDescription: fontconfig\nVersion: 2.16\nLibs: -L%s -lfontconfig\nCflags:\n' "$PC" > "$PC/fontconfig.pc"
  export PKG_CONFIG_PATH=$PC${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
fi

# Linked for any page size (4K, 16K, 64K kernels).
export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-Wl,-z,max-page-size=65536"
export CARGO_TARGET_DIR=${CARGO_TARGET_DIR:-$SRC/target-linux}  # apart from the Android build's
(cd "$SRC" && "$CARGO" build --locked --release --features portable-release)

mkdir -p "$OUT"
cp "$CARGO_TARGET_DIR/release/ftlman" "$OUT/ftlman"
ls -la "$OUT/ftlman"
