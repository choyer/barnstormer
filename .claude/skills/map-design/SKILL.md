---
name: map-design
description: Design and generate Barnstormer map files (.map) from a description - terrain, airfields, buildings and cattle. Use when asked to make, generate, design or fix a map, landscape or terrain for Barnstormer/Sopwith, or when editing a .map or .recipe file. Handles the file format, the placement rules the game does not document, and checking that what comes out can actually be flown.
---

# Designing a Barnstormer map

A map is a height field 3000 columns wide plus the placement of airfields,
buildings and cattle. Building one by hand is slow and the interesting
mistakes are invisible: a map can obey every rule in the format and still be
unflyable, or load perfectly and give the player the wrong three buildings.

So: **describe the map in a recipe, generate it, and check the result by
flying it.** Do not write the 3000 columns by hand, and do not hand-edit the
terrain in a `.map` file — regenerate from the recipe instead.

## The loop

```bash
# 1. write or edit a recipe (see references/recipe.md)
$EDITOR mountain-pass.recipe

# 2. generate -- prints an elevation profile and any notes
python3 .claude/skills/map-design/scripts/mapgen.py mountain-pass.recipe \
        -o ~/.local/share/barnstormer/maps/mountain-pass.map

# 3. check it is a map at all (no window; exit 1 and a line number if not)
./build/barnstormer --check ~/.local/share/barnstormer/maps/mountain-pass.map

# 4. fly it -- the only thing that catches an unflyable field
make flytest && ./build/flytest ~/.local/share/barnstormer/maps/mountain-pass.map
```

All four have to pass. `--check` says whether the file is *valid*; `flytest`
says whether it is *playable*, which is a different question and the one that
catches real mistakes. It flies the player off the deck and lets the game's
own autopilot try the other fields; every aeroplane that tries must get away.

Then hand it over: `barnstormer` and pick it from **Play Map** on the title
screen, or `barnstormer --map FILE` to go straight there.

## What makes a map good rather than merely valid

Read `references/rules.md` before generating anything — it has the placement
rules the game enforces but does not write down, and the measured limits of
what the aeroplane can do. The four that matter most:

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

## Files

- `references/recipe.md` — the recipe vocabulary, with examples
- `references/rules.md` — the format, the undocumented game rules, and the
  measured flight envelope
- `scripts/mapgen.py` — recipe to `.map`, with the profile and the notes
- `examples/mountain-pass.recipe` — a worked example that passes all four steps
