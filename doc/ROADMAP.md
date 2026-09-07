# Planned work

Three features are designed for but deliberately not implemented. This file
records the design so the shape of the code makes sense, and so whoever picks
them up is not starting from a blank page.

---

## 1. Enhanced graphics

**Goal.** Optional higher-resolution artwork, without losing the 1984 look for
people who want it.

**What already supports it.** Sprites are addressed entirely through
`sprite_set_t` (`include/sprites.h`): a name, a frame size, a frame count and a
byte-per-pixel buffer. Nothing outside `render/` knows the frames are 16×16, and
nothing anywhere knows they came from CGA. `render_ctx_t` already carries the
world-pixel scale, so a sprite set whose frames are 4× larger only needs the
blitter to divide its step by 4.

**What is missing.**

* A second sprite table, and a `--theme` switch that selects between them.
  Keep `sw_sprite_sets` as the `classic` theme; add `sw_sprite_sets_hd`.
* Sub-world-pixel blitting in `draw_object()`: currently one sprite pixel is
  one world pixel is `scale` device pixels. It needs a per-set
  `pixels_per_world_unit` so a 64×64 frame occupies the same 16×16 world
  extent.
* Collision must keep using the classic frames. `obj_pixel()` in
  `game/collision.c` deliberately reads `sw_sprite_sets`, not the themed table,
  and it must stay that way or the game plays differently per theme.
* Artwork. The colour indices 1/2/3 mean "team primary", "team accent" and
  "neutral"; an HD set should keep that convention so team colouring and the
  palette continue to work.

**Non-goals.** No shaders, no GPU path. The rasteriser handles 4K at 60 Hz
comfortably; adding a GL backend would cost more in dependencies than it buys.

---

## 2. Level builder

**Goal.** Design your own landscape and building layout, save it, load it,
share it. The reference for the feel is Excitebike's track editor: direct,
immediate, and playable from inside the editor.

**What already supports it.** `level_t` (`include/level.h`) is the only thing
`game_start()` takes: a height field plus arrays of runways, buildings and
oxen. `data/level_classic.c` is one of these and is not special-cased anywhere.
`level_load()`, `level_save()` and `level_free()` are declared and currently
fail with `ENOSYS`.

**What is missing.**

* The file format, specified in [LEVEL_FORMAT.md](LEVEL_FORMAT.md), and the
  two functions that read and write it.
* An editor mode: `barnstormer --edit FILE`. It reuses `render_frame()` for the
  world view and adds
  - a terrain brush (raise/lower/smooth, with the 26..199 clamp the game
    already assumes),
  - placement of the four building types, the two runways and the oxen,
  - validation (every runway needs 20 flat-ish columns; buildings need 16),
  - `Tab` to fly the level immediately and `Tab` again to return to editing.
* A level directory (`$XDG_DATA_HOME/barnstormer/levels`) and a picker on the
  title screen.

**Sharing.** A level is small — under 4 KB — so the sharing story is "send the
file". A base64 form short enough to paste into a chat window is worth having;
the format is designed so that gzip + base64 of a typical level fits in about
two lines.

**Ordering.** This should land before multiplayer: a shared level format is a
prerequisite for peers agreeing on what world they are in.

---

## 3. Internet multiplayer

**Goal.** Two to four pilots over the internet, in the spirit of the
original's serial-line and Imaginet modes.

**Model.** Deterministic lockstep. The simulation is already built for it:

* `game_tick()` consumes exactly one 16-bit control word per player and reads
  no clock, no input device and no global state.
* Every random choice comes from `game_t::explseed` or `game_t::randseed`.
* The object pool is a fixed array with deterministic allocation order.

So two peers fed identical inputs produce identical worlds, and a session only
ever exchanges control words — sixteen bits per player per tick, 12.14 ticks a
second, which is under 100 bytes a second for a four-player game.

**What is missing.** Everything in `include/net.h`:

* `net_host()` / `net_join()` over UDP, with a small reliability layer
  (sequence numbers, redundant re-sends of the last few frames — losing an
  input word is unrecoverable, so send each one three times rather than
  building an ACK protocol).
* `net_exchange()`: publish this tick's word, block until every peer's word for
  `tick - NET_INPUT_DELAY` has arrived. Three ticks of delay hides about
  250 ms of latency.
* `net_state_hash()`: a checksum over the object pool and the terrain, compared
  every second or so. Divergence means a bug; the honest response is to say so
  and disconnect, not to paper over it.
* Lobby: level name and hash exchanged at join time, so peers refuse to start
  on different worlds.

**What changes in the game.** `game_t::n_players` and the per-player runway
slots exist already (`level_runway_slot()` has a `PLAY_NET` table). The pieces
that need real work are:

* `sw_end_game()` currently assumes team 1 wins; the multi-player rule from the
  original compares scores between the two sides.
* Scoring in `sw_score()` folds everything into player 0's total; it needs to
  credit the right side.
* The head-up display needs a second score.

**Non-goals.** Matchmaking, accounts, servers. Direct connection to a
host:port, or a listen socket behind whatever tunnel the players already use.
