# ftl-switch

`ftl.nro` runs your own copy of **FTL: Faster Than Light for iPad** (1.6.21) on
a **Nintendo Switch** with Atmosphère, as homebrew.

It runs the real iOS game, unmodified: a Mach-O loader, an Objective-C runtime
and just enough of the iOS frameworks underneath it, on top of libnx. The
touchscreen works as on the iPad, Joy-Cons and the Pro Controller drive a
cursor, mods are applied on the console with ftlman's engine, and Hyperspace
(Multiverse and other big mods) is built in.

**Nothing of the game is in this repository**: you copy your own `.ipa` to the
SD card.

Tested on a Switch with Atmosphère (firmware 22.1.0): the game, saves, touch
and Joy-Cons, mods applied on the console, Hyperspace, and Multiverse with The
Renegade Collection.

This project is based on [ftl-android](https://github.com/Sidd065/ftl-android)
by Sidd065, whose loader, Objective-C runtime and iOS frameworks it runs on.
It has been reworked extensively and none of ftl-android's Android code is
left; see [CREDITS.md](CREDITS.md) for what came from there and what's
new. The same code also builds the Linux desktop version, `ftl-linux`, whose
own repository is **ftl-linux**.

## Quick start

1. Copy `ftl.nro` and your FTL for iPad `.ipa` (1.6.21, decrypted; the App
   Store copy is FairPlay-encrypted and can't be used) to `sdmc:/switch/ftl/`.
2. Start FTL with a game's memory: hold **R** while starting any game and pick
   FTL in the Homebrew Menu, or use an NSP forwarder.
3. The first start imports the game (a minute or so). Mods go in
   `sdmc:/switch/ftl/mods/`.

To build `ftl.nro` yourself, see [docs/switch/building.md](docs/switch/building.md).

## Documentation

| | |
|---|---|
| [Playing](docs/switch/playing.md) | Installing, controls, settings, logs, what was tested, known limitations |
| [Mods and Hyperspace](docs/switch/mods.md) | Mods on the console, patching big mods on a PC, Hyperspace |
| [Building](docs/switch/building.md) | Toolchain, build steps, the icon, testing in Ryujinx |
| [How the Switch port works](docs/switch/internals.md) | Toolchain, code memory under Horizon, ftlman and Hyperspace in one `.nro` |
| [How it works](docs/how-it-works.md) | The shared core: the loader, the runtime, the frameworks, the hooks |
| [ftl-linux](docs/linux/usage.md) | The Linux version, which builds from the same code ([building](docs/linux/building.md), [mods](docs/linux/mods.md), [testing](docs/testing.md)) |

## Reporting bugs

Please attach the session logs from `sdmc:/switch/ftl/logs/` (with Hyperspace,
`zhl.log` and `FTL_HS.log` too), and say which mods were applied and how FTL
was started (Homebrew Menu over a game, or a forwarder).

## License

GNU General Public License, version 3 ([LICENSE](LICENSE)). The code from
ftl-android is GPL version 3; what this project adds is also available under
GPL version 3 or any later version. Details, the licenses of Hyperspace, ftlman
and the other components, and a note on ftlman in `ftl.nro`:
[CREDITS.md](CREDITS.md).

FTL: Faster Than Light is made by Subset Games. This project isn't affiliated
with or endorsed by Subset Games, Apple or Nintendo.
