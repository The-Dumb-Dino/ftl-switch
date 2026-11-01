# Playing with ftl-linux

## Getting the game in

You need FTL for iPad as an `.ipa` (or the extracted `FTL.app`) whose arm64
executable is decrypted (not the App Store copy, which is FairPlay-encrypted).
Version 1.6.21 gets all features; other builds run without the extras that hook
the game's code.

    ftl-linux --import ~/Downloads/FTL.ipa
    ftl-linux --info                # FTL for iPad 1.6.21, the 1.6.21 build (all features)
    ftl-linux

Or just start `ftl-linux` (or FTL from the app menu): without a game it asks
for the `.ipa` with a file chooser (kdialog on KDE, else zenity) and imports it.

`--import` also takes an `FTL.app` folder or a folder holding `Payload/FTL.app`.
It copies `FTL`, `ftl.dat` and `Info.plist` and refuses a FairPlay-encrypted
executable. Importing again replaces the copy, mods included.

## A portable folder

    ftl-linux --make-portable ~/Games/FTL ~/Downloads/FTL.ipa

makes a folder with everything in it, which runs from wherever it's put (another
disk, a USB stick, another account on an aarch64 Linux with SDL3):

    FTL/
      ftl-linux        the game (double-click FTL.desktop, or run it)
      mod-manager      ftlman for this folder's game
      FTL.desktop      a launcher that finds the folder itself; right-click for the mod manager
      portable         the marker: with it, ftl-linux keeps everything in its own folder
      game/            FTL, ftl.dat, Info.plist from the .ipa (ftlman adds ftl.dat.vanilla)
      ftlman/          ftlman, its settings.json and mods/
      data/            settings.ini, saves/, logs/, cache/ (made on the first start)
      README.txt

Without the `.ipa` the game already imported is copied in (its unmodded
`ftl.dat`). Running `--make-portable` again on the same folder updates
`ftl-linux` and ftlman and keeps the game, saves and mods; with an `.ipa` it
imports the game again. A folder moved elsewhere keeps working: ftl-linux finds
its files next to itself, and points ftlman at the folder's game each time the
mod manager starts. Don't share a folder with `game/` in it: that's your copy of
the game.

## Command line

| Command | |
|---|---|
| `ftl-linux` | Play |
| `ftl-linux --import <path>` | Import the game (`.ipa`, `FTL.app`, or a folder with `Payload/FTL.app`) |
| `ftl-linux --info` | The imported game's version |
| `ftl-linux --paths` | Where the game, saves, settings and logs are |
| `ftl-linux --ftlman` | The mod manager (ftlman), set up for this game |
| `ftl-linux --make-portable <folder> [FTL.ipa]` | A portable folder (see above) |
| `--fullscreen`, `--windowed` | Override the setting for this start |
| `--verbose` | More detail in the log, and the log on stderr |

## Paths

Installed, the XDG base directories are honoured (defaults in brackets). In a
portable folder everything is in the folder (see above).

| What | Where |
|---|---|
| Imported game (`FTL`, `ftl.dat`, `Info.plist`) | `$XDG_DATA_HOME/ftl-linux/game` (`~/.local/share/...`) |
| Saves and the game's own settings | `$XDG_DATA_HOME/ftl-linux/saves` |
| ftlman (a copy), its settings and `mods/` | `$XDG_DATA_HOME/ftl-linux/ftlman` |
| Settings of the port | `$XDG_CONFIG_HOME/ftl-linux/settings.ini` (`~/.config/...`) |
| Session logs, crash marker | `$XDG_STATE_HOME/ftl-linux` (`~/.local/state/...`) |
| Cache (the mod image overlay) | `$XDG_CACHE_HOME/ftl-linux` |

The last five session logs are kept. If the game crashed, the next start says
so in the log and starts at the title screen instead of continuing the run.

## Settings

`settings.ini` is written with comments the first time the game starts. Delete
a line to get its default.

| Key | Default | |
|---|---|---|
| `fullscreen` | `false` | Start in fullscreen (F11 or Alt+Enter switch while playing) |
| `window_width`, `window_height` | `0` | Window size in desktop units; 0: 80% of the screen, with the screen's shape |
| `vsync` | `true` | |
| `render_height` | `0` | 0: automatic (1536 pixels on windows taller than 900, else 768); or `768`, `1536` |
| `pause_on_focus_loss` | `true` | Pause when another window gets the focus (minimizing always pauses) |
| `magnifier` | `auto` | The loupe over your finger. `auto`: off with a mouse, on with a touchscreen |
| `pinch_zoom` | `auto` | Two fingers zoom and pan. The mouse wheel always zooms |
| `auto_zoom` | `auto` | Zoom in on the beacon map and the upgrade screen |
| `event_zoom` | `false` | Zoom in on events too |
| `minimap` | `true` | Shows where you are while zoomed in |
| `two_finger_pause` | `true` | A two-finger tap pauses (touchscreens) |
| `event_text` | `0` | Event text size: 0 the game's, 1 large, 2 larger |
| `resume_run` | `false` | Continue the saved run at launch |
| `pad_magnetism`, `pad_rumble`, `pad_touchpad` | `true` | Controller cursor pull, rumble, DualSense/DualShock touchpad |
| `pad_<button>` | see below | Controller buttons (`a b x y lb rb lt rt l3 r3 start select home`) |
| `text_dialog` | `auto` | Text prompts (renaming the ship): `zenity`, `kdialog`, or `window` |
| `ftlman` | | Path to ftlman, if it isn't in `ftlman/` next to `ftl-linux`, next to it, or in `libexec` |
| `hyperspace_lib` | | Hyperspace's library, if it isn't in the game folder, next to `ftl-linux` or in `libexec` |

Controller actions for `pad_<button>`: `none tap right_click back pause zoom_in
zoom_out reset_zoom magnifier turbo precision`. The defaults: A tap, B and
Start back, X pause, LB/RB zoom out/in, LT magnifier, RT right click, Select and
R3 reset the zoom.

## Mouse and keyboard

The iPad interface is made for touch, so the left mouse button acts as a
finger: click to tap, hold and drag to drag. On top of that:

| Input | |
|---|---|
| Mouse wheel (or two-finger scroll on a touchpad) | Zoom in and out at the pointer |
| Middle button, held | Drag the zoomed-in view around |
| Right button | Back: close the window or open the game menu (a right click for mods that listen for one) |
| Escape | Close the window or open the menu |
| F11, Alt+Enter | Fullscreen on/off |
| The PC version's keys | Space pause, J jump, 1–8 weapons, A/S/D/F/W... systems, Shift removes power, F1–F8 crew, V autofire, Z/X doors, U/C/I ship screens, T/R teleporter, L lockdown |

Hotkeys show in tooltips once a key has been pressed. Closing the window saves
and quits like iOS does when an app ends; minimizing the window pauses the game.

The game draws its canvas 768 units tall and as wide as the window's shape, at
1536 pixels on big windows, so it is sharp on HiDPI displays (the window is
created at its real pixel size). The shape is fixed when the game starts: the
default window has the screen's shape so fullscreen fills the screen. A window
resized to another shape shows the game with bars.

## Touchscreens and controllers

Touchscreens work as on the iPad, plus: hold a finger to magnify small buttons
(slide onto one, then lift), pinch and two-finger drag to zoom and pan, a
two-finger tap pauses, and for mods that use one, one finger held and a tap with
another is a right click (to drag, keep the second finger down while you move
the first). To open and close doors, tap the door button in the bottom-left
corner, then tap or drag across doors. Once a touchscreen is used, the touch
helpers set to `auto` switch on, and a mouse click switches them back off.

Controllers go through SDL3's gamepad support (Xbox, PlayStation, Switch Pro,
8BitDo and others SDL knows). A controller moves a cursor around the game, and A
taps where it points. The cursor slows down over things you can tap (buttons,
rooms, crew, doors, beacons, weapons, and the buttons Hyperspace and mods add)
and settles onto them when you let go of the stick.

| Button | What it does |
|---|---|
| Left stick, d-pad | Move the cursor. A d-pad press moves it a little; held, it speeds up |
| A | Tap, or hold to drag |
| B, Start | Back |
| X | Pause |
| LB, RB | Zoom out, zoom in |
| Right stick | Look around while zoomed |
| LT (hold) | Magnifier |
| RT | Right click, for mods that use one. Hold and move to drag |
| Select, R3 | Reset the zoom |

On a PlayStation controller, A is Cross, B is Circle and X is Square.
`pad_<button>` in settings.ini changes them, and can also give one the job of
making the cursor faster (`turbo`) or slower and free of the pull (`precision`)
while it's held. A DualSense or DualShock 4 touchpad moves the cursor like a
laptop's. The controller rumbles when you're hit, when you fire and when you
jump, and the game pauses if it disconnects.

## Known limitations

- **Hovering** shows nothing: the iPad build has no mouse-over tooltips, so
  information shows when you tap, as on the iPad.
- **Window shape**: the canvas is fixed at start; resizing to another shape
  shows bars. Restart to use the new shape.
- **Text prompts** use zenity or kdialog when installed and not in fullscreen;
  otherwise you type into the window (the title bar shows the text).
- **Fractional scaling**: with a fractional scale the window can be a pixel off
  the screen's size (2561×1601 on a 2560×1600 panel at 150%); the game is scaled
  by that pixel.
- Only the iPad 1.6.21 build gets the hooks (keys, mods' images, pause gesture);
  other builds run without them.
- No Flatpak or AppImage yet (the portable folder is the self-contained way for now).
