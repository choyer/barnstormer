# Barnstormer level format (version 1)

Implemented by `level_load()` and `level_save()` in `game/level.c`, against
`level_t` in `include/level.h`. `tests/leveltest.c` holds the format to this
document; `make test` runs it.

## Goals

* Human-readable, so a level can be diffed, reviewed and hand-edited.
* Small enough to send to somebody.
* Forward-compatible: an unknown key is skipped, not an error.
* Verifiable: a hash a peer can compare before starting a networked game.

## Shape

UTF-8 text, LF line endings. Blank lines and lines beginning with `#` are
ignored. Every other line is `key value...`, whitespace-separated. The
`barnstormer-level` header must come first; the rest may come in any order.

```
barnstormer-level 1
name Bridge Too Far
author carl
seed 7491
size 3000 200

# Terrain: run-length encoded height field, left to right.
# Each pair is "count:height"; the counts must sum to the level width.
# Heights are 26..199, measured up from the bottom of the world.
ground 183:199 1:198 1:197 1:196 ... 8:64
# ... continued on as many "ground" lines as needed; they concatenate.

# Runways: x, orientation (0 faces right, 1 faces left).
# The first two are the player's and the enemy's home fields; the rest are
# used only in modes with more aircraft.
runway 1270 0
runway 588 0
runway 1720 1
runway 2456 1

# Buildings: x, kind.  0 house, 1 factory, 2 fuel dump, 3 hangar.
# A fuel dump is worth 200 points and explodes harder; the rest are 100.
target 191 1
target 284 3
target 409 1

# Oxen: x, y.  Optional, at most two.
ox 1376 80
ox 1608 91
```

`name` and `author` take the rest of the line, trimmed at both ends, so a name
may contain spaces. `author` is optional and is preserved across a round trip.

## Rules the loader enforces

| Rule | Why |
|---|---|
| `size` must be exactly `3000 200` in version 1 | the simulation's constants |
| every height in 26..199 | below 26 the head-up display band shows through; above 199 is outside the world |
| ground counts sum to the width | otherwise the height field has holes |
| at least two runways, at most eight | the spawn tables index eight slots |
| the 21 columns from each runway x are within 4 of each other, and inside the world | an aircraft's footprint is 21 columns wide and it needs somewhere flat to sit |
| at most 20 buildings, at most two oxen | fixed-size arrays in `game_t` |
| buildings at least 16 columns apart, and inside the world | they are 16 wide and the game flattens the ground under them |
| an ox inside the world | it is drawn where it stands |
| `name` present, `name` and `author` at most 63 bytes | a level with no name cannot be offered in a picker |
| a fixed-arity line carries no extra words | `runway 100 0 please` is a typo, not a level |

A file that breaks a rule is rejected — `level_load()` returns -1 with `errno`
`EINVAL` and nothing allocated — and `level_error()` names the line:

```
line 12: "3000:210" is not count:height with a height in 26..199
line 6: the runway at 100 is not flat: 26..100 over its 21 columns, more than 4 apart
```

Levels are shared between strangers; a loader that limps on with a half-valid
world is worse than one that says no.

## What `level_save()` writes

The canonical form: no comments, sections in the order above, terrain runs
wrapped onto `ground` lines that never exceed 76 columns. Saving a level, loading it
back and saving it again produces byte-identical files. A level that would not
load back is refused rather than written, and the write goes through a
`.tmp` file and a rename, so an interrupted save cannot truncate a level that
was already there.

`level_free()` refuses a pointer it did not hand out, so a built-in level like
`level_classic` cannot be freed by accident.

## Hash

Not implemented yet. `level_hash()` will be FNV-1a over the canonical
serialisation — the bytes `level_save()` writes, so whitespace and comments
cannot change it. Networked peers exchange the hash at join time and refuse to
start if it differs.

## Size

Measured on the classic level, which is as detailed as levels get: its terrain
compresses to 957 runs, and the file is **6,024 bytes**. That is bigger than
an early estimate of "under 4 KB" — the terrain is hand-drawn and rarely flat
for long, so the RLE saves less than a smoother landscape would.

Gzipped it is 1,770 bytes, which is 32 lines of base64 at 76 columns — enough
to paste into a chat window, but not the two lines this document once claimed.
A level built in the editor, with more flat ground, will be smaller.

## Binary form

None. The text form is small enough that a second format would be complexity
for its own sake.
