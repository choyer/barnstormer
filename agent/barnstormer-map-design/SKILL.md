---
id: barnstormer-map-design
name: barnstormer-map-design
version: 1.6.0
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

# 4. look at the whole thing at once, recipe and all
python3 "$SKILL/scripts/mapshot.py" \
        ~/.local/share/barnstormer/maps/mountain-pass.map --format png
```

Generating and checking both have to pass; the picture is how you read what
you got. `verify.sh` runs the two questions that matter and
returns one status: `barnstormer --check` says whether the file is *valid*,
and `flytest` says whether it is *playable*, which is a different question and
the one that catches real mistakes. It flies the player off the deck and lets
the game's own autopilot try the other fields; every aeroplane that tries must
get away.

Then hand it over: `barnstormer` and pick it from **Play Map** on the title
screen, or `barnstormer --map FILE` to go straight there.

### Driving it from a program

All three tools take `--json`, so an agent does not have to parse prose:

```bash
python3 "$SKILL/scripts/mapgen.py" r.recipe -o out.map --json
# {"ok": true, "map": "out.map", "buildings": 20, "player_buildings": 3,
#  "notes": [...], "profile": "...", "verify": "scripts/verify.sh out.map"}

"$SKILL/scripts/verify.sh" --json out.map
# {"map":"out.map","ok":true,"valid":true,"flyable":true,"check":"...","fly":"..."}

python3 "$SKILL/scripts/mapshot.py" --json out.map --format png
# {"map":"out.map","ok":true,"hash":"3f012f15","image":"out.png",
#  "layout":"full","scale":2,"bytes":241874,"recipe":"out.recipe", ...}
```

Exit codes are the contract: `mapgen.py` returns 0 written, 1 the recipe is
wrong (reason on stderr and in `error`), 2 usage. `verify.sh` returns 0 valid
and flyable, 1 not, 2 the binaries could not be found — which is a different
thing from a bad map and should be reported as such rather than retried.
`mapshot.py` returns 0 drawn, 1 the map is not usable, 2 usage, 3 png was
asked for and there is no rasteriser installed.

`notes` are lint, not errors: a map with notes is still written and may still
be fine. Read them before handing the map over.

## Looking at the whole map

`verify.sh` says whether a map works; `mapshot.py` says what it *is*. It
draws all 3000 columns at once the way the game draws them: the game's
palette, two world rows of turf over earth, both fields with the 170 columns
each needs in front of it, the walls at the ends of the world, the 130/160
flight limits, and every building and ox as **the game's own 16x16 sprite**
— `SPRITE_TARGET` frames 0-3 and `SPRITE_OX`, in the owner's colours, so a
fuel dump looks like a fuel dump and not like a box with a label:

```bash
python3 "$SKILL/scripts/mapshot.py" mountain-pass.map                  # -> .svg
python3 "$SKILL/scripts/mapshot.py" mountain-pass.map --format png     # -> .png
python3 "$SKILL/scripts/mapshot.py" mountain-pass.map --layout map     # world only
python3 "$SKILL/scripts/mapshot.py" *.map --layout map --scale 1 \
        --out-dir thumbs --format png                                  # thumbnails
```

Two layouts, because there are two jobs:

| `--layout` | What it draws | For |
|---|---|---|
| `full` (default) | the world, a ruler in recipe coordinates, and a panel with the name, the hash, the counts and the recipe itself | review, or handing a map to somebody who may want to change it |
| `map` | the world and nothing else | thumbnails, and illustrations something else captions |

`full` takes the recipe from `NAME.recipe` beside the map unless `--recipe`
names one; `--no-recipe` draws the panel with the map's details alone rather
than inventing a recipe it does not have.

Two details are the game's rather than the file's, and both matter when
reading a preview. Buildings stand on the **levelled pad** `init_targets()`
cuts under them (`game_pad_height()`), not on the raw height field, so what
you see is the ground the player will fly over. And the ox is drawn with
`clr` 1, which means it wears the player's colours in the game too — a cyan
ox is not a mis-render.

Read it the way you read the elevation profile, but for the things the
profile cannot show: whether the player's three buildings really are behind
their own field, whether a group reads as clusters or as fence posts, and
whether the shape has landmarks or is varied everywhere and memorable
nowhere.

### Serving previews

It is built to be run by a request handler as well as by a person, which is
why it is a separate script from `mapgen.py` and why it takes a `.map`
rather than a recipe — a server has the map, not the thing that made it:

* SVG output is pure stdlib and needs nothing installed; `--format png`
  shells out to `rsvg-convert` or `magick` and exits **3** if neither is
  there, which is a missing dependency rather than a bad map and should be
  reported as such.
* Output is deterministic — no timestamps, no randomness — so a preview can
  be cached on `(hash, layout, scale, mapshot version)`. `--json` reports
  all four. The hash is computed from the canonical form in
  `../../doc/MAP_FORMAT.md`, so a preview is labelled with the same value
  the game prints without running the game.
* Map files come from strangers: the parser enforces the format's rules,
  caps the input at `--max-bytes` (1 MiB by default, against a real map's
  6 KB), holds names to 63 bytes, drops control characters and XML-escapes
  everything that reaches the drawing.
* `-o -` writes the image to stdout, and `--scale 1` is the cheapest useful
  size: a 3000x200 thumbnail, about 26 KB of PNG.

## What makes a map good rather than merely valid

Read `references/rules.md` before generating anything — it has the placement
rules the game enforces but does not write down, and the measured limits of
what the aeroplane can do. The eight that matter most:

1. **Every airfield has a hangar and a fuel dump behind its home strip, and
   the player's has a tank in front of it.** Not suggestions: `mapgen.py`
   places all of them, at the classic map's own offsets — hangar 30 columns
   behind the strip, fuel dump 60, both owned by the side whose field it is,
   and the player's tank 170 columns beyond the last strip on the enemy's
   side, a defensive unit covering the approach. A strip on its own is not
   an airfield. Five buildings are therefore already placed, all three of
   the player's among them, so a recipe writes no `owner=player` line and
   asks only for the enemy's.
2. **Keep 170 columns clear in front of every airfield.** An aeroplane needs
   103 columns to leave the ground and 137 to climb over building height. A
   hillside or a building inside that is a wall at the end of the runway. The
   classic map leaves 170 and puts the player's own buildings *behind* the
   field. `mapgen.py` flattens a run for you and refuses buildings that
   fall inside it.
3. **The order of the buildings decides whose they are.** The game gives the
   player exactly the buildings at index 7, 8 and 9. `mapgen.py` emits them
   in the right order from `owner=`; if you write a `.map` by hand, you must
   do it yourself.
4. **Ground above 130 is a corridor, above 160 a wall.** The world is only
   200 tall and the aeroplane stalls near the top, so a peak does not just
   look tall — it takes the air away. Nothing can turn round above 130.
5. **Slope is never the limit.** The aeroplane out-climbs any gradient the
   terrain can have, so shape the land for interest, not for gentleness.
6. **Both ends of the world are walled off**, automatically, by a steep rise
   over the outer 140 columns. The world simply stops at column 0 and column
   3000 and an aeroplane that reaches either just sits there against nothing;
   a wall says so in the only language the game has. Keep fields and
   buildings between **140 and 2860** — the generator refuses anything inside
   the walls.
7. **Cattle need daylight around them.** An ox costs 200 points to whoever
   kills it and dies to any bullet or bomb, so one standing against a
   building makes that building unattackable rather than interesting.
   `mapgen.py` moves an ox to the nearest column with **40 clear** of every
   building and off the strips, tries to keep the two **200 apart**, and
   reports every move as a note. Read those notes: an ox that had to travel
   300 columns is telling you the buildings are packed too tightly.
8. **A map is terrain first, buildings second.** The count is a range, not a
   target: **10 at the floor, 20 at the ceiling**, and most maps want 12–15.
   Ten is arithmetic — the player's three have to land on index 7, 8 and 9,
   so seven enemy buildings must come first — and twenty is the array in
   `game_t`. What makes a map worth flying is a pass to thread or a bowl to
   dive into, not the fourteenth factory. Buildings go down as singles and
   the odd pair with **120 columns of open ground** between groups; the
   classic map's median gap is **111**, and `mapgen.py` reports yours next
   to it and complains under half of it. Fewer buildings, further apart,
   over more interesting ground.

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
- `scripts/mapshot.py` — the whole world as one picture, with or without the
  recipe under it; also computes the map's hash without the game
- `examples/mountain-pass.recipe` — a worked example that passes both steps
