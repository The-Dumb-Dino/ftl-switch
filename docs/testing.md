# Testing and debugging

## Logs

- Session logs: `ftl-linux --paths` shows the folder. The last five sessions
  are kept; Hyperspace's `zhl.log` and `FTL_HS.log` go to the same folder.
- `FTL_LOG_STDERR=1` (or `--verbose`) copies the log to stderr.
- On a crash the log ends with a backtrace: the game's frames as `FTL+<address>`
  (the address in the 1.6.21 binary, as a disassembler shows it), the port's as
  `library!symbol+offset`.

## Scripted input

`FTL_TEST_CMD=/path/to/fifo ftl-linux` takes commands on a FIFO, in window
pixels: `click x y`, `rclick x y`, `down/up/move x y`, `wheel x y steps`,
`key <key code>` (pad.c's `K_*` numbers), `text <reply to text prompts>`,
`shot file.png`, `fullscreen`, `pause`, `resume`, `quit`, and a virtual
controller: `padattach`, `padbtn <SDL button> 0|1`, `padaxis <SDL axis>
<value>`; `audio` logs the output's peak level. With the screen locked, add
`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` for the controller commands.

## The headless harness

`native/build-harness.sh` builds `ftlrun`, a headless harness (Mesa's
surfaceless EGL, guarded allocations) that runs the game natively on aarch64
or under `qemu-aarch64` elsewhere. It takes commands from a FIFO (taps, drags,
multi-finger gestures, screenshots, text, quit) and can record audio to a WAV
file.

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
