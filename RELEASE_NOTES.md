# Barnstormer 1.4.0

Sopwith re-implemented for Wayland: David L. Clark's 1984 biplane dogfight,
in a window or flying as a transparent overlay across your desktop.

The 1984 flight model is intact — stalls, inverted flight, the ceiling, the
fixed 12.14 moves a second — and so is the artwork, extracted from the
original sources rather than redrawn. The renderer is a software rasteriser;
the only libraries linked are libwayland-client and libxkbcommon, plus ALSA
for sound. No toolkit, no GL, no SDL.

## Changes in 1.4.0

### Build your own maps

`barnstormer --edit my-field.map` opens a map editor, or starts a map if
that file is not there yet. A terrain brush that raises, lowers, smooths and
flattens; the four building types, runways and oxen; the map's name and
author typed in place; `g` to pick something up and carry it somewhere else;
and `Tab` to fly what you are looking at, `Tab` again to come back.

The map under construction is always a map. Every change is checked
against the loader's own rules and undone if it would break one, with the
reason on the status line -- "cannot raise: the runway at 400 is not flat".
So the test flight is always available, and what you save always loads.

### Playing them

Maps live in `~/.local/share/barnstormer/maps`, and the title screen's
fourth row opens a picker over them: it offers the ones that load, sorted by
name, and says how many files are there and refusing rather than hiding them.
`barnstormer --map FILE` flies one straight from the command line.

Runs on an authored map are deliberately not ranked. The high score boards
are three columns of scores made on the classic map, and a map with four
buildings and no enemy would top them without meaning anything.

### The map file

Plain text, hand-editable and diffable, specified in
[doc/MAP_FORMAT.md](doc/MAP_FORMAT.md): a run-length encoded height field
and the placement of runways, buildings and cattle. The loader is strict --
maps get passed between strangers, and a file that would make a broken world
is refused with the line that is wrong rather than loaded half-valid.

`map_hash()` is FNV-1a over the canonical form, printed when the editor
writes a file and when `--map` loads one, so two people can check they hold
the same map however their copies are laid out. The classic map is
`c22d60a3`.

### A best per map

The three boards stay what they are: scores flown on the classic map. A run
on a map somebody wrote is still not ranked against them -- an easy map
would top them without meaning anything -- but it is no longer thrown away
either. Each map keeps the best score you have flown on it, stored against
`map_hash()` rather than a filename, so two copies of a map share a best
and an edited map is a different map. The picker shows it in a **BEST**
column beside the author, and the end-of-run screen says what there is to
beat when the run did not rank.

Sixty-four maps are remembered; past that the least recently beaten makes
way, so the ones being flown are the ones kept.

### Maps, not levels

What was a level is now a map: the menus say so, the command-line option is
`-m, --map`, and the files live in `~/.local/share/barnstormer/maps` with a
`.map` extension. Nothing is migrated for you -- a `levels` directory from
an older build is not read, so move what is in it across and rename the
files. They still load once they are there: the reader accepts the
superseded `barnstormer-level 1` header, though it never writes it. Because
that header is part of the bytes that are hashed, every map's hash changed
with the rename; the classic map is now `c22d60a3`.

### Fixed

The count of enemy buildings still standing was a constant rather than a count
of the buildings on the map, so any map with fewer than twenty of them
could never be cleared and the `TARGETS` readout started wrong. It is now
tallied as the buildings go up. The classic map is unaffected -- it has
exactly twenty.

A building or an ox that had been destroyed was taken off the screen but left
in the collision list, so the square of empty sky where it had stood still
brought an aircraft down. Destroying one now removes it from that list, which
is what the game already did with an aircraft that had left the run. Flying
into something still standing, or into the explosion that has just taken it
down, is unchanged.

## Changes in 1.3.0

### Throttle and airspeed dials

Press `d` for a strip above the panel showing what the throttle is set to and
what the aircraft has actually got. It is off by default and the setting is
remembered between runs.

The throttle is five positions but only four units of thrust above the
minimum, so it reads as four pips: none lit is idle, all four is full. The
airspeed bar beneath it carries three things at once -- the fill is the
current airspeed, the bright tick is the speed the throttle asked for, and the
shaded region is below the stall floor. The gap between the fill and the tick
is the throttle lag, which closes one step every fourth tick; the fill turns
red once it drops into the shaded part, which is the stall arriving with about
a second of warning.

None of that information is new. It was always in the simulation and simply
had no way of being seen, which is why the flight model has always been
harder to read than to fly. The stall shading is hidden in novice mode, which
cannot stall at all.

### A more legible title screen

The control list now draws each key in white against its action in grey, laid
out on columns measured from the keys themselves rather than a fixed width, so
nothing is stranded halfway across the screen from what it does. The block is
a quarter narrower as a result, which leaves the rest of the title screen
larger on a small window.

## Installing

**Arch and Omarchy** — build the package, which tracks the files and
uninstalls cleanly:

```bash
git clone https://github.com/choyer/barnstormer
cd barnstormer/packaging && makepkg -si
```

**From the tarball** — `barnstormer-1.4.0-x86_64.tar.gz`:

```bash
tar xzf barnstormer-1.4.0-x86_64.tar.gz
cd barnstormer-1.4.0-x86_64 && ./install.sh      # installs to ~/.local
```

**From source** — `make && make install PREFIX=$HOME/.local`.

Either route installs both desktop entries and their icons, so **Sopwith
Barnstormer** and **Barnstormer Overlay** appear under Apps in the launcher
and the Omarchy menu.

## Two ways to play

- **Window** — `barnstormer`, an ordinary xdg-shell toplevel.
- **Breakout** — `barnstormer --breakout`, a wlr-layer-shell overlay with a
  transparent sky, the game is painted over your desktop while the pointer
  still reaches whatever is underneath. `--no-grab` makes it a display rather
  than a game, for watching the computer pilots fight it out over your work.

`F2` switches between the two at run time. The game supports
`wp-fractional-scale-v1`, so on a fractionally scaled output it rasterises at
device resolution instead of being resampled.

## Overlay keyboard recovery

Anything that takes the keyboard exclusively — the Omarchy menu, the
screenshot picker — takes it from the overlay, and the compositor cannot hand
it back, because it looks for the surface under the pointer and a click-
through overlay has no input region to be found by. Barnstormer pauses the
moment the keyboard goes, reclaims it as soon as the compositor returns the
pointer, and resumes: a few milliseconds after the menu closes, measured on
Hyprland 0.56.2. `SIGUSR1` asks for the keyboard back by hand if a compositor
ever leaves it stranded:

```lua
o.bind("SUPER + SHIFT + K", "Barnstormer: reclaim keyboard",
       "pkill -USR1 -x barnstormer")
```

## What the binary needs

The prebuilt binary is dynamically linked, built in a clean `archlinux:base-devel`
container, and needs:

| | |
|---|---|
| glibc | 2.34 or newer |
| libraries | `libwayland-client.so.0`, `libxkbcommon.so.0`, `libasound.so.2` |
| compositor | one that implements `wlr-layer-shell-unstable-v1`, for overlay mode |

It is built and tested on Arch and Omarchy. The glibc floor is low enough
that it should also run on Debian 12, Ubuntu 22.04 and Fedora 35 or newer,
though those are untested — build from source if it does not start. ALSA is
resolved at build time: this build requires `libasound.so.2`, and a build made
without alsa-lib present simply has no sound.

Without layer-shell the game still runs in a window; it prints a notice and
falls back.

Verify the download against `barnstormer-1.4.0-x86_64.tar.gz.sha256`.

## Credits

The original Sopwith is © 1984 BMB Compuscience Canada Ltd., released under
the terms in `LICENSE.origsopwith.txt`. 
This re-implementation, Barnstormer created by Carl Hoyer, is GPL-3.0-or-later.
