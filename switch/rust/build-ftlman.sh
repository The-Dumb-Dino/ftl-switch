#!/bin/bash
# Builds ftlman's patch engine for ftl.nro (mods from /switch/ftl/mods/ applied on the
# console): a static library with one C function,
# ftlman_patch (ftlman/switch.patch). switch/build.sh links it in.
#   switch/rust/build-ftlman.sh   -> ~/ftl-build/switch/ftlman-switch/libftlman_switch.a
#
# Rust's std has no Switch target, but it supports the 3DS's Horizon with newlib, the
# same C library ftl.nro uses: aarch64-nintendo-switch-ftl.json is that for aarch64,
# std is built from source (-Zbuild-std) with libc patched for it (sizes measured
# from devkitPro's headers), and lua-src and getrandom learn the target.
# Needs: rustup, switch/setup.sh. The nightly is pinned: std's libc version must
# match libc-0.2.191-switch.patch.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TOOLCHAIN=${RUST_TOOLCHAIN:-nightly}  # checked below: std must use libc 0.2.191 (the 2026-10-28 nightly does)
ROOT=${SWITCH_ROOT:-$HOME/ftl-build/switch}
SRC=${FTLMAN_SRC:-$HOME/ftl-build/ftlman-src}
OUT=${OUT:-$ROOT/ftlman-switch}
REPO=https://github.com/afishhh/ftlman
COMMIT=7ad94795f9eff7f4bf005ea40d9bb2cf4d57ec88  # v0.7.4
export PATH=$HOME/.cargo/bin:$PATH

rustup toolchain list | grep -q "^$TOOLCHAIN" || rustup toolchain install "$TOOLCHAIN" --profile minimal
rustup component add rust-src --toolchain "$TOOLCHAIN" >/dev/null
SYSROOT=$(rustc +"$TOOLCHAIN" --print sysroot)
LIBC_VER=$(grep -A1 '^name = "libc"' "$SYSROOT/lib/rustlib/src/rust/library/Cargo.lock" | sed -n 's/version = "\(.*\)"/\1/p' | head -1)
[ "$LIBC_VER" = 0.2.191 ] || { echo "std uses libc $LIBC_VER; libc-0.2.191-switch.patch needs 0.2.191 (RUST_TOOLCHAIN=$TOOLCHAIN)" >&2; exit 1; }

# std's sources, with libc for the Switch
STD=$ROOT/rust-src/library
CRATES=$ROOT/rust-crates
mkdir -p "$CRATES"
REG=$(ls -d "$HOME"/.cargo/registry/src/index.crates.io-* 2>/dev/null | head -1)
fetch_crate() {  # name version -> $CRATES/<dir>, patched
  local dir=$CRATES/$3
  rm -rf "$dir"
  if [ ! -d "$REG/$1-$2" ]; then
    mkdir -p "$ROOT/crate-dl" && curl -fsSL "https://static.crates.io/crates/$1/$1-$2.crate" | tar xz -C "$ROOT/crate-dl"
    cp -r "$ROOT/crate-dl/$1-$2" "$dir"
  else
    cp -r "$REG/$1-$2" "$dir"
  fi
  chmod -R u+w "$dir"
  patch -s -d "$dir" -p1 < "$HERE/$1-$2-switch.patch"
}
fetch_crate libc 0.2.191 libc
fetch_crate lua-src 547.0.0 lua-src
fetch_crate getrandom 0.2.16 getrandom-0.2.16
fetch_crate getrandom 0.3.3 getrandom-0.3.3
rm -rf "$ROOT/rust-src" && mkdir -p "$ROOT/rust-src" && cp -r "$SYSROOT/lib/rustlib/src/rust/library" "$STD"
printf '\n# ftl.nro: libc with the Switch (Horizon on aarch64, devkitPro newlib)\nlibc = { path = %s }\n' "'$CRATES/libc'" >> "$STD/Cargo.toml"

# ftlman: that release, headless (headless.patch), plus the Switch entry (switch.patch)
[ -d "$SRC/.git" ] || git clone "$REPO" "$SRC"
git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q -f "$COMMIT"
git -C "$SRC" reset -q --hard "$COMMIT"
git -C "$SRC" clean -fdq -e 'target*'
git -C "$SRC" apply "$HERE/../../ftlman/headless.patch"
git -C "$SRC" apply "$HERE/../../ftlman/switch.patch"
ln -sfn "$CRATES" "$SRC/switch-crates"
(cd "$SRC" && cargo +"$TOOLCHAIN" update -q -p libc 2>/dev/null || true)

. "$HERE/env.sh"
export RUST_STD_SRC=$STD __CARGO_TESTS_ONLY_SRC_ROOT=$STD
export CARGO_TARGET_DIR=$ROOT/ftlman-target
(cd "$SRC" && cargo +"$TOOLCHAIN" build -Zbuild-std=std,panic_abort -Zjson-target-spec --release --lib \
   --no-default-features --target "$SWITCH_TARGET")
mkdir -p "$OUT"
cp "$CARGO_TARGET_DIR/aarch64-nintendo-switch-ftl/release/libftlman_switch.a" "$OUT/"
ls -la "$OUT/libftlman_switch.a"
