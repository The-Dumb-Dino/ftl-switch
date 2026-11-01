# ftl-switch

This project runs a copy of the iPad version of **FTL: Faster Than Light** on a **Nintendo Switch 1** with Atmosphère custom firmware installed.

This runs the real iOS game, unmodified: a Mach-O loader, an Objective-C runtime
and just enough of the iOS frameworks underneath it, on top of libnx. The
touchscreen works as on the iPad, Joy-Cons and the Pro Controller drive a
cursor, mods are applied on the console with ftlman's engine, and Hyperspace
(Multiverse and other big mods) is built in.

**Nothing of the game is in this repository**: You are required to acquire your own `FTL.ipa` to put on the
SD card.

Tested on a Switch with Atmosphère (firmware 22.1.0): the game, saves, touch
and Joy-Cons, mods applied on the console, Hyperspace, and Multiverse with TRC.

This project is derived from [ftl-android](https://github.com/Sidd065/ftl-android)
by Sidd065, whose loader, Objective-C runtime and iOS frameworks it runs on.
It has been reworked extensively and none of ftl-android's Android code is
left; see [CREDITS.md](CREDITS.md) for what came from there and what's new.


## Disclaimer

This project has been assisted by AI, which some people may not view too positively. I am therefore letting you know now, so that you can step back if you would prefer to stay away from such projects.
I would like to clarify that this is not my first FTL-related project. Please check out my profile. I am by no means inexperienced and have achieved a bunch of great things without any use of AI.

One of my fellow developer friends said something pretty fitting: 
***If a craftsman gets access to a new tool and their work gets worse, the tool is not to blame***


## Quick start

1. Copy `ftl.nro` and your FTL for iPad `.ipa` (1.6.21, decrypted; the App
   Store copy is FairPlay-encrypted and can't be used) to `sdmc:/switch/ftl/`.
2. Start FTL with a game's memory: best through an NSP forwarder (made with a
   tool like Sphaira). Without one, hold **R** while starting any game and pick
   FTL in the Homebrew Menu; from the Album (gallery) it doesn't work.
3. The first start imports the game (a minute or so). Mods go in
   `sdmc:/switch/ftl/mods/`.

To build `ftl.nro` yourself, see [docs/switch/building.md](docs/switch/building.md).


## Supporting the project

This is a hobby project done in my spare time. If you find it useful and want
to say thanks, you can buy me a coffee:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/DEINNAME)

Donations are never expected; Bug reports, logs, and well-written issues are
just as valuable.


## Documentation

| | |
|---|---|
| [Playing](docs/switch/playing.md) | Installing, controls, settings, logs, what was tested, known limitations |
| [Mods and Hyperspace](docs/switch/mods.md) | Mods on the console, the mod list, very large mod sets on a PC, Hyperspace |
| [Building](docs/switch/building.md) | Toolchain, build steps, the icon, testing in Ryujinx |
| [How it works](docs/how-it-works.md) | The loader, the runtime and frameworks under the game, code memory under Horizon, the toolchain, ftlman and Hyperspace in one `.nro` |
| [Testing and debugging](docs/testing.md) | Logs, the headless test harness, debug switches, tools |


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
