---
name: level-design
description: Design and generate Barnstormer level files (.lvl) from a description - terrain, airfields, buildings and cattle. Use when asked to make, generate, design or fix a level, map or landscape for Barnstormer/Sopwith, or when editing a .lvl or .recipe file. Handles the file format, the placement rules the game does not document, and checking that what comes out can actually be flown.
---

# Designing a Barnstormer level

A level is a height field 3000 columns wide plus the placement of airfields,
buildings and cattle. Building one by hand is slow and the interesting
mistakes are invisible: a level can obey every rule in the format and still be
unflyable, or load perfectly and give the player the wrong three buildings.

So: **describe the level in a recipe, generate it, and check the result by
flying it.** Do not write the 3000 columns by hand, and do not hand-edit the
terrain in a `.lvl` file — regenerate from the recipe instead.

## The loop

```bash
# 1. write or edit a recipe (see references/recipe.md)
$EDITOR mountain-pass.recipe

# 2. generate -- prints an elevation profile and any notes
python3 .claude/skills/level-design/scripts/levelgen.py mountain-pass.recipe \
        -o ~/.local/share/barnstormer/levels/mountain-pass.lvl

# 3. check it is a level at all (no window; exit 1 and a line number if not)
./build/barnstormer --check ~/.local/share/barnstormer/levels/mountain-pass.lvl

# 4. fly it -- the only thing that catches an unflyable field
make flytest && ./build/flytest ~/.local/share/barnstormer/levels/mountain-pass.lvl
```

All four have to pass. `--check` says whether the file is *valid*; `flytest`
says whether it is *playable*, which is a different question and the one that
catches real mistakes. It flies the player off the deck and lets the game's
own autopilot try the other fields; every aeroplane that tries must get away.

Then hand it over: `barnstormer` and pick it from **LEVEL** on the title
screen, or `barnstormer --level FILE` to go straight there.

## What makes a level good rather than merely valid

Read `references/rules.md` before generating anything — it has the placement
rules the game enforces but does not write down, and the measured limits of
what the aeroplane can do. The four that matter most:

1. **Keep 170 columns clear in front of every airfield.** An aeroplane needs
   103 columns to leave the ground and 137 to climb over building height. A
   hillside or a building inside that is a wall at the end of the runway. The
   classic map leaves 170 and puts the player's own buildings *behind* the
   field. `levelgen.py` flattens a run for you and refuses buildings that
   fall inside it.
2. **The order of the buildings decides whose they are.** The game gives the
   player exactly the buildings at index 7, 8 and 9. `levelgen.py` emits them
   in the right order from `owner=`; if you write a `.lvl` by hand, you must
   do it yourself.
3. **Ground above 130 is a corridor, above 160 a wall.** The world is only
   200 tall and the aeroplane stalls near the top, so a peak does not just
   look tall — it takes the air away. Nothing can turn round above 130.
4. **Slope is never the limit.** The aeroplane out-climbs any gradient the
   terrain can have, so shape the land for interest, not for gentleness.

## Making terrain that is worth flying

Vary the scale: two or three big landforms across the map, medium undulation
inside them, and a little roughness on top (`rough=3` is plenty; `rough=8` is
a moonscape). Flat ground only where something stands on it.

Give the map two or three things a pilot would name — a pass to thread, a bowl
to dive into, a ridge to come over with the sun behind you. A level that is
statistically varied everywhere reads as featureless; landmarks are what make
it somewhere rather than something.

Put the fields in the open at either end and the enemy's buildings past the
high ground, so getting there is a decision rather than a straight line. Check
the elevation profile the generator prints: if you cannot tell what the level
is from it, neither can a player.

## Where to put the file

`~/.local/share/barnstormer/levels/NAME.lvl` (or
`$XDG_DATA_HOME/barnstormer/levels`) — anything there is offered by the level
picker. Keep the `.recipe` next to it, or in the project, so the level can be
regenerated and edited later.

Never overwrite a level you did not generate this session without asking:
somebody may have spent an evening on it in the editor.

## Handing off to the editor

The editor is for nudging, not for bulk work — the division of labour is that
this skill lays down the landscape and a person adjusts it by hand:

```bash
barnstormer --edit ~/.local/share/barnstormer/levels/mountain-pass.lvl
```

`g` picks up a building and carries it somewhere better, the brush raises and
lowers ground, `Tab` flies what is on the screen. Anything changed there is
lost the next time the recipe is regenerated, so once a level has been edited
by hand, edit it by hand from then on.

## Files

- `references/recipe.md` — the recipe vocabulary, with examples
- `references/rules.md` — the format, the undocumented game rules, and the
  measured flight envelope
- `scripts/levelgen.py` — recipe to `.lvl`, with the profile and the notes
- `examples/mountain-pass.recipe` — a worked example that passes all four steps
