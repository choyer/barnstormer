---
id: barnstormer-map-design
name: barnstormer-map-design
version: 1.0.0
description: Design and generate Barnstormer map files (.map) from a text recipe - terrain, airfields, buildings and cattle. Use when asked to make, generate, design or fix a map, landscape or terrain for Barnstormer/Sopwith, or when editing a .map or .recipe file. Handles the file format, the placement rules the game does not document, and checking that what comes out can actually be flown.
entrypoint: SKILL.md
manifest: manifest.json
produces: .map files, map format version 1
license: same as the repository
---

# Designing a Barnstormer map

A map is a height field 3000 columns wide plus the placement of airfields,
buildings and cattle. Building one by hand is slow and the interesting
mistakes are invisible: a map can obey every rule in the format and still be
unflyable, or load perfectly and give the player the wrong three buildings.

So: **describe the map in a recipe, generate it, and check the result by
flying it.** Do not write the 3000 columns by hand, and do not hand-edit the
terrain in a `.map` file — regenerate from the recipe instead.

This file is the whole instruction set; nothing here depends on a particular
agent, editor or harness. `manifest.json` is the same thing in machine-readable
form, for a consumer that wants to discover the tools without reading prose.

## What you need

| Thing | How it is found | If it is missing |
|---|---|---|
| `python3` ≥ 3.8 | `PATH` | nothing works; the generator is pure stdlib |
| `barnstormer` | `$BARNSTORMER`, else `./build/barnstormer`, else `PATH` | `make` in the repository root |
| `flytest` | `$FLYTEST`, else `./build/flytest`, else `PATH` | `make flytest` |

`$SKILL` below means this directory. Nothing is installed and nothing is
written outside the map you ask for, so the skill can be copied out of the
repository and pointed at a built game elsewhere:

```bash
SKILL=/path/to/agent/barnstormer-map-design
BARNSTORMER=/usr/local/bin/barnstormer
FLYTEST=~/src/barnstormer/build/flytest
```

## The loop

```bash
# 1. write or edit a recipe (see references/recipe.md)
$EDITOR mountain-pass.recipe

# 2. generate -- prints an elevation profile and any notes
python3 "$SKILL/scripts/mapgen.py" mountain-pass.recipe \
        -o ~/.local/share/barnstormer/maps/mountain-pass.map

# 3. check it is a valid map, and that it can be flown out of
"$SKILL/scripts/verify.sh" ~/.local/share/barnstormer/maps/mountain-pass.map
```

Both steps have to pass. `verify.sh` runs the two questions that matter and
returns one status: `barnstormer --check` says whether the file is *valid*,
and `flytest` says whether it is *playable*, which is a different question and
the one that catches real mistakes. It flies the player off the deck and lets
the game's own autopilot try the other fields; every aeroplane that tries must
get away.

Then hand it over: `barnstormer` and pick it from **Play Map** on the title
screen, or `barnstormer --map FILE` to go straight there.

### Driving it from a program

Both tools take `--json`, so an agent does not have to parse prose:

```bash
python3 "$SKILL/scripts/mapgen.py" r.recipe -o out.map --json
# {"ok": true, "map": "out.map", "buildings": 20, "player_buildings": 3,
#  "notes": [...], "profile": "...", "verify": "scripts/verify.sh out.map"}

"$SKILL/scripts/verify.sh" --json out.map
# {"map":"out.map","ok":true,"valid":true,"flyable":true,"check":"...","fly":"..."}
```

Exit codes are the contract: `mapgen.py` returns 0 written, 1 the recipe is
wrong (reason on stderr and in `error`), 2 usage. `verify.sh` returns 0 valid
and flyable, 1 not, 2 the binaries could not be found — which is a different
thing from a bad map and should be reported as such rather than retried.

`notes` are lint, not errors: a map with notes is still written and may still
be fine. Read them before handing the map over.

## What makes a map good rather than merely valid

Read `references/rules.md` before generating anything — it has the placement
rules the game enforces but does not write down, and the measured limits of
what the aeroplane can do. The five that matter most:

1. **Keep 170 columns clear in front of every airfield.** An aeroplane needs
   103 columns to leave the ground and 137 to climb over building height. A
   hillside or a building inside that is a wall at the end of the runway. The
   classic map leaves 170 and puts the player's own buildings *behind* the
   field. `mapgen.py` flattens a run for you and refuses buildings that
   fall inside it.
2. **The order of the buildings decides whose they are.** The game gives the
   player exactly the buildings at index 7, 8 and 9. `mapgen.py` emits them
   in the right order from `owner=`; if you write a `.map` by hand, you must
   do it yourself.
3. **Ground above 130 is a corridor, above 160 a wall.** The world is only
   200 tall and the aeroplane stalls near the top, so a peak does not just
   look tall — it takes the air away. Nothing can turn round above 130.
4. **Slope is never the limit.** The aeroplane out-climbs any gradient the
   terrain can have, so shape the land for interest, not for gentleness.
5. **Both ends of the world are walled off**, automatically, by a steep rise
   over the outer 140 columns. The world simply stops at column 0 and column
   3000 and an aeroplane that reaches either just sits there against nothing;
   a wall says so in the only language the game has. Keep fields and
   buildings between **140 and 2860** — the generator refuses anything inside
   the walls.

## Making terrain that is worth flying

Vary the scale: two or three big landforms across the map, medium undulation
inside them, and a little roughness on top (`rough=3` is plenty; `rough=8` is
a moonscape). Flat ground only where something stands on it.

Avoid one long progression from one end to the other. A map that rises
steadily west to east is read in a single glance and flown the same way every
time; break it with something that interrupts the trend — a dip behind the
high ground, a knoll on the flat, a shelf half way up — so there is more than
one way to approach the far end.

`mapgen.py` places a group of buildings in **clusters of two to four with
open ground between**, rather than at an even spacing, because a row of
evenly spaced structures reads as fence posts and gives a pilot nowhere to
turn between passes. Use several `buildings` groups when you want distinct
installations, and `building owner=... at=... kind=...` to put one exactly
where you want it.

Give the map two or three things a pilot would name — a pass to thread, a bowl
to dive into, a ridge to come over with the sun behind you. A map that is
statistically varied everywhere reads as featureless; landmarks are what make
it somewhere rather than something.

Put the fields in the open at either end and the enemy's buildings past the
high ground, so getting there is a decision rather than a straight line. Check
the elevation profile the generator prints: if you cannot tell what the map
is from it, neither can a player.

## Where to put the file

`~/.local/share/barnstormer/maps/NAME.map` (or
`$XDG_DATA_HOME/barnstormer/maps`) — anything there is offered by the map
picker. Keep the `.recipe` next to it, or in the project, so the map can be
regenerated and edited later.

Never overwrite a map you did not generate this session without asking:
somebody may have spent an evening on it in the editor.

## Handing off to the editor

The editor is for nudging, not for bulk work — the division of labour is that
this skill lays down the landscape and a person adjusts it by hand:

```bash
barnstormer --edit ~/.local/share/barnstormer/maps/mountain-pass.map
```

`g` picks up a building and carries it somewhere better, the brush raises and
lowers ground, `Tab` flies what is on the screen. Anything changed there is
lost the next time the recipe is regenerated, so once a map has been edited
by hand, edit it by hand from then on.

## Versions and compatibility

This skill is versioned separately from the game (`version:` above and in
`manifest.json`). What binds it to a release is not the game's version number
but two things it declares:

* **The map format version it writes** — 1, the format in
  `doc/MAP_FORMAT.md`. A map written for format 1 loads in any build that
  reads format 1.
* **The minimum game it needs** — 1.4.0, because that is where the header
  token became `barnstormer-map` and the data directory became `maps/`.
  Older builds wrote and read `barnstormer-level`.

So: a change to the recipe vocabulary or to the generator's judgement is a
version bump here and nothing in the game; a change to the map format is a
bump in both, and `MAP_FORMAT_VERSION` in `scripts/mapgen.py` is the line
that has to move with it.

## Files

- `manifest.json` — the same description, machine-readable
- `references/recipe.md` — the recipe vocabulary, with examples
- `references/rules.md` — the format, the undocumented game rules, and the
  measured flight envelope
- `scripts/mapgen.py` — recipe to `.map`, with the profile and the notes
- `scripts/verify.sh` — valid and flyable, in one command and one exit status
- `examples/mountain-pass.recipe` — a worked example that passes both steps
