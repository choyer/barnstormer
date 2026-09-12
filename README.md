# Sopwith Barnstormer

A re-implementation of David L. Clark's 1984 biplane game for Wayland, written
in C with no toolkit, no engine and no GPU.

It plays two ways:

* **Classic** — an ordinary window.
* **Breakout** — a transparent full-screen overlay. The sky is genuinely
  see-through, so the aeroplanes, the buildings and the landscape sit on top of
  whatever you were already doing. The pointer passes straight through, so the
  desktop underneath stays usable.

The flight model, gunnery, collision handling, autopilot, terrain and artwork
all come from the original DOS sources in `../origsrc`; see
[doc/ORIGINAL.md](doc/ORIGINAL.md) for exactly what was kept, what was changed
and why.

## Building

```sh
make          # -> build/barnstormer
make test     # headless soak + high score tests (ASan + UBSan)
make test-ui  # drives the real binary through every screen (needs
              # a Wayland session and wtype; takes the keyboard)

# Install for your user only -- no root needed, and ~/.local/bin is already
# on the session PATH on Omarchy.  Installs the binary, a .desktop entry and
# an icon, so the game appears in the app launcher and under the Omarchy
# menu's Apps section.
make install PREFIX="$HOME/.local"

# Or system-wide (needs root):
sudo make install          # PREFIX=/usr/local by default
```

Requirements, all of which a Wayland desktop already has:

| Dependency          | Why                                    | Required? |
|---------------------|----------------------------------------|-----------|
| `wayland-client`    | the only display path                  | yes       |
| `wayland-scanner`   | generates protocol glue at build time  | yes       |
| `xkbcommon`         | keyboard decoding                      | yes       |
| `alsa-lib`          | PC-speaker emulation                   | optional  |
| `python3`           | only for `make regen-data`             | no        |

Without ALSA the game builds and runs silently. There is no SDL, no GTK, no
GL and no image or font library: the renderer is a software rasteriser writing
into a shared-memory buffer, and the font and sprites are compiled in.

Breakout mode additionally needs a compositor that implements
`wlr-layer-shell-unstable-v1` — Hyprland, Sway, river, niri, Wayfire and
friends. If it is missing, the game says so and opens a window instead.

## Playing

```sh
barnstormer                       # title screen, pick a mode
barnstormer --breakout            # straight into the overlay
barnstormer --computer --game 3   # skip the menu, start at difficulty 3
barnstormer --help
```

| Key       | Action                                       |
|-----------|----------------------------------------------|
| `,`       | pull up   (also `Up`)                        |
| `/`       | dive      (also `Down`)                      |
| `.`       | flip the aircraft over (also `Return`)       |
| `x` / `z` | throttle up / down (also `Right` / `Left`)   |
| `Space`   | machine guns                                 |
| `b`       | bomb                                         |
| `v`       | missile                                      |
| `c`       | flare — decoys an incoming missile           |
| `h`       | fly home and land                            |
| `s`       | sound on/off                                 |
| `d`       | throttle and airspeed dials (remembered)     |
| `p`       | pause                                        |
| `r`       | restart the current game                     |
| `F2`      | switch between window and overlay            |
| `Esc`     | parked at home: retire and keep the score    |
|           | in the air: abandon the run, score forfeit   |
|           | on the score screen: the menu, then quit     |

### High scores

Ten entries a board, three initials each, and a board apiece for novice,
single player and against the computer -- a run in one mode is not comparable
with a run in another, since single player is capped at 2,175 a level while
the computer board is not capped at all.

A run ends in one of three ways. Your fifth crash is `GAME OVER` and counts.
Flying home, landing and pressing `Esc` while parked is a `RETIRED` run and
also counts -- it is how you bank a score without throwing the aircraft at the
ground. Pressing `Esc` in the air is `ABANDONED` and forfeits the score. All
three show you what you scored and the board it was measured against.

Scores live in `~/.local/share/barnstormer/scores` (or `$XDG_DATA_HOME`), are
written the moment an entry is committed, and survive updates -- nothing the
package installs writes to that directory. It is a plain text file; if it goes
missing the built-in defaults come back.

Level every enemy building to win; the difficulty then goes up a notch, enemy
aircraft get faster and the anti-aircraft batteries reach further. Five crashes
and the run is over. Flying into your own buildings, your own oxen or the
birds costs you points.

The aircraft stalls if you climb past the ceiling or let the airspeed fall
away, and it will not fly upside down without the stick input to match — the
1984 flight model is unforgiving on purpose. Novice mode turns off stalls,
wildlife and ammunition limits.

### Motion

The simulation advances 12.14 times a second, as the original's did, and on a
320x200 screen a 4-12 pixel step a tick was near invisible. Magnified eight
times onto a full-screen overlay it is 32-96 pixels, twelve times a second,
against a perfectly still desktop -- which reads as judder.

The game therefore draws between those positions. Nothing about the
simulation changes: it still advances exactly 12.14 times a second, the
physics and collisions are identical, and the deterministic replay test hashes
the same either way. Only where things are painted moves.

Aircraft, scenery and the camera are offset half a tick either side of their
simulated position, so the display's timing averages out to the original's
exactly -- no added latency in either direction. Shots, bombs and missiles are
offset backwards instead, drawn between where they were and where they are:
they die the instant they touch something, and drawing one ahead would put it
through the wall that is about to stop it.

`--no-smooth` turns all of it off and draws only the positions the simulation
produces, which is what the original did.

### Breakout mode

The overlay grabs the keyboard while it runs, because otherwise your
keystrokes would go to the window underneath. `F2` puts the game back in a
window, which releases the keyboard. `Esc` steps back out one level at a time
— run, score, title screen — and a last `Esc` from the title quits and
releases it too. `--no-grab` leaves the keyboard with the desktop, which makes the
overlay a display rather than a game — useful for watching the computer pilots
fight it out over your work.

Anything that asks for the keyboard exclusively takes it from the overlay: the
Omarchy menu and the screenshot picker both do. The compositor cannot give it
back afterwards, because it looks for the surface under the pointer and the
overlay has no input region to be found by, so the game would be left deaf
with the aircraft still flying. It therefore pauses the moment the keyboard
goes, asks for it back as soon as the compositor returns the pointer, and
resumes — in practice a few milliseconds after the menu closes. If a
compositor ever leaves it stranded, `SIGUSR1` asks for the keyboard again:

```lua
o.bind("SUPER + SHIFT + K", "Barnstormer: reclaim keyboard",
       "pkill -USR1 -x barnstormer")
```

### Hyprland

A tiling compositor will hand the window whatever shape the layout dictates,
and a 320×200 game in a tall column leaves a lot of sky. Floating it is nicer.
Omarchy configures Hyprland in Lua, so this goes in `~/.config/hypr/hyprland.lua`:

```lua
o.window("^(barnstormer)$", {
  float = true,
  center = true,
  size = { 1280, 800 },
  tag = "-default-opacity",
  opacity = "1 1",
})
```

The last two lines opt out of Omarchy's default window transparency, which
would otherwise wash the artwork out. Overlay mode is a layer surface rather
than a window, so the rule does not apply to it.

The game supports `wp-fractional-scale-v1`, so on a fractionally scaled output
it rasterises at device resolution rather than being resampled.

### The Omarchy menu

`make install` puts two `.desktop` entries and their icons in place, so both
ways to play show up under **Apps** in the launcher and the Omarchy menu with
no further setup:

| Entry                 | Runs                     | Icon                  |
|-----------------------|--------------------------|-----------------------|
| `Sopwith Barnstormer` | `barnstormer`            | the player's aircraft |
| `Barnstormer Overlay` | `barnstormer --breakout` | the enemy's, in pink  |

Both icons are generated from the same sprite the game draws by
`tools/make_icon.py`, which takes the livery as its third argument, so they
cannot drift out of step with the artwork.

A row added to `~/.config/omarchy/extensions/omarchy-menu.jsonc` would be a
glyph rather than an image: the menu renders an icon image only for rows of
`kind: "app"`, which is derived from the desktop entry list and cannot be set
from the JSONC. A desktop entry is what gets you a real icon.

## Layout

```
include/     public interfaces, one per subsystem
game/        the simulation: no windows, no files, no audio device
data/        artwork and the stock level, generated from ../origsrc
render/      software rasteriser and scene composition
platform/    the Wayland backend (xdg-shell and wlr-layer-shell)
audio/       PC-speaker emulation over ALSA
protocol/    vendored Wayland protocol XML
tools/       the extractors that regenerate data/
tests/       headless soak test
doc/         design notes and the plans for what comes next
```

The seam that matters is between `game/` and everything else. `game_tick()`
takes one 16-bit control word per player and advances the world; it never
touches I/O, and every random choice runs off a seed in `game_t`. That is what
lets the same core drive both front ends, and it is what a future netplay layer
plugs into.

## What is planned but not built

See [doc/ROADMAP.md](doc/ROADMAP.md). In short: an enhanced-graphics pack, a
level editor with save/load/share, and internet multiplayer. The interfaces
they need already exist (`sprite_set_t`, `level_t`, `net.h`); none of the
features are implemented.

## Licence

The original Sopwith is Copyright © 1984-2000 David L. Clark and is
distributed under the terms in [LICENSE.origsopwith.txt](LICENSE.origsopwith.txt),
which this work follows: the copyright notice appears on the title screen, the
licence travels with the code, and the modifications are described in
[doc/ORIGINAL.md](doc/ORIGINAL.md).

The name is this project's own. Sopwith Barnstormer is neither David L. Clark's
original nor Simon Howard's SDL port — which is the one that ships as `sopwith`
in the distributions — so it installs as `barnstormer` and stays out of their
way.
