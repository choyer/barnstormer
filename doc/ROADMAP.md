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

## 2. Map builder

**Goal.** Design your own landscape and building layout, save it, load it,
share it. The reference for the feel is Excitebike's track editor: direct,
immediate, and playable from inside the editor.

**What already supports it.** `map_t` (`include/map.h`) is the only thing
`game_start()` takes: a height field plus arrays of runways, buildings and
oxen. `data/map_classic.c` is one of these and is not special-cased anywhere.
The file format is specified in [MAP_FORMAT.md](MAP_FORMAT.md) and
implemented: `map_load()`, `map_save()` and `map_free()` in
`game/map.c`, with `map_error()` for the message to show whoever is editing
the file. The classic map round-trips through disk to a byte-identical file
and an identical 3000-tick replay hash (`tests/maptest.c`). `barnstormer
--map FILE` flies one; such a run is deliberately not ranked, since the
boards are scores made on the classic map.

**The editor.** `barnstormer --edit FILE` opens a map, or starts one if the
file is not there yet: a terrain brush (raise, lower, smooth, flatten), the
four building types, runways and oxen, the map's name and author as typed
text, `g` to pick up something already placed and carry it somewhere else, and
`Tab` to fly what you are looking at and `Tab` again to come back. The model
is in `game/editor.c`, headless and tested (`tests/edittest.c`); the drawing
is `render_edit()`.

Its one rule is that the map under construction is always a map: every
change is checked against `map_check()` -- the loader's own validation -- and
undone if it would break one, with the reason in the status line. So the test
flight is always available, and saving cannot produce a file the game would
refuse.

**Choosing one.** `$XDG_DATA_HOME/barnstormer/maps` holds them, and the
title screen's fourth row opens a picker over it: `map_list()` offers only
the files that actually load, sorted by name, and says how many would not
rather than hiding them.

Carrying obeys the same rule as everything else. What is picked up stays part
of the map while it is carried, so it is checked every step: carry it
somewhere it cannot go and it stays where it was while the cursor carries on,
and it catches up when the way is clear. It keeps its index, too, which
erasing and re-placing would not -- a building's index decides whose side it
is on and a runway's decides who spawns there.

**Done.** The builder is finished as scoped here. What would extend it, if
anyone wants it: more building types (the four are the original's), maps of
a size other than 3000x200 (see the note in MAP_FORMAT.md about what that
would take), and undo.

**Sharing.** A map is one small text file — 6 KB for the classic map, the
most detailed there is — so the sharing story is "send the file". A base64 form
is still worth having for pasting into a chat window, though it is bigger than
this document once guessed: gzipped and base64'd, the classic map is 32 lines
at 76 columns rather than two. `map_hash()` is written: FNV-1a over the
canonical serialisation, printed when the editor saves and when `--map`
loads, so two people can check they hold the same map. Netplay compares it
at join time.

**Ordering.** This should land before multiplayer: a shared map format is a
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
* Lobby: map name and hash exchanged at join time, so peers refuse to start
  on different worlds.

**What changes in the game.** `game_t::n_players` and the per-player runway
slots exist already (`map_runway_slot()` has a `PLAY_NET` table). The pieces
that need real work are:

* `sw_end_game()` currently assumes team 1 wins; the multi-player rule from the
  original compares scores between the two sides.
* Scoring in `sw_score()` folds everything into player 0's total; it needs to
  credit the right side.
* The head-up display needs a second score.

**Non-goals.** Matchmaking, accounts, servers. Direct connection to a
host:port, or a listen socket behind whatever tunnel the players already use.
