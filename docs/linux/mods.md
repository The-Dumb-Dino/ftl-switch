# Mods and Hyperspace on Linux

## Mods

Mods are installed with [ftlman](https://github.com/afishhh/ftlman), the PC
mod manager, in its own window:

    ftl-linux --ftlman              # or ./mod-manager in a portable folder,
                                    # or "Mod Manager" in FTL's launcher menu

Put `.ftl`/`.zip` mods in its `mods/` folder (`--paths` shows where), tick
them, press **Apply**, close ftlman and start the game. ftlman patches the
game's `ftl.dat` in place and keeps the original as `ftl.dat.vanilla`, exactly
as with FTL on PC; applying with nothing ticked puts the original back.

The game notices on its own: at launch, when `ftl.dat` changed, ftl-linux looks
at it and, if mods added images (the iPad build keeps small images in atlases),
writes the atlas lists without them into the cache, so the mods' images show
(see [how it works](../how-it-works.md#mods)). After `ftl.dat` changes the game
starts at the title screen, since a run saved with other mods may not load.

`--ftlman` gives ftlman settings for this port the first time: the game's
folder as FTL's, and its Hyperspace installer and self-update off (both are
for the PC game and ftlman's PC builds). If you also use ftlman for FTL on PC
and have `~/.config/ftlman/settings.json`, ftlman uses that instead; point its
FTL directory at the game folder then.

Tested: [FTL Event Tags](https://github.com/thrnz/FTLEventTags1.6) and the
[KRS Obsidian Cruiser](https://github.com/zedseven/ftl-obsidian-cruiser) apply
together, and a run with them goes through sector 1 into sector 2.
Mod text in Windows-1252 rather than UTF-8 is up to ftlman.

## Hyperspace

[Hyperspace](https://github.com/FTL-Hyperspace/FTL-Hyperspace), which
Multiverse and other big mods build on, changes the game's code as well as its
data. The iPad game's Hyperspace (the `android-arm64` branch of
[Sidd065/FTL-Hyperspace](https://github.com/Sidd065/FTL-Hyperspace)) builds
for Linux with `hyperspace/build-linux.sh` (see [building.md](building.md#optional-hyperspace)).
The fork has a Linux target (its `buildscripts/linux/`): the iPad code paths
(`HS_ANDROID`) with the host's clang and glibc, and the `arm64-linux-ftl`
vcpkg triplet with the port's libc++.

Installing works as on PC, without the launch script: on PC the `.so` goes next
to `FTL.amd64` and the `FTL` script is edited to `LD_PRELOAD` it. The iPad game
isn't a Linux program, so preloading can't hook it; ftl-linux loads Hyperspace
itself once the game is mapped and before any of its code runs. So:

1. Put `Hyperspace.1.6.21.arm64.so` in the game folder (or next to
   `ftl-linux`; `native/build-linux.sh install` puts it in
   `libexec/ftl-linux/`, and `--make-portable` copies it into the folder).
   `hyperspace_lib` in settings.ini overrides where it's looked for.
2. In ftlman, tick `Hyperspace.ftl` (from any
   [Hyperspace 1.23.2](https://github.com/FTL-Hyperspace/FTL-Hyperspace/releases/tag/v1.23.2)
   download) first, then the mods that use it, and Apply.
3. Start the game. `ftl-linux --info` says whether Hyperspace is found and
   used.

Hyperspace is only started while the applied mods include it (ftl.dat has
`data/hyperspace.xml`), so the same install plays vanilla and other mods
untouched. To uninstall, delete the `.so`. Its logs (`zhl.log`, `FTL_HS.log`)
go to the log folder, `settings_hs.ini` to the saves folder. ftlman's own
Hyperspace installer is for the PC game and stays off.

Tested on the 16K-page Asahi kernel: with `Hyperspace.ftl` from 1.23.2 applied,
Hyperspace starts (all hooks in place), the title screen and the version line
are Hyperspace's, the hangar has its seed field, and a run goes through the
beacon map, a jump and a fight at a star, then saves (`hs_continue.sav`) and
quits cleanly. Multiverse's load order, top to bottom: `Hyperspace.ftl`,
Multiverse Assets, Multiverse Data, then any addons. Mods that name their saves,
like Multiverse, keep their own run and profile, so they don't mix with the
plain game's.
