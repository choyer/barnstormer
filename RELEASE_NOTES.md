# Barnstormer 1.1.0

Sopwith re-implemented for Wayland: David L. Clark's 1984 biplane dogfight,
in a window or flying as a transparent overlay across your desktop.

The 1984 flight model is intact — stalls, inverted flight, the ceiling, the
fixed 12.14 moves a second — and so is the artwork, extracted from the
original sources rather than redrawn. The renderer is a software rasteriser;
the only libraries linked are libwayland-client and libxkbcommon, plus ALSA
for sound. No toolkit, no GL, no SDL.

## Changes in 1.1.0

### High scores

A local arcade leaderboard: ten entries a board, three initials each, and a
board apiece for novice, single player and against the computer. A run in one
mode is not comparable with a run in another -- single player is capped at
2,175 a level, while the computer board has no ceiling at all -- so they are
kept apart. Each board ships with built-in defaults, and every finished run
shows you what you scored against the board it was measured on.

Initials are entered the way a cabinet does it: three cells, A-Z and space,
Up and Down to cycle, Left and Right to move, and a blinking caret on the
cell you are on. Typing the letter works too, because this is a keyboard.

Scores live in `$XDG_DATA_HOME/barnstormer/scores` (`~/.local/share` by
default), written the moment an entry is committed, and survive updates --
nothing the package installs writes to that directory. The file is plain
text. If it goes missing the built-in defaults come back; if it is damaged it
is moved aside rather than overwritten.

### A run can now end

Your fifth crash used to rebuild the world with the score silently reset to
zero, so a run had no end and nothing to record. It now ends:

| How it ends | Shows | Ranked? |
|---|---|---|
| Fifth crash | `GAME OVER` | yes |
| `Esc` parked at your own airfield | `RETIRED` | yes |
| `Esc` in the air | `ABANDONED` | no |

Flying home and landing is how you bank a score without throwing the aircraft
at the ground five times. Retiring is a deliberate press rather than automatic
on touchdown, because landing is also how you refuel and rearm.

This is a change to the simulation, not just the interface: the soak tests now
report the difficulty they were started at instead of 0, because a death no
longer resets the run.

### Also

`make test-ui` drives the real binary through every screen with synthetic key
events, which is the only way to test a state machine that exists in response
to real input.

## Installing

**Arch and Omarchy** — build the package, which tracks the files and
uninstalls cleanly:

```bash
git clone https://github.com/choyer/barnstormer
cd barnstormer/packaging && makepkg -si
```

**From the tarball** — `barnstormer-1.1.0-x86_64.tar.gz`:

```bash
tar xzf barnstormer-1.1.0-x86_64.tar.gz
cd barnstormer-1.1.0-x86_64 && ./install.sh      # installs to ~/.local
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

Verify the download against `barnstormer-1.1.0-x86_64.tar.gz.sha256`.

## Credits

The original Sopwith is © 1984 BMB Compuscience Canada Ltd., released under
the terms in `LICENSE.origsopwith.txt`. 
This re-implementation, Barnstormer created by Carl Hoyer, is GPL-3.0-or-later.
