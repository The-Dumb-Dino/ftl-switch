# Mods and Hyperspace on the Switch

## Mods

Put mods (`.ftl` or `.zip` files, or unpacked mod folders) in
`sdmc:/switch/ftl/mods/` and start FTL: when that set of mods changed since the
last start, they're applied right there, with progress on the screen, by
ftlman's own patch engine built into `ftl.nro` (the same results as ftlman on PC
and Linux). Then the game starts with them.

- Order: `Hyperspace.ftl` first, then the order in `mods/order.txt` if there is
  one (one file name per line; mods it doesn't name follow by name), else by name.
- Every patch starts from the original, kept as `game/ftl.dat.vanilla`. Taking
  all mods out of the folder puts the original back.
- If a mod can't be applied, FTL says why and plays without mods; fix or remove
  the mod and start again. ftlman's messages are in `logs/`.
- After a change the game starts at the title screen (a run saved with other
  mods may not load).
- With `Hyperspace.ftl` among them (or a mod like Multiverse that brings
  Hyperspace's files), Hyperspace starts with the game (see below).

Either way the port notices a changed `ftl.dat` at launch and makes the image
overlay mods need.

## Big mods: patch on a PC

Applying mods on the console is slow for big ones (Multiverse with addons takes
20 minutes or more); ftlman on a PC does it in under a minute, and the console
only needs the result:

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

How Hyperspace is built and hooked in on the Switch: [internals.md](internals.md#hyperspace).
