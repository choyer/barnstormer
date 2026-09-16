# How the code is put together

The shape of the tree, the one seam that matters, and the two timing decisions
that everything else is measured against. This is the file to read before
changing anything in `game/` or `render/`.

## Layout

```
include/     public interfaces, one per subsystem
game/        the simulation, plus map and score files: no windows, no audio
data/        artwork and the stock map, generated from ../origsrc
render/      software rasteriser and scene composition
platform/    the Wayland backend (xdg-shell and wlr-layer-shell)
audio/       PC-speaker emulation over ALSA
protocol/    vendored Wayland protocol XML
tools/       the extractors that regenerate data/, and release helpers
packaging/   PKGBUILD and the standalone install script
tests/       the headless suites and the UI flow script
doc/         design notes and the plans for what comes next
```

## The seam

The seam that matters is between `game/` and everything else. `game_tick()`
takes one 16-bit control word per player and advances the world; it never
touches I/O, and every random choice runs off a seed in `game_t`. That is what
lets the same core drive both front ends, and it is what a future netplay
layer plugs into (`include/net.h`, and §3 of [ROADMAP.md](ROADMAP.md)).

Two consequences worth stating out loud, because they are easy to break:

* **A run is reproducible from its inputs.** `tests/simtest.c` plays the same
  3000 ticks twice and compares `net_state_hash()`; anything that reads the
  clock, the filesystem or an uninitialised value inside `game/` breaks that
  test rather than failing quietly.
* **Collision reads the classic sprite frames, always.** `obj_pixel()` in
  `game/collision.c` deliberately indexes `sw_sprite_sets` rather than any
  themed table, so artwork cannot change how the game plays. §1 of
  [ROADMAP.md](ROADMAP.md) depends on this staying true.

`game_t.map` is the only description of the world the simulation takes
(`include/map.h`), so a map from a file and the built-in `map_classic` are the
same thing to it.

## Motion: the simulation tick versus the drawn frame

The simulation advances 12.14 times a second, as the original's did
(`GAME_TICK_HZ`; see [ORIGINAL.md](ORIGINAL.md) for where that number comes
from). On a 320x200 screen a 4-12 pixel step a tick was near invisible.
Magnified eight times onto a full-screen overlay it is 32-96 pixels, twelve
times a second, against a perfectly still desktop -- which reads as judder.

The game therefore draws between those positions. Nothing about the
simulation changes: it still advances exactly 12.14 times a second, the
physics and collisions are identical, and the deterministic replay test hashes
the same either way. Only where things are painted moves.

Aircraft, scenery and the camera are offset half a tick either side of their
simulated position (`render_ctx_t.lead_ticks`), so the display's timing
averages out to the original's exactly -- no added latency in either
direction. Shots, bombs and missiles are offset backwards instead
(`trail_ticks`), drawn between where they were and where they are: they die
the instant they touch something, and drawing one ahead would put it through
the wall that is about to stop it.

Zero means "exactly the state the tick produced" for both, which is what
`--no-smooth` restores. Keeping the units as ticks rather than a 0..1 phase is
deliberate: it means changing the smoothing scheme cannot quietly change which
value disables it.

## The front end's states

`main.c` is a state machine over `uistate_t`: title, map picker, play, paused,
end-of-run summary, initials entry, the map editor, and the attract cycle.
Two timings live there rather than in `game/`, because they are about a person
in front of a screen and not about the world:

* The title screen left alone for `TITLE_IDLE_SECS` (65) starts cycling the
  three high score boards, `ATTRACT_BOARD_SECS` (15) each, until a key brings
  the menu back. The key that dismisses it only dismisses it -- Esc must not
  quit the game on the way back from a screen nobody asked for.
* Both run on real seconds, not frames, so they take as long on a 60 Hz panel
  as on a 144 Hz one.

Rendering is pull-based: every screen is a function in `render/scene.c`, and
the map picker, the boards and the editor take a snapshot struct (`mappick_t`,
`scoreboard_t`, `editview_t`) rather than the live model, which is what keeps
the renderer ignorant of how editing or scoring works.

## Dependencies, and what is deliberately absent

| Dependency          | Why                                    | Required? |
|---------------------|----------------------------------------|-----------|
| `wayland-client`    | the only display path                  | yes       |
| `wayland-scanner`   | generates protocol glue at build time  | yes       |
| `xkbcommon`         | keyboard decoding                      | yes       |
| `alsa-lib`          | PC-speaker emulation                   | optional  |
| `python3`           | only for `make regen-data`             | no        |

Without ALSA the game builds and runs silently. There is no SDL, no GTK, no
GL and no image or font library: the renderer is a software rasteriser writing
into a shared-memory buffer, and the font and sprites are compiled in. The
5x7 font is 95 glyphs in `render/font.c`; the score and map-picker columns
are aligned by padding the row strings rather than measuring them, because
the font advances a fixed six units a character.

## Build and test entry points

```sh
make                       # -> build/barnstormer
make test                  # simtest, scoretest, maptest, edittest (ASan + UBSan)
make test-ui               # drives the real binary through every screen
make install PREFIX=...    # binary, desktop entries, icons, licences
make regen-data            # re-extracts data/ from ../origsrc (needs python3)
make flytest               # can a map be flown out of?  not part of make test
make probe                 # measures the flight envelope; prints, never fails
```

`make test` is the gate: four headless suites, no compositor needed, with the
map format held to [MAP_FORMAT.md](MAP_FORMAT.md) and the scores to
[LEADERBOARD.md](LEADERBOARD.md).

`make test-ui` needs a Wayland session and `wtype`, and it takes the keyboard
for about forty seconds. It is a real-input test, so it is only as reliable as
the compositor's focus handling: if synthetic keys are not reaching the game
window, every step that only checks "is it still alive" passes and the ones
that check for an exit or a written file fail. Before believing a failure,
check that no other `barnstormer` is running and that the game window can hold
focus (`input:follow_mouse` on Hyprland will hand focus back to whatever the
pointer is over).
