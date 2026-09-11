# Barnstormer 1.2.0

Sopwith re-implemented for Wayland: David L. Clark's 1984 biplane dogfight,
in a window or flying as a transparent overlay across your desktop.

The 1984 flight model is intact — stalls, inverted flight, the ceiling, the
fixed 12.14 moves a second — and so is the artwork, extracted from the
original sources rather than redrawn. The renderer is a software rasteriser;
the only libraries linked are libwayland-client and libxkbcommon, plus ALSA
for sound. No toolkit, no GL, no SDL.

## Changes in 1.2.0

### The overlay no longer judders

The simulation advances 12.14 times a second, exactly as the original's did.
On a 320x200 screen a 4-12 pixel step a tick was near invisible; magnified
eight times onto a full-screen overlay it is 32-96 pixels, twelve times a
second, against a perfectly still desktop. That reads as judder, and it was
the single worst thing about breakout mode.

The renderer now draws between those positions, offsetting each object from
its simulated position by a fraction of a tick scaled by the velocity the
simulation already carries. Nothing has to be remembered between ticks, and
objects that spawn or teleport need no special case.

Aircraft, scenery and the camera are offset half a tick either side, so the
display's timing averages the original's exactly -- smoothness without trading
judder for input lag. Shots, bombs and missiles are offset backwards instead,
drawn between where they were and where they are: they die the instant they
touch something, and drawing one ahead put it visibly through the wall that
was about to stop it.

`--no-smooth` turns it all off and draws only the positions the simulation
produces, as the original did.

**The simulation is untouched.** It still advances 12.14 times a second, the
physics and collisions are identical, the deterministic replay test hashes the
same, and `--no-smooth` renders byte-identically to 1.1.0. Only where things
are painted changed.

### Also

`packaging/install.sh` is now part of the repository rather than only existing
inside a built tarball, so a release can be cut from a clean clone.

## Installing

**Arch and Omarchy** — build the package, which tracks the files and
uninstalls cleanly:

```bash
git clone https://github.com/choyer/barnstormer
cd barnstormer/packaging && makepkg -si
```

**From the tarball** — `barnstormer-1.2.0-x86_64.tar.gz`:

```bash
tar xzf barnstormer-1.2.0-x86_64.tar.gz
cd barnstormer-1.2.0-x86_64 && ./install.sh      # installs to ~/.local
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

Verify the download against `barnstormer-1.2.0-x86_64.tar.gz.sha256`.

## Credits

The original Sopwith is © 1984 BMB Compuscience Canada Ltd., released under
the terms in `LICENSE.origsopwith.txt`. 
This re-implementation, Barnstormer created by Carl Hoyer, is GPL-3.0-or-later.
