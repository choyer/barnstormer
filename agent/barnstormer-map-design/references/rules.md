# What the game requires, and what the aeroplane can do

Two kinds of rule. The first kind the loader enforces, so breaking one is an
error you will see immediately. The second kind nothing enforces: the map
loads, and it is wrong anyway.

## Rules the loader enforces

From `doc/MAP_FORMAT.md`, refused with the line that broke them:

| Rule | Why |
|---|---|
| `size` exactly `3000 200` | the simulation's constants |
| every height 26–199 | below 26 the instrument band shows through |
| terrain run-lengths sum to 3000 | otherwise the height field has holes |
| 2–8 runways | the spawn tables index eight slots |
| a runway's 21 columns level within 4 | an aeroplane has to sit on it |
| at most 20 buildings, 2 oxen | fixed arrays in the game |
| buildings 16 columns apart | they are 16 wide |
| no building across a runway's 21 columns | aircraft spawn inside it |
| a name, 63 bytes or less | the picker has to show something |

`barnstormer --check FILE` applies all of them without opening a window.

## Rules nothing enforces

These live in `game/game.c`, not in the format. A map that breaks them loads
and plays and is quietly wrong.

**The order of the buildings decides whose they are.** The game gives the
player the buildings at index **7, 8 and 9** in the file and the enemy every
other one. Sort your buildings by position and the player gets whichever three
happen to land in the middle of the map. Emit them as: seven enemy, then the
player's three, then the rest of the enemy's.

**Runway slots are positional.** Slot 0 is the player. Slot 7 is the enemy.
Against the computer, slots 1 and 6 are used as well; in networked play, 3 and
4. With fewer than eight runways the game wraps (`slot % n_runways`), which
puts enemy aircraft on your own strip — so give a map eight, four at each
end, and the arrangement works in every mode.

**Strips come in twos and fours, and the classic map scatters them.** Its
four per side are not in one place: player 1270 (slot 0), 1330, 1360 and
**588**; enemy 1720 (slot 7), 1660, 1630 and **2456**. The outlier takes
slot 1 or 6 — the second slot the computer mode uses — so the second
aeroplane of each side spawns 700 columns away from its own field. That is
`spread=dispersed` in a recipe, and it is what makes an attack able to
arrive from two directions.

**A reserve strip does not get the run a home strip gets.** Measured on the
classic map, the clear ground ahead of each of its eight strips before a
building or rising terrain:

| Slot | x | Faces | Clear run | Cut short by |
|---|---|---|---|---|
| 0 (player home) | 1270 | right | **170** | the tank at 1440 |
| 7 (enemy home) | 1720 | left | **155** | the tank at 1550 |
| 2 | 1330 | right | 110 | the tank at 1440 |
| 5 | 1660 | left | 95 | the tank at 1550 |
| 1 | 588 | right | 87 | ground rising from 26 to 43 |
| 3 | 1360 | right | 80 | the tank at 1440 |
| 4 | 1630 | left | 65 | the tank at 1550 |
| 6 | 2456 | left | 51 | the tank at 2390 |

So the home strips get 155–170 and the reserves 51–110, and the map is
flyable: `flytest` gets every aeroplane away. This skill keeps **170 for a
home strip and 80 for a reserve**, which is what makes four strips a side
affordable — four full corridors would flatten a third of the world for
aeroplanes that are parked.

**An airfield is a strip plus a hangar and a fuel dump — and the player's
also has a tank.** That is what the classic map has, and it is the layout
this skill reproduces on every map. Offsets are measured from the home
strip's `x`:

| | classic player field | classic enemy field | rule |
|---|---|---|---|
| home strip | slot 0 at 1270, facing right | slot 7 at 1720, facing left | the recipe's `at=` |
| hangar (kind 0) | 1240 | 1750 | 30 columns behind the strip |
| fuel dump (kind 2) | 1210 | 1780 | 60 columns behind the strip |
| tank (kind 3) | 1440 | — | the player's only: 170 columns beyond the last strip, facing the enemy |

Both airfield buildings belong to the side whose field it is, and the
player's tank is the third building it owns, which is why a recipe places
none of the player's three. The tank sits between the two fields on
purpose: it is a defensive unit, so it is what an attack run meets first.
The classic map's third player building is at 1440 — exactly 170 columns
past its home strip at 1270, past the take-off run rather than inside it.

**Fuel is derived from the map width** (`MAXFUEL = 3 × MAX_X`), so the
distance between the fields is a balance decision as much as a layout one.

**A building stands on a pad the game levels under it**, so terrain under
buildings is cosmetic. Terrain under a runway is not.

**An ox is a liability, not a decoration.** Killing one costs the killer
**200 points** (`collision.c`, `score_penalty(g, ttype, agent, 200)`), and
it dies to anything that touches it except the blast: the OBJ_OX case
returns early only for `OBJ_EXPLOSION` and `OBJ_STARBURST`, so bullets,
bombs and aircraft all kill it. Two consequences for layout:

* **Keep 40 columns between an ox and any building.** Otherwise the
  building cannot be attacked without risking the fine, which makes it a
  trap rather than a target. The classic map leaves 48 and 42.
* **Keep the two oxen apart** — 200 columns or more; the classic map has
  232. Side by side they are one hazard worth 400, and a single wide bomb
  run can take both.
* **Keep them off the strips and out of the take-off runs.** An aeroplane
  that hits an ox is wounded *and* fined, which is a rough way to start a
  sortie. (The classic map does put one at the end of a reserve strip at
  1360 — a quirk worth not copying.)

## What the aeroplane can do

Measured with `make probe` (`tests/flightprobe.c`), single player, game 0. Run
it again if the flight model ever changes, because everything below is only
true of the model as it stands.

### Take-off

| | columns |
|---|---|
| leaves the ground after | 103 |
| clears building height (16) after | 137 |
| reaches 50 above the field after | 221 |

The classic map leaves **170 columns clear** in front of each field. Use that
as the working number: it is the original's own margin and 33 more than the
aeroplane strictly needs.

### Climbing

| pitch | gradient |
|---|---|
| 22.5° | 1 in 2.4 |
| 45° | 1 in 1.1 |
| 67.5° | 1 in 0.6 |
| 90° | 1 in 0.3 |

**Slope is never the limit.** Every gradient from 1 in 6 to 1 in 1 has been
flown up successfully; the aeroplane out-climbs anything terrain can do. Shape
the land for interest, not for gentleness.

### Turning round

A loop is about **32–38 columns wide and 36–44 high**. That is the room an
aeroplane needs to reverse direction, since there is no other way to do it.

### Height is the real constraint

The world is 200 tall and the aeroplane stalls near the top, so ground height
takes air away:

| ground | air above | what it can do there |
|---|---|---|
| up to 130 | 70+ | climbs, turns, fights |
| 140–160 | 40–60 | can transit, cannot turn round |
| 170+ | under 30 | barely gets over it |

So a 180-high mountain is not scenery, it is a wall; a 150-high plateau is a
corridor you can fly along but not manoeuvre over. Peaks of 110–130 give you
dramatic terrain that is still a place to fight.

## The edges of the world

The world is 3000 columns and stops dead at both ends: an aeroplane that
reaches column 0 or column 3000 stays there, against nothing, with no
indication of why. Every map should therefore wall both ends off with a
steep rise — `mapgen.py` does it automatically over the outer 140 columns,
up to 186, which is high enough to read as the end of the world rather than
as a hill worth crossing.

## Checking a map

```bash
scripts/verify.sh MAP                  # both questions, one exit status

# or the two tools separately, which is what verify.sh runs:
barnstormer --check MAP                # is it a map?        (no window)
flytest MAP                            # can it be flown?    (no window)
```

`verify.sh` finds the binaries through `$BARNSTORMER` and `$FLYTEST`, then the
repository's `build/`, then `PATH`, and exits 2 rather than 1 when it cannot
find them -- a missing tool is not a bad map.

`flytest` starts a real game against the computer, flies the player off the
deck and lets the game's own autopilot try the other fields. Every aeroplane
that tries to leave must get away: 30 above the ground and 200 columns out.
Aircraft that never move are the reserves waiting their turn, and are not
counted against the map.

It is calibrated against the classic map, which passes. If your map does not,
the map is wrong.
