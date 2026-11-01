# Playing FTL on the Switch

## What you need

- A Switch running [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)
  (tested with firmware 22.1.0).
- `ftl.nro` from the [releases](https://github.com/The-Dumb-Dino/ftl-switch/releases).
- Your own copy of **FTL for iPad 1.6.21** as an `.ipa` with a decrypted
  executable. A copy straight from the App Store is encrypted and won't work.

## Setting up

1. Create the folder `sdmc:/switch/ftl/` on the SD card.
2. Copy `ftl.nro` and your `.ipa` into it.

On the first start FTL imports the game from the `.ipa` (about a minute).
Afterwards you can delete the `.ipa`.

## Starting the game

FTL needs a game's memory: it doesn't run from the Album, which gives homebrew
too little (FTL says so and stops). Two ways to start it:

- **A forwarder (recommended).** Create an NSP forwarder for `ftl.nro` with a
  tool like [Sphaira](https://github.com/ITotalJustice/sphaira). FTL then gets
  its own icon on the HOME menu and starts like any other game, with the
  memory it needs.
- **The Homebrew Menu.** Without a forwarder, FTL has to be started this way:
  hold **R** while starting any game, then pick FTL in the Homebrew Menu that
  opens. The Homebrew Menu you get from the Album (gallery) doesn't work.

Each start shows a short text screen first (settings, the game, mods), then
FTL restarts itself straight into the game. If a start stops on that screen,
the last line says why.

## Controls

**Touch** works as on the iPad. In addition, a **two-finger tap pauses** the
game.

**Controllers** (Joy-Cons, Pro Controller) move a cursor:

| Button | Action |
|---|---|
| Left stick | Move the cursor (it's drawn to buttons) |
| A | Tap |
| B, + | Back |
| X | Pause |
| ZL (hold) | Magnifier |
| ZR | Right click (for mods that use one) |

The controller controls may be reworked in a future version. The buttons can
be changed in `settings.ini` (see below).

**Zoom (optional)** is off: the whole game fits the screen. To use it, set
`zoom = true` in `settings.ini`. Then two fingers pinch and pan, the game zooms
in on the map and the upgrade screen, a minimap shows where you are, **L/R**
zoom and **−** or the right stick resets the zoom.

## Leaving the game

Press **HOME** and close FTL whenever you want: it saves before it closes. To
be safe, go back to the title screen first.

## Mods

Put mods in `sdmc:/switch/ftl/mods/` and start FTL: it applies them before the
game starts. To switch mods on and off, press **L** when the start screen says
"Press L for the mod list...". Everything about mods and Hyperspace (Multiverse
and other big mods) is in [mods.md](mods.md).

## Updates

Release builds check GitHub for a new version at start, when the Switch is
online, and ask:

    A  Update now
    B  Start without updating
    X  Skip this version

**A** downloads and checks the new version, puts it in place and starts it. If
anything goes wrong, the old version starts as usual. **X** stops asking about
that version (delete `update-skip` to be asked again).

The check needs the Switch's date and time to be right; with the clock far off
it can't reach GitHub securely, and the start screen says so.
`check_updates = false` in `settings.ini` turns the check off. Builds you make
yourself don't check.

## Settings

`sdmc:/switch/ftl/settings.ini` is created on the first start, with a comment
on every line. Delete a line to get its default.

| Setting | Default | |
|---|---|---|
| `docked_1080p` | `true` | Docked: draw at 1080p (else 720p, scaled up by the console) |
| `render_height` | `0` | 0: automatic; or `768`, `1536` |
| `magnifier` | `false` | The loupe over a held finger (ZL shows it either way) |
| `two_finger_pause` | `true` | A two-finger tap pauses |
| `event_text` | `0` | Event text size: 0 the game's, 1 large, 2 larger |
| `resume_run` | `false` | Continue the saved run at start |
| `pad_magnetism` | `true` | The cursor is drawn to buttons |
| `pad_rumble` | `true` | Rumble |
| `pad_<button>` | see above | `a b x y l r zl zr lstick rstick plus minus` |
| `zoom` | `false` | Zoom (see Controls); with it on, `pinch_zoom`, `auto_zoom`, `event_zoom` and `minimap` pick its parts |
| `allow_applet_mode` | `false` | Start from the Album too (little memory: may crash) |
| `check_updates` | `true` | Look for updates at start (release builds) |
| `verbose` | `false` | More detail in the logs |

Actions for `pad_<button>`: `none tap right_click back pause zoom_in zoom_out
reset_zoom magnifier turbo precision` (the zoom actions only with `zoom = true`).

## Files on the SD card

    sdmc:/switch/ftl/
      ftl.nro          the app
      game/            the game, imported from your .ipa
      saves/           your saves and the game's settings
      mods/            mods (see mods.md)
      settings.ini     the port's settings
      logs/            session logs
      cache/           what mods' images need

## Problems

Please report bugs on GitHub and attach the session logs from
`sdmc:/switch/ftl/logs/` (with Hyperspace, `zhl.log` and `FTL_HS.log` are
there too).

An empty file named `screenshot` in `sdmc:/switch/ftl/` saves the next frame as
`screenshot.png` there, for showing what went wrong.

## Known limitations

- The picture size is chosen at start (1080p docked, 720p handheld). Switching
  between docked and handheld while playing leaves the scaling to the console.
- Very large mod sets that don't fit in memory are patched on the SD card,
  which is slow; patch those on a PC instead (see
  [mods.md](mods.md#very-large-mod-sets-patch-on-a-pc)).
