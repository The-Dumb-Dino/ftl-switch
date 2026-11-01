# Playing on the Switch

`ftl.nro` runs on a Switch with [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere),
as homebrew. It was tested on Atmosphère with firmware 22.1.0.

## Installing

1. Copy `ftl.nro` to `sdmc:/switch/ftl/ftl.nro`.
2. Copy your FTL for iPad `.ipa` (1.6.21, with a decrypted executable; the App
   Store copy is FairPlay-encrypted and can't be used) into the same folder,
   `sdmc:/switch/ftl/`.
3. Start FTL **with a game's memory**: from the Homebrew Menu opened by holding
   **R** while starting any game, or through an NSP forwarder. (From the Album,
   homebrew gets about 440 MB, which isn't enough; FTL says so and stops.)

The first start imports the `.ipa` into `sdmc:/switch/ftl/game/` (a minute or
so); the `.ipa` can be deleted afterwards.

Each start shows its steps on a text screen first (settings, memory, the game,
mods), then `ftl.nro` has the Homebrew Menu start it again straight into the
game, so the graphics get a window the text screen never used. If a start stops
on that screen, the last line says where, and `logs/` has the details.

    sdmc:/switch/ftl/
      ftl.nro          the app
      game/            FTL, ftl.dat, Info.plist (from the .ipa)
      saves/           the game's saves and settings
      mods/            mods to apply (see mods.md)
      settings.ini     the port's settings (written with comments on the first start)
      logs/            session logs (send these with a bug report)
      cache/           the mod image overlay

## Controls

- **Handheld**: the touchscreen, as on the iPad. A two-finger tap pauses.
- **Controllers** (Joy-Con, Pro Controller): the left stick moves a cursor that
  is drawn to buttons, **A** taps, **B** and **+** go back, **X** pauses,
  **ZL** shows the magnifier while held, **ZR** is a right click (for mods that
  use one).
- **Zoom** is off: the whole game fits the Switch's screen. `zoom = true` in
  settings.ini turns it on: two-finger pinch and pan, zooming in on the map and
  the upgrade screen, the minimap, and **L/R** zoom, **−**/right stick reset.
- Renaming the ship opens the system keyboard.
- **Leaving**: close FTL from the HOME menu; the game saves first (settings,
  profile and run: `ftl.nro` holds the close until it has). Back on the main
  menu doesn't quit, so it can't happen by accident.

## Settings

`sdmc:/switch/ftl/settings.ini` is written with comments on the first start.
Delete a line to get its default.

| Key | Default | |
|---|---|---|
| `docked_1080p` | `true` | Docked: draw at 1080p (else 720p, scaled up by the console) |
| `render_height` | `0` | 0: automatic; or `768`, `1536` |
| `magnifier` | `false` | The loupe over a held finger (ZL shows it either way) |
| `two_finger_pause` | `true` | A two-finger tap pauses |
| `event_text` | `0` | Event text size: 0 the game's, 1 large, 2 larger |
| `resume_run` | `false` | Continue the saved run at launch |
| `pad_magnetism` | `true` | The cursor is drawn to buttons |
| `pad_rumble` | `true` | |
| `pad_<button>` | see below | `a b x y l r zl zr lstick rstick plus minus` |
| `zoom` | `false` | All of zooming; with it on, `pinch_zoom`, `auto_zoom`, `event_zoom`, `minimap` pick among its parts |
| `allow_applet_mode` | `false` | Start from the Album too (little memory: may crash) |
| `verbose` | `false` | More detail in `logs/` |

Actions for `pad_<button>`: `none tap right_click back pause zoom_in zoom_out
reset_zoom magnifier turbo precision` (the zoom actions only with `zoom = true`).

## Logs and screenshots

- Logs: `logs/session-*.txt`. Hyperspace's `zhl.log` and `FTL_HS.log` are
  there too. Every line also goes to the kernel's debug output.
- An empty file named `screenshot` in `sdmc:/switch/ftl/` makes the next frame
  go to `screenshot.png` (the screen) and `screenshot-game.png` (the game's own
  image, before scaling) there.

## Tested on a console

On Atmosphère, firmware 22.1.0, started through an NSP forwarder: importing the
`.ipa`, settings and saves, the touchscreen and detached Joy-Cons, rumble, mods
applied on the console, Hyperspace with its error pop-up, and Multiverse with
The Renegade Collection (patched on the console, and on a PC).

## Known limitations

- Alerts with choices show the message (error applet) and take the first
  button: homebrew has no system dialog with choices.
- The drawing size is picked at start (1080p docked, 720p handheld); switching
  modes while playing leaves the console to scale.
- Applying big mods on the console is slow (Multiverse with addons: 20 minutes
  or more). Patching on a PC is faster: see [mods.md](mods.md#big-mods-patch-on-a-pc).
