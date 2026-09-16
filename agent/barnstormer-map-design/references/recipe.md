# The recipe vocabulary

A recipe is plain text, one instruction per line, `#` to the end of a line is
a comment. Order matters only for `land`, which is laid out left to right.

```
name    Mountain Pass
author  CRH
seed    4718

land plain   width=520 h=44
land ridge   width=600 peak=118 rough=3
land valley  width=420 floor=34
land ridge   width=520 peak=124 rough=4
land plain   width=940 h=48

field player at=420  facing=right
field enemy  at=2580 facing=left

buildings owner=player from=80   to=300  count=3  kinds=house,factory
buildings owner=enemy  from=1180 to=2200 count=17 kinds=factory,fuel,hangar

ox at=1500
ox at=1560
```

## Heading

| Line | Meaning |
|---|---|
| `name TEXT` | required; what the picker shows |
| `author TEXT` | optional |
| `seed N` | explosion scatter, and the seed for `rough=`; any number |

## `land` — the terrain, left to right

Every `land` line takes `width=`, and the widths must total **3000**. The
generator says how many you are over or under if they do not.

| Form | Takes | Shape |
|---|---|---|
| `plain` | `h=` | flat at `h`, easing from whatever came before |
| `slope` | `h=` | straight climb or fall to `h` across the width |
| `plateau` | `h=` | rises to `h` over the first third, then flat |
| `hill` | `peak=` | a rounded rise to `peak` and back down |
| `ridge` | `peak=` | as `hill`, flatter on top |
| `valley` | `floor=` | dips to `floor` in the middle and comes back |

Each landform starts from the height the one before it ended at, so the joins
are smooth by construction — you cannot accidentally make a cliff between two
sections.

`rough=N` adds a wandering N-unit wobble to any of them. 2–4 reads as
landscape; above 6 it reads as noise. It is seeded from `seed`, so the same
recipe always gives the same map.

Heights run 26–199. Keep peaks at or below **130**: above that there is no
room to turn round, and above 160 an aeroplane can barely get over it.

**The outer 140 columns at each end are overwritten with a wall** rising to
186, whatever the recipe says, so that the edge of the world looks like an
edge instead of an aeroplane stuck against nothing. Write your `land` widths
to total 3000 as usual — the walls are carved out of the ends afterwards.

## `field` — the airfields

```
field player at=420 facing=right
field enemy  at=2580 facing=left
```

One of each is required. `at=` is where the first strip goes and `facing=` is
the way the aeroplanes point (`right` or `left` — point them at each other).

The generator puts **four strips at each end** and flattens a pad under them
plus a 170-column run in the direction they face, blending back into the land
beyond. Four because the game's spawn tables are positional and index up to
eight slots; laying 0–3 at the player's end and 4–7 at the enemy's means every
mode puts friends at one end and enemies at the other.

Leave room: `at=` plus about 300 columns of your own end of the map.

## `buildings` — what there is to bomb

```
buildings owner=enemy from=1180 to=2200 count=17 kinds=factory,fuel,hangar
```

`owner=` is `player` or `enemy`, `from=`/`to=` is the band they are spread
across, `count=` how many, and `kinds=` a list cycled through: `house`,
`factory`, `fuel` (worth 200 and explodes harder), `hangar`.

They are placed in **clusters of two to four with open ground between**, not
at an even spacing: a row of evenly spaced structures reads as fence posts,
and leaves a pilot nowhere to turn round between passes. Give a group room
for that — roughly 70 columns per cluster on top of the buildings themselves,
and the generator will say how much it wants if there is not enough.

For one building exactly where you want it:

```
building owner=enemy at=1820 kind=fuel
```

A map holds **20 buildings**, and **three of them are the player's** — the
game decides that by position, and the generator arranges the file so your
`owner=player` group lands in those positions. Ask for three.

Keep everything between columns **140 and 2860**; inside that is the wall at
the end of the world.

Buildings need 24 columns each, must not sit on a strip, and must not be in
the 170 columns in front of one. The generator refuses and tells you where to
put them instead.

Put the player's own buildings *behind* their field, as the classic map does.

## `ox` — cattle

```
ox at=1500
```

At most two, and worth having: they are the only thing in the game that is
nobody's enemy. The height is worked out from the ground under them.
