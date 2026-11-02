# Mods and Hyperspace on the Switch

## Mods

Put mods (`.ftl` or `.zip` files, or unpacked mod folders) in
`sdmc:/switch/ftl/mods/` and start FTL: when that set of mods changed since the
last start, they're applied right there, with progress on the screen, by
ftlman's own patch engine built into `ftl.nro` (the same results as ftlman on
a PC). Then the game starts with them.

- Order: `Hyperspace.ftl` first, then the order in `mods/order.txt` if there is
  one (one file name per line, `-name` for a mod that's off; mods it doesn't
  name are on and follow by name), else by name.
- Every patch starts from the original, kept as `game/ftl.dat.vanilla`. Taking
  all mods out of the folder puts the original back.
- The patch happens in memory: `ftl.dat.vanilla` is read once and the new
  `ftl.dat` written once. Multiverse with the Renegade Collection takes about
  two minutes. That needs FTL started with a game's memory (see
  [playing.md](playing.md)), and room for the game's `ftl.dat` plus the mods
  unpacked (Multiverse with the Renegade Collection: about 1.1 GB of the
  roughly 2.7 GB there is). When that doesn't fit, it patches on the SD card
  instead, which takes 20 minutes or more for Multiverse.
- If a mod can't be applied, FTL says why and plays without mods; fix or remove
  the mod and start again. ftlman's messages are in `logs/`.
- After a change the game starts at the title screen (a run saved with other
  mods may not load).
- With `Hyperspace.ftl` among them (or a mod like Multiverse that brings
  Hyperspace's files), Hyperspace starts with the game (see below).

Either way the port notices a changed `ftl.dat` at launch and makes the image
overlay mods need.

## The mod list

When there are mods in `mods/`, FTL's start screen says "Press L for the mod
list, R for settings..." for a moment (a button held while FTL starts doesn't
count: press it there, or keep pressing it while FTL starts). The list shows
the mods and which are on:

    Up/Down  choose            A  on/off
    L / R    move up / down    -  all off (the original game)
    +        apply and start   X  apply now (always rebuilds ftl.dat)
    Y        settings          B  start without patching

**+** saves the choice to `mods/order.txt`, applies it when it changed, and
starts the game. **X** saves and applies right away and goes back to the list;
it rebuilds `ftl.dat` even when nothing changed, for when `ftl.dat` was changed
or replaced by hand. **B** starts without patching, leaving out what changed
since the last + or X. **Y** opens the settings, as R does on the start screen.

All off puts the original `ftl.dat` back (a copy, so it's quick), and the mods
stay in the folder for later. Hyperspace stays first in the list. FTL also
notices by itself when `ftl.dat` is the original game's again (for instance
`ftl.dat.vanilla` renamed back) and applies the mods again.

## Very large mod sets: patch on a PC

Multiverse and its usual addons patch on the console in a few minutes. Only a
mod set too large to patch in memory (many big addons on top of Multiverse, or
FTL started from the Album) falls back to the SD card, which is slow; the log
then says `Patching on the SD card`. For such a set, patching on a PC is
faster: ftlman there does it in under a minute, and the console only needs the
result:

1. Copy the iPad game's original `ftl.dat` to a folder on the PC:
   `sdmc:/switch/ftl/game/ftl.dat.vanilla` if it's there (mods were applied on
   the console before), else `game/ftl.dat`. Name it `ftl.dat`. (The PC
   version's own `ftl.dat`, from Steam or GOG, doesn't work: FTL says so.)
2. In [ftlman](https://github.com/afishhh/ftlman) on the PC, set the FTL
   directory to that folder (it only needs `ftl.dat`), tick `Hyperspace.ftl`
   first and then the mods, and Apply. Leave ftlman's Hyperspace installer off:
   `ftl.nro` brings its own.
3. If `game/ftl.dat.vanilla` isn't on the console yet, rename the console's
   `game/ftl.dat` to that first, so the original stays. Then copy the patched
   `ftl.dat` from the PC to `sdmc:/switch/ftl/game/ftl.dat`.
4. Start FTL. It notices the new `ftl.dat`, uses it as it is and starts
   Hyperspace when the mods include it (whatever patched them, Hyperspace runs
   when `ftl.dat` has `data/hyperspace.xml`).

Only a change in `mods/` makes `ftl.nro` patch on the console again: leave
`mods/` as it is while you use a PC-patched `ftl.dat`. Changing it later
patches the mods there from `ftl.dat.vanilla` and replaces the PC one; taking
every mod out of `mods/` puts the original back only if `ftl.dat` is still the
one patched on the console.

## Hyperspace

Hyperspace hooks the game's code at fixed addresses, so it's built for one
game binary. The Android app, this Switch port and the ARM Linux version all
run the same one, FTL for iPad 1.6.21, so they share one Hyperspace: the
[`iOS-binary-ports`](https://github.com/The-Dumb-Dino/FTL-Hyperspace-Dino/tree/iOS-binary-ports)
branch of FTL-Hyperspace-Dino. The PC versions of FTL are different binaries
and use the regular Hyperspace releases.

`ftl.nro` built with Hyperspace (the default when `hyperspace/build-switch.sh`
has run, see [building.md](building.md)) starts it whenever the applied mods
include it: `Hyperspace.ftl` (from any
[Hyperspace 1.23.2](https://github.com/FTL-Hyperspace/FTL-Hyperspace/releases/tag/v1.23.2)
download) first, then the mods that use it. There's nothing else to install:
it's part of `ftl.nro`. Its logs (`zhl.log`, `FTL_HS.log`) go to `logs/`,
`settings_hs.ini` to `saves/`.

Hyperspace's error messages (a mod's `Hyperspace.ErrorMessage` from Lua, which
some mods use on purpose as a fake system error, or a broken `hyperspace.xml`)
open the Switch's own error pop-up with an OK button, the game waiting until
it's closed, as the PC builds do with a message box.

Memory: Hyperspace loads images lazily, on first use, which takes Multiverse
from about 5.6 GB to about 1.4 GB, and a game's slot on the Switch has about
3.2 GB. Start FTL with a game's memory (hold R, or a forwarder).

How Hyperspace is built and hooked in on the Switch: [how-it-works.md](../how-it-works.md#hyperspace).
