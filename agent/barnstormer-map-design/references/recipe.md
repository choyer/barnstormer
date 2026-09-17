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

buildings owner=enemy  from=980  to=2300 count=9  kinds=factory,fuel,tank

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
field player at=420  facing=right
field enemy  at=2580 facing=left

field player at=1020 facing=right spread=dispersed
field enemy  at=1980 facing=left  spread=dispersed
```

One of each is required. `at=` is where the **home strip** goes and `facing=`
is the way the aeroplanes point (`right` or `left` — point them at each
other).

Each side gets **four strips**, because the game's spawn tables are
positional and index eight slots; laying 0–3 at the player's end and 4–7 at
the enemy's means every mode puts friends at one end and enemies at the
other.

### Which side, and where

**Neither side is tied to an end.** The player may be on the left, on the
right, or in the middle; the only rules are that the two fields **point at
each other** and stand at least **450 columns apart** (the classic map's own
figure). Both are checked, with the arithmetic in the message.

```
field player at=420  facing=right      # the usual: player west, enemy east
field enemy  at=2580 facing=left

field player at=2560 facing=left       # mirrored: player east
field enemy  at=420  facing=right

field player at=1270 facing=right      # what the classic map does: both
field enemy  at=1720 facing=left       # fields mid-map, 450 apart, and the
                                       # buildings spread to either side
```

The classic arrangement is worth trying: with both fields inland the works
run off both ways from the middle, the flight home is short, and the map
does not read as two ends with a journey between them. Mind the arithmetic
that follows from `at=`, though — the hangar and fuel dump go behind the
home strip, the player's tank in front of it, and with `spread=dispersed`
one strip 700 columns behind — so an inland field needs room on both sides.

### Which way they take off

`facing=` is set per field, so the two are independent. All of these work:

| Configuration | Recipe | Plays as |
|---|---|---|
| **towards each other** (default) | player `facing=right`, enemy `facing=left`, player west | take off and you are pointed at the war |
| **away from each other** | player at 900 `facing=left`, enemy at 2100 `facing=right` | everybody takes off outwards, climbs, loops and comes back over their own field; the fight is in the middle and the first minute is a join-up |
| **one way** | both `facing=right` | one side takes off into the fight, the other has to turn round first — lopsided on purpose |

A field that takes off **away** from the other needs room out there: at
least **320 columns** between its outermost strip and the wall (221 to reach
50 above the field, a loop about 38 wide, and somewhere to put the nose
down), and that ground wants to be open. Both are checked — the room is
refused outright, and rising ground in the climb-out is a note:

```
note: runway 4 at 1090 takes off away from the other field, and the ground
      out that way rises 43 above it within 320 columns; that is where the
      aeroplane has to climb and loop round, so keep it open
```

So a back-to-back map wants its fields inland, its outer thirds gentle, and
its terrain and buildings in the middle where both sides converge. The
player's tank follows the rule rather than the facing: it always stands
between its own field and the enemy's, which for an outward-facing field
means behind the strips instead of in front of them.

### Every strip is an airfield

A satellite strip gets **its own hangar**, 30 columns behind it, placed for
you — a strip in an empty field is a parked aeroplane, not an airfield. The
classic map does the same: a building 49 columns behind its outlier at 588
and 66 behind the one at 2456. Those hangars belong to the **enemy** even
when the strip is the player's, because the player owns exactly three
buildings and its own field carries all of them — which is how the classic
map has it too.

So the buildings placed for you are: each field's hangar and fuel dump, the
player's tank, and one hangar per satellite strip. With `spread=dispersed`
on both sides that is **seven**, and a recipe asks for the rest.

### `spread=` — where those four go

| `spread=` | Layout | Reads as |
|---|---|---|
| `line` (default) | the four in a row from `at`, 50 columns apart | one airfield; every aeroplane starts in the same place |
| `dispersed` | the home strip, two more 60 and 90 columns towards the enemy, and a fourth **700 columns behind the field** | two airfields a side, as the classic map has it |

`dispersed` is what the classic map does — player strips at 1270, 1330, 1360
and 588; enemy at 1720, 1660, 1630 and 2456 — and it is worth having. The
outlier takes **slot 1** (player) or **slot 6** (enemy), which is the second
slot the computer mode uses, so the second aeroplane of each side takes off
from the far strip: attacks arrive from two directions and there is no single
place to watch. It also needs room behind the field, so keep a dispersed
field at least 900 columns from its own end of the world.

### What each strip keeps in front of it

A strip is flattened with a run in the direction it faces, blending back
into the land beyond, and **the home strip gets more run than the reserves**:

| | Clear run | Why |
|---|---|---|
| home strip (slot 0 / 7) | **170** | where the sortie that matters starts; 137 is what an aeroplane needs to clear building height |
| reserve strips | **80** | measured on the classic map, whose six reserves have 51, 65, 80, 87, 95 and 110 — every one cut short by a tank or rising ground, and it flies |

That is what makes four strips affordable: four full corridors a side would
flatten a third of the world for aeroplanes that are parked. Strips that
stand together share one pad; a dispersed outlier gets its own, so the land
between them is left alone.

Every field also comes with **its own hangar and fuel dump**, placed for you
behind the home strip exactly as the classic map places them: the hangar 30
columns back and the fuel dump 60, on the field's own flat, owned by the side
whose field it is. An airfield in this game is not a bare strip, and getting
that wrong is not something a recipe should be able to do. The flat is
extended backwards to carry them.

The player's field also comes with **its tank**, placed 170 columns beyond
the last strip on the enemy's side of the field — a defensive unit covering
the approach, which is what the player's third building is for. The classic
map does the same: field at 1270 facing right, third player building at
1440.

So **all three of the player's buildings are placed for you**, and two of
the enemy's. A recipe asks only for the enemy's remainder — `count=5` to
`count=15` depending on how busy the map should be (see *How many* below).
An `owner=player` line is refused: there is nothing left for it to place.

Leave room: `at=` plus about 300 columns of your own end of the map, and at
least 76 columns behind the home strip for the hangar and the fuel dump —
which means a right-facing field at 216 or more, and a left-facing one at
2784 or less.

## `buildings` — what there is to bomb

```
buildings owner=enemy from=980 to=2300 count=9 kinds=factory,fuel,tank
```

`owner=` is `player` or `enemy`, `from=`/`to=` is the band they are spread
across, `count=` how many, and `kinds=` a list cycled through:

| Word | Kind | What it looks like |
|---|---|---|
| `hangar` | 0 | flagged shed with an open front — the one both airfields get |
| `factory` | 1 | windowed block with twin chimneys |
| `fuel` | 2 | drum on legs; worth 200 instead of 100 and explodes harder |
| `tank` | 3 | turret and a gun over tracks |

A word that is not one of those four is refused, rather than quietly
skipped.

They are placed as **singles and the occasional pair, with open ground
between** — which is what the classic map does: twenty buildings from 191 to
2763, a median gap of **111 columns**, and no gap under 69 except its two
airfield pairs. A row of closely spaced structures reads as fence posts and
leaves a pilot nowhere to turn round between passes. The generator wants
**120 columns** of open ground per group and says how much it needs if the
band is too narrow.

For one building exactly where you want it:

```
building owner=enemy at=1820 kind=fuel
```

### How many

**Three are always the player's** — the game decides that by position, and
the generator arranges the file so the player's land on index 7, 8 and 9.
All three come with its airfield: hangar, fuel dump, tank. A recipe writes
no `owner=player` line at all.

For the enemy the range is real, and most maps should not be near the top of
it:

| Buildings | Enemy `count=` | Reads as |
|---|---|---|
| 10 (the floor) | 5 | a landscape with something in it |
| 12–15 | 7–10 | the usual: a few installations worth a sortie each |
| 20 (the ceiling) | 15 | the classic map, and only if the terrain earns it |

Ten is arithmetic, not taste: the player's three have to land on index 7, 8
and 9, so there must be at least seven enemy buildings ahead of them or the
game hands some of the player's own to the enemy. Twenty is the fixed array
in `game_t`. Anything outside that is refused with the arithmetic spelled
out.

**And put them where the aeroplanes go.** A long stretch of world with
nothing on it is scenery: there is no reason to fly there and nothing
happens when you do. The classic map's emptiest run is **260 columns**, and
it has something within 100 columns of every one of its strips. Past **500
columns** of nothing the generator says so:

```
note: nothing stands between column 306 and 960 -- 654 columns of the
      world, and the classic map's emptiest run is 260. Put a group of
      buildings out there, or move a field into it: a stretch with nothing
      to fly to is scenery
```

It is a guideline, not a rule — a map whose point is emptiness may take the
note and keep it — but the usual answer is a second `buildings` group in
that stretch. It is what makes the far side of a satellite strip worth
visiting, and it is why the classic map's buildings run from 191 to 2763
rather than clustering between the fields.

**Terrain is what makes a map.** A pilot remembers a pass, a bowl or a ridge
they came over with the sun behind them; nobody remembers the fourteenth
factory. If the generator says the median gap is well under the classic's
111, the answer is usually fewer buildings rather than a wider band.

Keep everything between columns **140 and 2860**; inside that is the wall at
the end of the world.

Buildings need 24 columns each, must not sit on a strip, must not be in the
170 columns in front of one, and must not land on the columns a field keeps
for its own hangar, fuel dump and tank. The generator refuses and tells you
where to put them instead.

The player's tank stands *in front* of its field, towards the enemy — it is
the one building on the map that is meant to be in the way.

## `ox` — cattle

```
ox at=1500
```

**Nought, one or two — two is the maximum, not the default.** Cattle are
worth having: they are the only thing in the game that is nobody's enemy,
and they are the only thing a pilot can lose points to by accident. But a
map with no cattle is a perfectly good map, and twenty maps that all have
exactly two oxen look like twenty maps made by the same rule. Vary it: an
empty valley, a single animal on the flat, a pair a long way apart.

The height is worked out from the ground under them.

**They need room.** Killing an ox costs 200 points, and a bullet or a bomb
kills it outright — only the blast itself spares it. An ox standing against
a building turns that building into a trap: there is no way to bomb it
without risking the fine, and a pilot cannot see far enough ahead to plan
around it.

So `at=` is a request, not an instruction. The generator keeps **40 columns
clear** of every building and off the strips and their take-off runs, moving
the ox to the nearest column that has room and saying so in a note:

```
note: the ox asked for at 1310 stands at 1262: an ox needs 40 columns clear
      of a building, or bombing that building costs 200
```

It also tries to keep the two oxen **200 columns apart** — two animals in
one field are one hazard, and one stray bomb can cost 400. That one is a
preference: if the map has nowhere better the ox is still placed and a note
says they are crowded. Only a map with no room at all within 600 columns is
refused.

The classic map's own numbers, for reference: oxen at 1376 and 1608, 48 and
42 columns from the nearest building, 232 apart.
