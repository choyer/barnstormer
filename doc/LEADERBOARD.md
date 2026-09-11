# Local high score leaderboard

A retro arcade top ten: three initials, a score, descending, seeded with
built-in defaults. Local to the machine, no network, no accounts.

Implemented. `game/score.c`, `include/score.h`, `render_scores()` in
`render/scene.c`, the UI_ENTRY state in `main.c`, and `tests/scoretest.c`.

## Ending a run

A run used to have no end. `game.over` was set in exactly one place --
quitting -- and the fifth crash called `game_restart`, which set `score = 0`
and `gamenum = 0` and silently rebuilt the world. The README's claim that
"five crashes and the run is over" was not what the code did, and a
leaderboard has nothing to record until it is.

Three terminal states, all landing on the same screen:

| How it ends | Message | Ranked? |
|---|---|---|
| Fifth crash | `GAME OVER` | yes |
| `Esc` while parked at home and stopped | `RETIRED` | yes |
| `Esc` while airborne | `ABANDONED` | no |

Retiring is deliberate rather than automatic on touchdown, because landing at
home already refuels and rearms (`refuel()`, `move.c:149-155`) and that loop
is how a vs-computer run sustains itself. Ending the run on every landing
would destroy it. "Fly home to bank your score" is a rule players learn in
one go, and the original already special-cases quitting while at home
(`move.c:70-75`).

The change is contained: in `move_player`, the `END_LOSER` path stops calling
`game_restart` and instead sets `over` with a message, leaving the score
intact for `main.c` to read. The winner path is untouched, so level
progression still works exactly as it does now.

## Boards

Three, one per mode. A single shared board would be a vs-computer board in
practice: single-player is capped at 2,175 a level (1,800 of enemy buildings
plus the 375 perfect-clear bonus), while vs-computer is unbounded because
enemy pilots respawn unconditionally at +50 each.

## Ranking

- A score must be **above zero** to qualify. Runs can go negative -- an ox is
  -200 -- and those should not rank.
- It must **beat the tenth** entry to get in.
- **Ties rank below the incumbent**, as on a cabinet: an existing holder keeps
  the higher slot.
- The tenth entry drops off.

## Entering initials

Three cells, **A-Z and space**, blinking caret on the active cell.

| Key | Does |
|---|---|
| Up / Down | cycle the glyph |
| Left / Right | move between cells |
| A-Z, space | set this cell and advance |
| Backspace | step back a cell |
| Enter | commit and save |
| Esc | commits as well -- nobody should lose a high score to the key they habitually press to get out of things |

No timeout -- this is not eating quarters. The last initials used are
pre-filled instead of `AAA`.

This needed `SWKEY_LEFT`, `SWKEY_RIGHT` and `SWKEY_BACKSPACE` plus a character
event added to the `SWKEY_*` enum in `platform.h` and to the keysym mapping in
`wl_backend.c`; there was no left/right binding at all before. Typed
characters arrive as `SWKEY_CHAR_BASE + c` alongside whatever else the key
means, and UI_ENTRY consumes the whole queue itself so that typing `S` for
initials cannot toggle the sound.

## Screens

`UI_OVER` always shows the final score **and** the top ten, whether or not the
score ranked -- including an abandoned run, where the score is shown marked
`NOT RANKED` above the board. The forfeit rule then teaches itself the first
time someone quits mid-air.

If the score qualifies, a new `UI_ENTRY` state draws the same table with the
player's row already slotted into its rank, initials editable in place, the
other rows dimmed. Enter commits, writes the file, and returns to `UI_OVER`
with the new row highlighted. `Enter` plays again, `Esc` returns to the title,
as they do now.

`render_scores()` sits beside `render_title()` in `render/scene.c` and
reuses two things that already work there: the centred stack that shrinks
until it fits, so the table reads the same in a 700px window and on a 4K
overlay, and the translucent `PAL_HUD_BG` panel that keeps text legible over
the desktop in breakout mode. Scores are right-aligned by measuring with
`fb_text_width`.

## Storage

`$XDG_DATA_HOME/barnstormer/scores`, falling back to
`~/.local/share/barnstormer/scores`.

Nothing the project installs writes into `share/barnstormer/` --
`share/licenses/barnstormer/` and `share/doc/barnstormer/` are siblings, not
the same directory -- so a `PREFIX=$HOME/.local` install cannot collide with
it. **`$PREFIX/share/barnstormer/` is reserved**: if installed game data ever
needs a home, it goes somewhere else, or the scores move to
`$XDG_STATE_HOME`.

```
# barnstormer scores v1
LAST CRH
COMPUTER DHH 64850
COMPUTER CRH 52300
...
SINGLE CRH 12400
...
NOVICE DLC 15225
...
```

Plain text, one entry per line, uppercase ASCII, mode tag first. One file
means one atomic write and one parse for all three boards.

- **Written on commit**, not at exit, so a `kill -9` or a compositor crash a
  second later cannot lose an entry.
- **Temp file then rename**, so an interrupted write cannot truncate the
  table.
- **Re-read immediately before writing**, so a window instance and an overlay
  instance cannot clobber each other. Last writer still wins on a true race;
  acceptable for a local single-player game.
- **Version header.** Unknown lines are ignored rather than rejected, so a
  file written by a newer build degrades in an older one instead of
  exploding.
- **A file that fails to parse is renamed to `scores.bak`**, never
  overwritten. A parser bug must not silently eat someone's board.
- **Missing, short or corrupt falls back to the defaults**, padding to ten.

This would be the first thing the game ever writes -- today it is a pure
Wayland client with no persistence. A read-only home, a full disk or a
missing XDG directory must mean the game still plays, the board still shows
the defaults, and the screen says `SCORES NOT SAVED`. It must never be a
startup error and never a crash at the moment of triumph.

## Default tables

Seeded from the real ceilings rather than round numbers. Several entries are
exact multiples of a perfect level (2,175 = 1,800 + 375), so the targets mean
something: passing `ACE` on the single-player board is three clean levels.

The tenth entry sets the bar for getting on the board at all, and the three
differ on purpose -- novice welcomes a first attempt, vs-computer expects you
to have earned it.

### Novice

| # | Name | Score | |
|---|---|---|---|
| 1 | DLC | 15225 | seven perfect levels |
| 2 | CRH | 13050 | six |
| 3 | PUP | 11300 | |
| 4 | DHH | 10875 | five |
| 5 | RRH | 8700 | four |
| 6 | CAM | 7150 | |
| 7 | ACE | 6525 | three |
| 8 | SKY | 4350 | two |
| 9 | OWL | 2900 | |
| 10 | PIP | 1225 | half a level -- a first run can rank |

### Single player

| # | Name | Score | |
|---|---|---|---|
| 1 | DLC | 12400 | the original's author takes his own game |
| 2 | CRH | 10875 | five perfect levels |
| 3 | VON | 9650 | |
| 4 | RRH | 8700 | four |
| 5 | SOP | 7125 | |
| 6 | DHH | 6525 | three |
| 7 | RED | 5050 | |
| 8 | CAM | 4350 | two |
| 9 | ACE | 2975 | |
| 10 | PUP | 1650 | under one clean level |

### Against the computer

| # | Name | Score | |
|---|---|---|---|
| 1 | DHH | 64850 | |
| 2 | CRH | 52300 | |
| 3 | VON | 43775 | |
| 4 | DLC | 38200 | |
| 5 | RED | 31450 | |
| 6 | RRH | 26900 | |
| 7 | ACE | 21325 | |
| 8 | SOP | 16750 | |
| 9 | CAM | 11200 | |
| 10 | TRI | 7650 | roughly three levels plus a hundred kills |

`DLC` is David L. Clark, whose copyright the title screen already carries.
`SOP`, `CAM`, `PUP` and `TRI` are the Sopwith Camel, Pup and Triplane; `VON`
and `RED` are the obvious opposition.

## Code shape

A new `game/score.c` and `include/score.h`, called from `main.c` and
`render/scene.c` and **never from `game_tick`**. The simulation has a
deterministic replay test pinned to hash `c8aa4d46`; keeping file I/O and text
entry outside it means that test stays meaningful and unchanged.

Nothing submits or writes on: `r` (restart), `F2` (style switch), or
`--dump-frame`.

## Tests

`tests/uiflow.sh` (`make test-ui`) drives the real binary through every screen
with synthetic key events, because main.c's state machine only exists in
response to real input. It walks further than it looks like it needs to: a bug
that put the score screen straight back up after `Esc` survived testing that
pressed `Esc` once, since the game was alive either way. Only "Esc at the
title quits" separates the two, and it only gets there by walking the path.
Verified to fail on that bug before being kept.

Headless, alongside `tests/simtest.c`:

- insertion order, the tie rule, truncation to ten
- a score of zero and a negative score are both refused
- save then load round-trips exactly
- a corrupt file falls back to defaults and leaves `scores.bak`
- a short file is padded from the defaults
- an unwritable directory degrades instead of failing

The leaderboard module itself leaves the simulation alone. The run-end change
does not: ending a run on the fifth crash instead of silently rebuilding the
world moves the replay hash from `c8aa4d46` to `cdf82e1d`, and the soak tests
now report the difficulty they were started at rather than 0, because a death
no longer resets `gamenum`. That was verified to be the cause by reverting
just that hunk and watching the old hash come back. Determinism itself is
unaffected -- the test compares two runs of the same build, and they match.

## Not doing yet

Attract-mode cycling between the title screen and the board. Worth having,
but it is a title-screen feature rather than a leaderboard one.
