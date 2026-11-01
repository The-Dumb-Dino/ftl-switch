#!/bin/bash
# One-time setup for building ftl.nro with the host's clang/lld (see switch/README.md):
#  - devkitPro's target packages (libnx, newlib, mesa, zlib) into ~/ftl-build/switch/devkitpro.
#    These are built for the Switch and don't depend on the host, so this works where
#    devkitPro has no compiler (aarch64 Linux). Only their target files are used.
#  - elf2nro and nacptool from switch-tools, built for the host.
# Needs curl, tar with zstd/xz, git, cc.
set -euo pipefail
ROOT=${SWITCH_ROOT:-$HOME/ftl-build/switch}
DKP=${DEVKITPRO:-$ROOT/devkitpro}
PKGS=$ROOT/pkgs
REPO=https://pkg.devkitpro.org/packages
UA="pacman/6.0"  # the package server answers pacman
mkdir -p "$PKGS" "$DKP" "$ROOT/src" "$ROOT/tools"

# package name -> file name, from the repositories' databases
fetch_db() { curl -fsS -A "$UA" -o "$PKGS/$2" "$1"; mkdir -p "$PKGS/${2%.db}"; tar xf "$PKGS/$2" -C "$PKGS/${2%.db}"; }
fetch_db $REPO/dkp-libs.db dkp-libs.db
fetch_db $REPO/linux/x86_64/dkp-linux.db dkp-linux.db
file_of() {  # newest version of package $1
  local d
  d=$(ls -d "$PKGS"/dkp-libs/"$1"-[0-9]* "$PKGS"/dkp-linux/"$1"-[0-9]* 2>/dev/null | sort -V | tail -1)
  [ -n "$d" ] || { echo "no package $1" >&2; exit 1; }
  awk '/%FILENAME%/{getline; print; exit}' "$d/desc"
}
# target libraries (host-independent); devkita64-gcc only for libgcc, crt files and libstdc++ (mesa)
for p in libnx devkita64-newlib devkita64-gcc devkita64-rules switch-mesa switch-libdrm_nouveau switch-zlib; do
  f=$(file_of $p)
  [ -f "$PKGS/$f" ] || curl -fsS -A "$UA" -o "$PKGS/$f" "$REPO/$f" || curl -fsS -A "$UA" -o "$PKGS/$f" "$REPO/linux/x86_64/$f"
  tar xf "$PKGS/$f" -C "$DKP" --strip-components=2 --warning=no-unknown-keyword opt/devkitpro
  echo "  $f"
done

# elf2nro and nacptool (lz4 for elf2nro)
SRC=$ROOT/src
[ -d "$SRC/switch-tools" ] || git clone -q https://github.com/switchbrew/switch-tools.git "$SRC/switch-tools"
for f in lz4.c lz4.h; do [ -f "$SRC/$f" ] || curl -fsSL -o "$SRC/$f" https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/$f; done
(cd "$SRC/switch-tools/src" &&
  cc -O2 -I"$SRC" -o "$ROOT/tools/elf2nro" elf2nro.c romfs.c filepath.c "$SRC/lz4.c" &&
  cc -O2 -o "$ROOT/tools/nacptool" nacptool.c)
echo "devkitPro target files in $DKP, tools in $ROOT/tools"
