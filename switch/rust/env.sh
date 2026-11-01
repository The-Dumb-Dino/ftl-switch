# Environment for building Rust crates for ftl.nro (source it): the custom target,
# the std sources with libc for the Switch, and C code (cc crate) built like the port's.
SWITCH_RUST=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export DEVKITPRO=${DEVKITPRO:-$HOME/ftl-build/switch/devkitpro}
export RUST_STD_SRC=${RUST_STD_SRC:-$HOME/ftl-build/switch/rust-src/library}
export __CARGO_TESTS_ONLY_SRC_ROOT=$RUST_STD_SRC
SWITCH_TARGET=$SWITCH_RUST/aarch64-nintendo-switch-ftl.json
_nl=$DEVKITPRO/devkitA64/aarch64-none-elf
export CC_aarch64_nintendo_switch_ftl=clang
export AR_aarch64_nintendo_switch_ftl=llvm-ar
export CFLAGS_aarch64_nintendo_switch_ftl="--target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -femulated-tls -ffunction-sections -fdata-sections -D__SWITCH__ -nostdinc -isystem $(clang -print-resource-dir)/include -isystem $DEVKITPRO/libnx/include -isystem $_nl/include -D_GNU_SOURCE="
export CRATE_CC_NO_DEFAULTS=1
