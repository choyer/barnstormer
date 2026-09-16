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

Requirements, all of which a Wayland desktop already has: `wayland-client`,
`wayland-scanner`, `xkbcommon`, and `alsa-lib` for sound (optional -- without
it the game builds and runs silently). `python3` is needed only by
`make regen-data`. There is no SDL, no GTK, no GL and no image or font
library; see [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) for the dependency
table and the rest of the build and test entry points.

Breakout mode additionally needs a compositor that implements
`wlr-layer-shell-unstable-v1` — Hyprland, Sway, river, niri, Wayfire and
friends. If it is missing, the game says so and opens a window instead.

## Playing

```sh
barnstormer                       # title screen, pick a mode
barnstormer --breakout            # straight into the overlay
barnstormer --computer --game 3   # skip the menu, start at difficulty 3
barnstormer --map salt-flats.map  # fly a map file instead of the classic one
barnstormer --help
```

It prints the map's name and a short hash (`flying "Salt Flats" by carl
[ebae6cce]`) — two people can compare those to be sure they are flying the
same map, and the editor prints the same hash when it writes a file.

`--map` takes a map file in the format described in
[doc/MAP_FORMAT.md](doc/MAP_FORMAT.md) — plain text, hand-editable, and
strictly checked: a file that would make a broken world is refused with the
line that is wrong rather than loaded. Runs on an authored map are not
ranked, because the high score boards are scores made on the classic map —
a map with four buildings and no enemy would top them without meaning
anything. What such a run gets instead is a personal best per map, kept
against the map's own hash and shown in the **BEST** column of the picker,
so a map of your own has something to beat without anything to farm.
Maps you collect go in `~/.local/share/barnstormer/maps` (or
`$XDG_DATA_HOME/barnstormer/maps`). Anything with a `.map` extension in
there is offered by **Play Map** on the title screen — pick one, then pick
a mode and fly it. A file that will not load is not silently dropped: the
picker says how many are there and refusing to load.

## Building a map

```sh
barnstormer --edit my-field.map    # opens it, or starts it if it is not there
```

| Key | Action | | Key | Action |
|-----|--------|-|-----|--------|
| `←` `→` | move the cursor (hold to run) | | `Space` | place |
| `↑` `↓` | raise / lower the ground | | `Backspace` | remove |
| `[` `]` | brush width | | `t` | terrain, building, runway, ox |
| `f` | flatten under the brush | | `k` | which kind, or which way it faces |
| `s` | smooth | | `n` `a` | name and author |
| `g` | pick up what is under the cursor, `g` again to put it down | | `w` | write the file |
| `Tab` | fly it — and again to come back | | `Esc` | leave (twice, if unsaved) |

`g` picks up whatever is under the cursor and carries it until you put it
down; Esc puts it back where it came from. What you are carrying stays part of
the map, so it will not be carried anywhere it could not have been placed —
it stops, the cursor carries on, and it catches up when the way is clear.

`n` and `a` open the map's name and author for typing: Enter keeps what you
typed, Esc leaves the field as it was. A map cannot be left without a name,
but an author is optional and can be cleared.

The editor writes wherever you tell it to, creating the directory if it needs
to, so `--edit ~/.local/share/barnstormer/maps/my-field.map` puts a map
straight into the picker.

The map you are building is always a map: a change that would break one of
the format's rules — a building across a landing strip, ground dug out from
under a runway — is refused and the status line says which rule, so `Tab` is
always ready and what you save always loads.

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
with a run in another, since single player is capped at 2,175 a map while
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

The simulation advances 12.14 times a second, as the original's did, and the
game draws between those positions so that an eight-times magnified step does
not read as judder. Nothing about the simulation changes -- the physics,
collisions and the deterministic replay hash are identical either way -- and
`--no-smooth` draws only the positions the simulation produces, which is what
the original did. The offsets and why they differ for ordnance are in
[doc/ARCHITECTURE.md](doc/ARCHITECTURE.md).

### Breakout mode

The overlay grabs the keyboard while it runs, because otherwise your
keystrokes would go to the window underneath. `F2` puts the game back in a
window, which releases the keyboard. `Esc` steps back out one level at a time
— run, score, title screen — and a last `Esc` from the title quits and
releases it too. `--no-grab` leaves the keyboard with the desktop, which makes the
overlay a display rather than a game — useful for watching the computer pilots
fight it out over your work.

Anything that asks for the keyboard exclusively takes it from the overlay --
the Omarchy menu and the screenshot picker both do -- and the compositor
cannot give it back by itself. The game pauses the moment the keyboard goes
and asks for it back as soon as the compositor returns the pointer;
`SIGUSR1` asks again if a compositor ever leaves it stranded. See
[doc/PLATFORM.md](doc/PLATFORM.md) for why, and for the keybind.

### Hyprland and the Omarchy menu

A tiling compositor will hand the window whatever shape the layout dictates,
and a 320×200 game in a tall column leaves a lot of sky, so floating it is
nicer; the game also supports `wp-fractional-scale-v1` and rasterises at
device resolution. `make install` puts two `.desktop` entries and their icons
in place, so both ways to play show up under **Apps** in the launcher and the
Omarchy menu with no further setup. The window rule to paste into
`~/.config/hypr/hyprland.lua`, the entry table and the icon details are in
[doc/PLATFORM.md](doc/PLATFORM.md).

## How it is put together

The seam that matters is between `game/` and everything else. `game_tick()`
takes one 16-bit control word per player and advances the world; it touches no
I/O, and every random choice runs off a seed in `game_t`. That is what lets
the same core drive both front ends, and it is what a future netplay layer
plugs into.

The tree, that seam's invariants, the front end's states and the build and
test entry points are in [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md);
[doc/](doc/README.md) indexes the rest of the design notes.

## What is planned but not built

See [doc/ROADMAP.md](doc/ROADMAP.md). In short: an enhanced-graphics pack, a
map editor with save/load/share, and internet multiplayer. The interfaces
they need already exist (`sprite_set_t`, `map_t`, `net.h`); none of the
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
