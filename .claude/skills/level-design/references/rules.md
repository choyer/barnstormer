# What the game requires, and what the aeroplane can do

Two kinds of rule. The first kind the loader enforces, so breaking one is an
error you will see immediately. The second kind nothing enforces: the level
loads, and it is wrong anyway.

## Rules the loader enforces

From `doc/LEVEL_FORMAT.md`, refused with the line that broke them:

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

These live in `game/game.c`, not in the format. A level that breaks them loads
and plays and is quietly wrong.

**The order of the buildings decides whose they are.** The game gives the
player the buildings at index **7, 8 and 9** in the file and the enemy every
other one. Sort your buildings by position and the player gets whichever three
happen to land in the middle of the map. Emit them as: seven enemy, then the
player's three, then the rest of the enemy's.

**Runway slots are positional.** Slot 0 is the player. Slot 7 is the enemy.
Against the computer, slots 1 and 6 are used as well; in networked play, 3 and
4. With fewer than eight runways the game wraps (`slot % n_runways`), which
puts enemy aircraft on your own strip — so give a level eight, four at each
end, and the arrangement works in every mode.

**Fuel is derived from the map width** (`MAXFUEL = 3 × MAX_X`), so the
distance between the fields is a balance decision as much as a layout one.

**A building stands on a pad the game levels under it**, so terrain under
buildings is cosmetic. Terrain under a runway is not.

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

## Checking a level

```bash
./build/barnstormer --check LEVEL      # is it a level?      (no window)
./build/flytest LEVEL                  # can it be flown?    (no window)
```

`flytest` starts a real game against the computer, flies the player off the
deck and lets the game's own autopilot try the other fields. Every aeroplane
that tries to leave must get away: 30 above the ground and 200 columns out.
Aircraft that never move are the reserves waiting their turn, and are not
counted against the level.

It is calibrated against the classic map, which passes. If your level does not,
the level is wrong.
