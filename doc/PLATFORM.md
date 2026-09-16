# The Wayland backend and the desktop around it

What the two presentation modes need from a compositor, why the overlay has to
grab the keyboard and hand it back, and the desktop-integration details that
took a while to find.

## Two surfaces, one backend

`platform/wl_backend.c` covers both modes. Classic mode opens an ordinary
xdg-shell toplevel. Breakout mode opens a `wlr-layer-shell-unstable-v1`
surface on the overlay layer with an empty input region, so the sky is
genuinely transparent (premultiplied `0x00000000`) and the pointer passes
through to the desktop underneath.

Breakout mode therefore needs a compositor that implements layer-shell --
Hyprland, Sway, river, niri, Wayfire and friends. If it is missing the game
says so and opens a window instead; `platform_has_overlay()` is the test, and
`F2` (`SWKEY_STYLE`) tears the surface down and rebuilds it in the other mode
at run time.

The backend supports `wp-fractional-scale-v1`, so on a fractionally scaled
output it rasterises at device resolution rather than being resampled.

## The keyboard, and getting it back

The overlay grabs the keyboard while it runs, because otherwise the
keystrokes would go to the window underneath. `F2` puts the game back in a
window, which releases the keyboard. `--no-grab` leaves the keyboard with the
desktop, which makes the overlay a display rather than a game -- useful for
watching the computer pilots fight it out over your work.

Anything that asks for the keyboard exclusively takes it from the overlay: the
Omarchy menu and the screenshot picker both do. **The compositor cannot give
it back afterwards**, because it looks for the surface under the pointer and
the overlay has no input region to be found by -- so the game would be left
deaf with the aircraft still flying.

The handling that follows from that is worth not rediscovering:

* `platform_input_lost()` goes true the moment the keyboard is taken, and
  `main.c` pauses (`UI_PAUSED`, with `deaf_pause` set so the screen says
  `KEYBOARD RELEASED` rather than `PAUSED`).
* The backend asks for the keyboard back as soon as the compositor returns the
  pointer, and the game resumes -- in practice a few milliseconds after the
  menu closes.
* If a compositor ever leaves it stranded, `SIGUSR1` asks again
  (`platform_regrab()`), which is what this keybind is for:

```lua
o.bind("SUPER + SHIFT + K", "Barnstormer: reclaim keyboard",
       "pkill -USR1 -x barnstormer")
```

## Hyprland

A tiling compositor will hand the window whatever shape the layout dictates,
and a 320x200 game in a tall column leaves a lot of sky. Floating it is
nicer. Omarchy configures Hyprland in Lua, so this goes in
`~/.config/hypr/hyprland.lua`:

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

## Desktop entries, icons, and the Omarchy menu

`make install` puts two `.desktop` entries and their icons in place, so both
ways to play show up under **Apps** in the launcher and the Omarchy menu with
no further setup:

| Entry                 | Runs                     | Icon                  |
|-----------------------|--------------------------|-----------------------|
| `Sopwith Barnstormer` | `barnstormer`            | the player's aircraft |
| `Barnstormer Overlay` | `barnstormer --breakout` | the enemy's, in pink  |

Both icons are generated from the same sprite the game draws, by
`tools/make_icon.py`, which takes the livery as its third argument, so they
cannot drift out of step with the artwork.

A row added to `~/.config/omarchy/extensions/omarchy-menu.jsonc` would be a
glyph rather than an image: the menu renders an icon image only for rows of
`kind: "app"`, which is derived from the desktop entry list and cannot be set
from the JSONC. **A desktop entry is what gets you a real icon** -- that is
why installation goes through `.desktop` files rather than a menu extension.

## Where the game's own files live

| Path                                        | What                        |
|---------------------------------------------|-----------------------------|
| `$XDG_DATA_HOME/barnstormer/maps`           | maps the picker offers      |
| `$XDG_DATA_HOME/barnstormer/scores`         | boards, bests, dials        |

`$XDG_DATA_HOME` falls back to `~/.local/share` (`include/paths.h`). Nothing
the package installs writes into that directory, so scores and maps survive
updates. Only `*.map` files are listed, and there is no migration from the
`levels` directory earlier builds used -- see the note in
[MAP_FORMAT.md](MAP_FORMAT.md) about the header token that changed with it.
