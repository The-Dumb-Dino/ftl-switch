# Testing and debugging

## Logs

- Session logs go to `sdmc:/switch/ftl/logs/`; the last five sessions are kept,
  and Hyperspace's `zhl.log` and `FTL_HS.log` go to the same folder.
  `verbose = true` in `settings.ini` logs more.
- On a crash the log ends with a backtrace: the game's frames as `FTL+<address>`
  (the address in the 1.6.21 binary, as a disassembler shows it), the port's as
  offsets into `ftl.elf`.
- Testing in Ryujinx (logs, screenshots, what it doesn't catch) is in
  [switch/building.md](switch/building.md#testing-in-ryujinx).

## The headless harness

`native/build-harness.sh` builds `ftlrun`, a headless harness (Mesa's
surfaceless EGL, guarded allocations) that runs the game's core (loader,
runtime, frameworks) on an aarch64 Linux computer, or under `qemu-aarch64`
elsewhere, without the Switch. It needs `libcxx/build.sh linux`. It takes
commands from a FIFO (taps, drags, multi-finger gestures, screenshots, text,
quit) and can record audio to a WAV file.

## Debug switches

- `FTL_PAGESIZE=65536`: the loader acts as if pages were 64K.
- In `ftlrun` only: `FTL_GUARD=1`, a guarded allocator for hunting heap
  corruption in the game (`debug/guard_alloc.c`), and `FTL_DEBUGHOOKS=1`,
  hooks that trace some game functions (`debug/debug_hooks.c`).

## Tools

`tools/` has Mach-O and Objective-C inspection scripts (`macho.py`,
`machinfo.py`, `objcdump.py`, `imports.py`), the C++ symbol table generator
(`gen_cxx_syms.py`) and `.ipa`/package helpers (`pkgextract.py`,
`pkglist.py`).
