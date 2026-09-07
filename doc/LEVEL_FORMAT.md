# Barnstormer level format (planned, version 1)

Not implemented yet — `level_load()` and `level_save()` currently fail with
`ENOSYS`. This is the specification they will implement, written down now so
that `level_t` in `include/level.h` and the editor described in
[ROADMAP.md](ROADMAP.md) are designed against the same thing.

## Goals

* Human-readable, so a level can be diffed, reviewed and hand-edited.
* Small enough to paste into a chat window after gzip and base64.
* Forward-compatible: an unknown key is skipped, not an error.
* Verifiable: a hash a peer can compare before starting a networked game.

## Shape

UTF-8 text, LF line endings. Blank lines and lines beginning with `#` are
ignored. Every other line is `key value...`, whitespace-separated.

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

## Rules a loader must enforce

| Rule | Why |
|---|---|
| `size` must be exactly `3000 200` in version 1 | the simulation's constants |
| every height in 26..199 | below 26 the head-up display band shows through; above 199 is outside the world |
| ground counts sum to the width | otherwise the height field has holes |
| at least two runways, at most eight | the spawn tables index eight slots |
| 20 columns from each runway x are within 4 of each other | an aircraft needs somewhere flat to sit |
| at most 20 buildings, at most two oxen | fixed-size arrays in `game_t` |
| buildings at least 16 columns apart | they are 16 wide and the game flattens the ground under them |

A file that breaks a rule is rejected with a message naming the line. Levels
are shared between strangers; a loader that limps on with a half-valid world is
worse than one that says no.

## Hash

`level_hash()` will be FNV-1a over the canonical serialisation — the file as
`level_save()` would write it, so whitespace and comments cannot change it.
Networked peers exchange the hash at join time and refuse to start if it
differs.

## Binary form

None. The text form gzips to well under a kilobyte for a typical level, which
is small enough that a second format would be complexity for its own sake.
