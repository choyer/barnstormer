#!/usr/bin/env python3
"""mapgen.py -- turn a map recipe into a Barnstormer map file.

The recipe is the source; the .map is the artifact.  Terrain is described as a
few named landforms laid left to right, fields and buildings by where they go,
and this works out the 3000 columns, the slot order and the ownership order --
the parts that are tedious to do by hand and easy to get subtly wrong.

    mapgen.py mountain-pass.recipe -o mountain-pass.map
    mapgen.py mountain-pass.recipe -o out.map --json    # for a program

Output is the same canonical form map_save() writes, so a map that comes
back out of the editor differs only where it was edited.  It does not depend
on this script's options: --quiet and --json change what is printed, never
what is written.

Exit status is 0 when the map was written, 1 when the recipe is wrong (the
reason goes to stderr, and to the "error" field under --json), 2 for usage.

See references/recipe.md for the vocabulary and references/rules.md for why
the rules are what they are.
"""
import argparse
import json as _json
import math
import os
import re
import sys

VERSION = "1.6.0"          # this skill's version; see ../manifest.json
MAP_FORMAT_VERSION = 1     # what the generated file declares in its header

MAX_X, MAX_Y = 3000, 200
GROUND_MIN, GROUND_MAX = 26, 199
RUNWAY_SPAN, TARGET_WIDTH = 21, 16
MAX_RUNWAYS, MAX_TARGETS, MAX_OXEN = 8, 20, 2
# The four silhouettes, by the numbers the format stores (include/map.h,
# doc/MAP_FORMAT.md).  Kind 0 is the flagged shed with the open front that
# the classic map stands beside both airfields; kind 3 is the tank.
KINDS = {"hangar": 0, "factory": 1, "fuel": 2, "tank": 3}

# Measured with `make probe`; see references/rules.md.
TAKEOFF_RUN = 103          # columns before the wheels leave the ground
TAKEOFF_CLEAR = 137        # columns before it is above building height
CORRIDOR = 170             # what the original leaves clear ahead of a field
EDGE_WIDTH = 140           # the wall at each end of the world
EDGE_PEAK = 186            # high enough that it reads as the end, not a hill
FREE_AIR = 130             # ground above this leaves no room to turn round
WALL = 160                 # ground above this is a wall, not scenery

# Every airfield gets the classic map's own pair of buildings, behind the
# home strip: a hangar 30 columns back and a fuel dump 60 back.  In the
# classic map the player's field is slot 0 at 1270 facing right with a hangar
# at 1240 and a fuel dump at 1210, and the enemy's is slot 7 at 1720 facing
# left with the mirror image at 1750 and 1780.  A field without them is a
# strip in a field; with them it is somewhere aircraft come from.
FIELD_HANGAR_BACK = 30     # the hangar, behind the home strip
FIELD_FUEL_BACK = 60       # the fuel dump, behind that

# Cattle.  The classic map's own clearances: its oxen stand 48 and 42
# columns from the nearest building and 232 columns apart.
OX_WIDTH = 16              # the sprite, same as a building
OX_CLEAR = 40              # columns of daylight an ox needs either side
OX_APART = 200             # two oxen closer than this are one target
OX_PENALTY = 200           # what killing one costs (game/collision.c)

# How many things a map wants standing on it, and how far apart.  The floor
# is arithmetic, not taste: the game hands the player the buildings at index
# 7, 8 and 9, so a map with fewer than ten has no index 9 and the player
# loses buildings it should own.  The ceiling is the fixed array in game_t.
# Between them it is a judgement, and the classic map is the yardstick: 20
# buildings, but spread from 191 to 2763 with a median gap of 111 columns.
# A map earns its interest from its terrain; buildings are what there is to
# do once you are there.
MIN_TARGETS = 10
ENEMY_MIN = 7              # so the player's three land on 7, 8 and 9
CLASSIC_MEDIAN_GAP = 111   # measured on the classic map
CLUSTER_PITCH = 24         # inside a group: 16 wide plus this, plus 0..23
CLUSTER_APART = 120        # open ground between groups


class Fail(Exception):
    pass


# ---- deterministic noise, so a recipe always gives the same map -----------

class Rng:
    def __init__(self, seed):
        self.s = (seed or 7491) & 0xFFFFFFFF

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return self.s

    def unit(self):                      # -1.0 .. 1.0
        return (self.next() % 20001) / 10000.0 - 1.0


# ---- the recipe ----------------------------------------------------------

def parse(path):
    rec = {"name": None, "author": None, "seed": 7491,
           "land": [], "fields": [], "buildings": [], "singles": [],
           "oxen": []}

    for n, raw in enumerate(open(path), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        opts = dict(re.findall(r"(\w+)=([^\s]+)", rest))
        first = rest.split()[0] if rest else ""

        try:
            if word == "name":
                rec["name"] = rest
            elif word == "author":
                rec["author"] = rest
            elif word == "seed":
                rec["seed"] = int(rest)
            elif word == "land":
                rec["land"].append((n, first, opts))
            elif word == "field":
                rec["fields"].append((n, first, opts))
            elif word == "buildings":
                rec["buildings"].append((n, opts))
            elif word == "building":
                rec["singles"].append((n, opts))
            elif word == "ox":
                rec["oxen"].append((n, opts))
            else:
                raise Fail(f"line {n}: unknown recipe word {word!r}")
        except ValueError as e:
            raise Fail(f"line {n}: {e}")

    if not rec["name"]:
        raise Fail("the recipe has no name line")
    if not rec["land"]:
        raise Fail("the recipe has no land lines, so there is no terrain")
    return rec


# ---- terrain -------------------------------------------------------------

def blend(col, start, end, width):
    """Smooth step from start to end across width -- never a cliff."""
    t = 0.0 if width <= 1 else col / (width - 1)
    return start + (end - start) * (0.5 - 0.5 * math.cos(math.pi * t))


def build_terrain(land, rng):
    total = sum(int(o.get("width", 0)) for _, _, o in land)
    if total != MAX_X:
        short = MAX_X - total
        raise Fail(f"the land widths total {total}, and a map is {MAX_X} "
                   f"columns: {'add' if short > 0 else 'remove'} {abs(short)}")

    g = []
    prev = None
    for n, kind, o in land:
        w = int(o["width"])
        rough = float(o.get("rough", 0))

        def need(key, default=None):
            if key in o:
                return float(o[key])
            if default is not None:
                return default
            raise Fail(f"line {n}: {kind} needs {key}=")

        if kind == "plain":
            h = need("h", prev if prev is not None else 60)
            seg = [blend(i, prev if prev is not None else h, h, min(w, 120))
                   if i < min(w, 120) else h for i in range(w)]
        elif kind == "slope":
            h = need("h")
            start = prev if prev is not None else h
            seg = [blend(i, start, h, w) for i in range(w)]
        elif kind == "plateau":
            h = need("h")
            start = prev if prev is not None else h
            rise = min(w // 3, 200)
            seg = [blend(i, start, h, rise) if i < rise else h
                   for i in range(w)]
        elif kind in ("hill", "ridge"):
            peak = need("peak")
            base = prev if prev is not None else peak - 40
            seg = [base + (peak - base) * math.sin(math.pi * (i + 0.5) / w)
                   for i in range(w)]
            if kind == "ridge":          # flatter on top, steeper on the sides
                seg = [base + (v - base) ** 1.0 for v in seg]
        elif kind == "valley":
            floor = need("floor")
            base = prev if prev is not None else floor + 40
            seg = [base - (base - floor) * math.sin(math.pi * (i + 0.5) / w)
                   for i in range(w)]
        else:
            raise Fail(f"line {n}: unknown landform {kind!r}")

        if rough:
            walk = 0.0
            for i in range(w):
                walk = max(-rough, min(rough, walk + rng.unit() * rough * 0.5))
                seg[i] += walk

        g.extend(seg)
        prev = seg[-1]

    # Two smoothing passes: the joins are already continuous, this takes the
    # stair-steps off the roughness so it reads as land rather than as noise.
    for _ in range(2):
        g = [g[0]] + [(g[i - 1] + g[i] + g[i + 1]) / 3 for i in
                      range(1, len(g) - 1)] + [g[-1]]

    g = [max(GROUND_MIN, min(GROUND_MAX, int(round(v)))) for v in g]
    return add_edges(g)


def add_edges(g):
    """Wall off both ends of the world.

    The world stops at column 0 and column 3000 and an aeroplane that reaches
    either just sits there against nothing.  A steep rise at each end says so
    in the only language the game has: you can see it coming, and there is
    visibly nothing beyond it.
    """
    for i in range(EDGE_WIDTH):
        inner = g[EDGE_WIDTH]
        g[i] = int(round(blend(i, EDGE_PEAK, inner, EDGE_WIDTH)))

        j = MAX_X - 1 - i
        inner = g[MAX_X - 1 - EDGE_WIDTH]
        g[j] = int(round(blend(i, EDGE_PEAK, inner, EDGE_WIDTH)))

    return [max(GROUND_MIN, min(GROUND_MAX, v)) for v in g]


# ---- fields --------------------------------------------------------------

def place_fields(ground, fields):
    """Lay out both ends and flatten what each one needs.

    A field is not just a flat pad: an aeroplane needs 103 columns to leave
    the ground and 137 to climb over building height, so the flat runs on in
    the direction the strips face and then blends back into the land.  Without
    that the hillside beyond the pad is a wall at the end of the runway, which
    is the commonest way a generated map turns out unflyable.

    The flat also runs the other way, far enough for the hangar and the fuel
    dump behind the home strip to stand on the field rather than on whatever
    the land was doing there.

    Slots are positional in the game: 0 is the player, 7 the enemy, and vs the
    computer 1 and 6 as well.  Laying slots 0-3 at the player's end and 4-7 at
    the enemy's means every mode spawns friends at one end and enemies at the
    other, whichever table it indexes.  Slot 0 and slot 7 are the home strips,
    and they are the ones the airfield buildings belong to.
    """
    ends = {}
    for n, who, o in fields:
        if who not in ("player", "enemy"):
            raise Fail(f"line {n}: a field is player or enemy, not {who!r}")
        if "at" not in o:
            raise Fail(f"line {n}: a field needs at=")
        ends[who] = (int(o["at"]), o.get("facing", "right" if who == "player"
                                         else "left"))
    for who in ("player", "enemy"):
        if who not in ends:
            raise Fail(f"there is no {who} field; a map needs both")

    orig = list(ground)
    runways = [None] * MAX_RUNWAYS

    for who, slots in (("player", [0, 1, 2, 3]), ("enemy", [7, 6, 5, 4])):
        at, facing = ends[who]
        orient = 0 if facing == "right" else 1
        out = 1 if orient == 0 else -1          # the way they take off

        xs = []
        for i in range(len(slots)):
            x = at + i * (RUNWAY_SPAN + 29) * (1 if who == "player" else -1)
            xs.append(max(0, min(MAX_X - RUNWAY_SPAN - 1, x)))

        if min(xs) < EDGE_WIDTH + 20 or max(xs) > MAX_X - EDGE_WIDTH - 60:
            raise Fail(f"the {who} field at {at} is in the wall at the end of "
                       f"the world; keep fields between {EDGE_WIDTH + 20} and "
                       f"{MAX_X - EDGE_WIDTH - 80}")

        # The hangar and the fuel dump sit behind the home strip, and they
        # are as much part of the world's edge arithmetic as the strips are.
        hangar, fuel = field_buildings(at, facing)
        for bx, what in ((hangar, "hangar"), (fuel, "fuel dump")):
            if bx < EDGE_WIDTH or bx + TARGET_WIDTH > MAX_X - EDGE_WIDTH:
                room = FIELD_FUEL_BACK + TARGET_WIDTH
                raise Fail(f"the {who} field at {at} leaves no room for its "
                           f"{what} at {bx}: every field carries a hangar "
                           f"{FIELD_HANGAR_BACK} columns behind the home "
                           f"strip and a fuel dump {FIELD_FUEL_BACK} behind "
                           f"it, so keep a {facing}-facing field between "
                           f"{EDGE_WIDTH + room} and "
                           f"{MAX_X - EDGE_WIDTH - room}")

        # The player's tank stands past the far end of every take-off run,
        # between its own field and the enemy's: it is what the enemy meets
        # first, and it must not be a wall at the end of a strip.
        tank = field_tank(xs, facing) if who == "player" else None
        if tank is not None and not (
                EDGE_WIDTH <= tank and tank + TARGET_WIDTH <= MAX_X - EDGE_WIDTH):
            raise Fail(f"the player field at {at} leaves no room for its tank "
                       f"at {tank}: it stands {CORRIDOR} columns beyond the "
                       f"last strip, facing the enemy. Move the field back "
                       f"from the end of the world")

        lo, hi = min(xs) - 8, max(xs) + RUNWAY_SPAN + 8
        span = [orig[i] for i in range(max(0, lo), min(MAX_X, hi))]
        pad = sorted(span)[len(span) // 2]

        # The strips, the run in front of them, the apron behind that the
        # airfield buildings stand on, and the ground under the tank.
        back = FIELD_FUEL_BACK + TARGET_WIDTH + 8
        apron_lo = (lo if out > 0 else lo - CORRIDOR)
        apron_hi = (hi + CORRIDOR if out > 0 else hi)
        if out > 0:
            apron_lo = min(apron_lo, at - back)
            if tank is not None:
                apron_hi = max(apron_hi, tank + TARGET_WIDTH + 8)
        else:
            apron_hi = max(apron_hi, at + back)
            if tank is not None:
                apron_lo = min(apron_lo, tank - 8)
        for i in range(max(0, apron_lo), min(MAX_X, apron_hi)):
            ground[i] = pad

        # Back into the land beyond, gently enough to fly over.
        for k in range(140):
            i = (apron_hi + k) if out > 0 else (apron_lo - 1 - k)
            if not 0 <= i < MAX_X:
                break
            ground[i] = int(round(blend(k, pad, orig[i], 140)))

        for slot, x in zip(slots, xs):
            runways[slot] = (x, orient)

    return runways, ends


# ---- buildings and cattle ------------------------------------------------

def cluster_positions(lo, hi, count, rng):
    """Where a group of buildings actually goes.

    Measured on the classic map, which is the yardstick worth matching: its
    twenty buildings run from 191 to 2763, its **median gap is 111 columns**,
    and apart from the two airfield pairs every gap is 69 or more.  It has no
    huddles at all -- it is singles and the occasional pair, scattered across
    the whole world, and that is what gives a pilot somewhere to turn round
    and come back from.

    So: mostly singles, a pair now and then for somewhere that reads as one
    installation, and open ground between.
    """
    if count <= 1:
        return [lo]

    sizes, left = [], count
    while left > 0:
        n = 2 if rng.next() % 4 == 0 else 1   # a quarter of them are pairs
        n = min(n, left)
        sizes.append(n)
        left -= n

    # A pitch per cluster, not one for the whole map: buildings the same
    # distance apart all the way across read as a fence however they are
    # grouped.
    pitches = [TARGET_WIDTH + CLUSTER_PITCH + rng.next() % 24 for _ in sizes]
    inside = sum((n - 1) * p for n, p in zip(sizes, pitches))
    gaps = len(sizes) - 1
    room = hi - lo

    if room < inside + gaps * CLUSTER_APART:
        raise Fail(f"{count} buildings between {lo} and {hi} do not fit as "
                   f"{len(sizes)} groups with {CLUSTER_APART} columns of open "
                   f"ground between them; give them "
                   f"{inside + gaps * CLUSTER_APART + 40} columns, or ask for "
                   f"fewer -- terrain is what makes a map, not the number of "
                   f"things standing on it")

    slack = room - inside
    share = slack / (gaps + 1) if gaps else slack
    out, x = [], float(lo)
    for i, (n, pitch) in enumerate(zip(sizes, pitches)):
        for k in range(n):
            out.append(int(x + k * pitch))
        x += (n - 1) * pitch
        if i < gaps:
            x += share * (0.7 + 0.6 * ((rng.next() % 100) / 100.0))
    return out


def field_buildings(at, facing):
    """Where a field's hangar and fuel dump go: behind the home strip.

    Behind, because the 170 columns in front are the take-off run, and at
    these two offsets because that is where the classic map puts them.
    """
    back = -1 if facing == "right" else 1
    return (at + back * FIELD_HANGAR_BACK, at + back * FIELD_FUEL_BACK)


def field_tank(strips, facing):
    """Where the player's tank goes: the field's own defence.

    The third building the player owns is a defensive unit, so it stands on
    the enemy's side of its airfield -- between the two fields, the first
    thing an attack run meets -- and one column past the far end of every
    take-off run, so it is cover rather than a wall at the end of a strip.
    The classic map does the same thing: its player field is slot 0 at 1270
    facing right, and its third player building sits at 1440, exactly 170
    columns along.
    """
    if facing == "right":
        return max(strips) + RUNWAY_SPAN + CORRIDOR + 1
    return min(strips) - CORRIDOR - TARGET_WIDTH - 1


def place_buildings(groups, singles, runways, ends):
    """Space each group out, keeping clear of the strips and the ends.

    Every airfield brings its own hangar and fuel dump, and the player's
    field also brings the tank that defends it: all three of the player's
    buildings are placed here, so a recipe asks only for the enemy's.

    The order of the output is not cosmetic: the game gives the player the
    buildings at index 7, 8 and 9 and the enemy all the rest, so the player's
    are emitted in those positions whatever order the recipe lists them in.
    """
    def clear_of_runways(x):
        return all(not (x <= rx + RUNWAY_SPAN - 1 and x + TARGET_WIDTH - 1 >= rx)
                   for rx, _ in runways if rx is not None)

    def in_takeoff_path(x):
        """In front of a field, within the run it needs to get airborne.

        The classic map leaves 170 columns ahead of each field and puts the
        rest of the buildings behind it; an aeroplane needs 137 to climb over
        building height, so anything inside that is a wall at the end of the
        runway."""
        for r in runways:
            if r is None:
                continue
            rx, orient = r
            lo, hi = ((rx - CORRIDOR, rx) if orient
                      else (rx + RUNWAY_SPAN, rx + CORRIDOR))
            if x + TARGET_WIDTH - 1 >= lo and x <= hi:
                return rx
        return None

    out = {"player": [], "enemy": []}
    reserved = {}

    # The airfields first, so they are the ones a clash is reported against.
    for who in ("player", "enemy"):
        at, facing = ends[who]
        hangar, fuel = field_buildings(at, facing)
        out[who] += [(hangar, KINDS["hangar"]), (fuel, KINDS["fuel"])]
        reserved[hangar] = f"the {who} field's hangar"
        reserved[fuel] = f"the {who} field's fuel dump"

    # And the player's tank, which is the third building it owns.
    at, facing = ends["player"]
    strips = [r[0] for i, r in enumerate(runways) if r and i in (0, 1, 2, 3)]
    tank = field_tank(strips, facing)
    out["player"].append((tank, KINDS["tank"]))
    reserved[tank] = "the player's tank"

    for n, o in singles:
        owner = o.get("owner")
        if owner not in out:
            raise Fail(f"line {n}: a building needs owner=player or "
                       f"owner=enemy")
        if "at" not in o:
            raise Fail(f"line {n}: a building needs at=")
        kind = KINDS.get(o.get("kind", "hangar"))
        if kind is None:
            raise Fail(f"line {n}: {o.get('kind')!r} is not a building kind; "
                       f"they are {', '.join(KINDS)}")
        out[owner].append((int(o["at"]), kind))

    for n, o in groups:
        owner = o.get("owner")
        if owner not in out:
            raise Fail(f"line {n}: buildings need owner=player or owner=enemy")
        try:
            lo, hi, count = int(o["from"]), int(o["to"]), int(o["count"])
        except KeyError:
            raise Fail(f"line {n}: buildings need from= to= count=")
        words = o.get("kinds", "hangar,factory").split(",")
        for k in words:
            if k not in KINDS:
                raise Fail(f"line {n}: {k!r} is not a building kind; they "
                           f"are {', '.join(KINDS)}")
        kinds = [KINDS[k] for k in words]
        if count < 1:
            continue

        rng = Rng(lo * 7919 + hi * 104729 + count)
        for i, x in enumerate(cluster_positions(lo, hi, count, rng)):
            out[owner].append((x, kinds[i % len(kinds)]))

    # Everything, checked where it ended up.
    for owner, items in out.items():
        for x, _ in items:
            if x < EDGE_WIDTH or x + TARGET_WIDTH > MAX_X - EDGE_WIDTH:
                raise Fail(f"a building at {x} is inside the wall at the end "
                           f"of the world; keep buildings between "
                           f"{EDGE_WIDTH} and {MAX_X - EDGE_WIDTH}")
            if not clear_of_runways(x):
                raise Fail(f"a building at {x} stands on a runway")
            for rx, what in reserved.items():
                if x != rx and abs(x - rx) < TARGET_WIDTH:
                    raise Fail(f"a building at {x} overlaps {what} at {rx}; "
                               f"every field keeps the "
                               f"{FIELD_FUEL_BACK + TARGET_WIDTH} columns "
                               f"behind its home strip for those two, so put "
                               f"this group beyond them")
            blocked = in_takeoff_path(x)
            if blocked is not None:
                raise Fail(
                    f"a building at {x} stands in the take-off path of the "
                    f"field at {blocked}. An aeroplane needs {TAKEOFF_CLEAR} "
                    f"columns to climb over building height, and the classic "
                    f"map leaves {CORRIDOR}. Put these buildings behind the "
                    f"field instead, or start the group beyond column "
                    f"{blocked + CORRIDOR}")

    enemy, player = out["enemy"], out["player"]
    if len(enemy) + len(player) > MAX_TARGETS:
        raise Fail(f"{len(enemy) + len(player)} buildings; a map holds "
                   f"{MAX_TARGETS}, and five of them are placed for you: "
                   f"each airfield's hangar and fuel dump, and the player's "
                   f"tank")
    if len(player) != 3:
        raise Fail(f"{len(player)} buildings are the player's, and the game "
                   f"gives the player exactly the ones at index 7, 8 and 9. "
                   f"All three are placed for you -- the field's hangar and "
                   f"fuel dump behind the home strip, and the tank that "
                   f"defends it {CORRIDOR} columns out towards the enemy -- "
                   f"so a recipe asks for no owner=player buildings at all, "
                   f"only the enemy's")
    if len(enemy) < ENEMY_MIN:
        raise Fail(f"{len(enemy)} of the buildings are the enemy's, and the "
                   f"player's three have to land on index 7, 8 and 9: with "
                   f"fewer than {ENEMY_MIN} enemy buildings ahead of them "
                   f"they land earlier and the game hands some of the "
                   f"player's own buildings to the enemy. The enemy field "
                   f"already carries two, so ask for "
                   f"{ENEMY_MIN - 2} to {MAX_TARGETS - 5} more with "
                   f"owner=enemy -- a map wants {MIN_TARGETS} buildings at "
                   f"least and rarely wants all {MAX_TARGETS}")

    # [enemy x7][the player's][the rest of the enemy's]
    ordered = enemy[:7] + player + enemy[7:]
    return ordered, len(player)


def ox_clearance(x, targets):
    """Columns between an ox at x and the nearest building; negative overlaps."""
    if not targets:
        return MAX_X
    return min((x - (tx + TARGET_WIDTH)) if x > tx else (tx - (x + OX_WIDTH))
               for tx, _kind in targets)


def place_oxen(ground, oxen, targets, runways):
    """Cattle, moved clear of the buildings rather than left in the blast.

    An ox costs whoever kills it 200 points, and a bullet or a bomb kills it
    outright -- `collision.c` spares it only from OBJ_EXPLOSION and
    OBJ_STARBURST.  So an ox parked against a building is
    not scenery, it is a fine for attacking that building, and a pilot
    cannot see far enough ahead to plan around it.  The classic map leaves
    48 and 42 columns between its oxen and the nearest building.

    A recipe names a column, but the buildings around it were laid out by
    the generator, so the author cannot know what it will land beside.  The
    ox is therefore nudged to the nearest column that has room, and the move
    is reported; only a map with nowhere to put it at all is refused.
    """
    out, notes = [], []

    def on_a_field(x):
        for r in runways:
            if r is None:
                continue
            rx, orient = r
            lo, hi = ((rx - CORRIDOR, rx + RUNWAY_SPAN) if orient
                      else (rx, rx + CORRIDOR))
            if x + OX_WIDTH - 1 >= lo and x <= hi:
                return True
        return False

    for n, o in oxen[:MAX_OXEN]:
        if "at" not in o:
            raise Fail(f"line {n}: an ox needs at=")
        want = max(EDGE_WIDTH, min(MAX_X - EDGE_WIDTH - OX_WIDTH,
                                   int(o["at"])))

        # Candidates outwards from where the recipe asked, nearest first;
        # the other ox counts as something to stay away from as well.
        others = targets + [(px, None) for px, _py in out]
        best = None
        for d in range(0, 601):
            for x in ((want,) if d == 0 else (want - d, want + d)):
                if not EDGE_WIDTH <= x <= MAX_X - EDGE_WIDTH - OX_WIDTH:
                    continue
                if ox_clearance(x, others) < OX_CLEAR:
                    continue
                if on_a_field(x):
                    continue          # an aeroplane would hit it taking off
                far = (not out) or abs(x - out[0][0]) >= OX_APART
                if far:
                    best = x
                    break
                if best is None:      # room, but crowding the other ox
                    best = x
            if best is not None and (not out or abs(best - out[0][0]) >= OX_APART):
                break
        if best is None:
            raise Fail(f"line {n}: nowhere within 600 columns of {want} for "
                       f"an ox: it needs {OX_CLEAR} columns clear of every "
                       f"building and of the strips. Thin the buildings out "
                       f"or ask for the ox somewhere emptier")
        if best != want:
            notes.append(f"the ox asked for at {want} stands at {best}: an "
                         f"ox needs {OX_CLEAR} columns clear of a building, "
                         f"or bombing that building costs {OX_PENALTY}")

        out.append((best, min(MAX_Y - 1, ground[best] + 16)))
    return out, notes


# ---- writing it out ------------------------------------------------------

def write_map(path, rec, ground, runways, targets, oxen):
    runs = []
    x = 0
    while x < MAX_X:
        h, run = ground[x], 1
        while x + run < MAX_X and ground[x + run] == h:
            run += 1
        runs.append(f"{run}:{h}")
        x += run

    lines = [f"barnstormer-map 1", f"name {rec['name']}"]
    if rec["author"]:
        lines.append(f"author {rec['author']}")
    lines += [f"seed {rec['seed']}", f"size {MAX_X} {MAX_Y}", ""]

    line, col = "ground", 6
    for r in runs:
        line += " " + r
        col += len(r) + 1
        if col >= 68:
            lines.append(line)
            line, col = "ground", 6
    if col > 6:
        lines.append(line)

    lines.append("")
    lines += [f"runway {x} {o}" for x, o in runways if x is not None]
    lines.append("")
    lines += [f"target {x} {k}" for x, k in targets]
    if oxen:
        lines.append("")
        lines += [f"ox {x} {y}" for x, y in oxen]

    open(path, "w").write("\n".join(lines) + "\n")
    return len(runs)


# ---- looking at it -------------------------------------------------------

def profile(ground, runways, targets, oxen, width=96, height=16):
    """An elevation profile, for reading the shape without launching a game."""
    cols = []
    for c in range(width):
        lo = c * MAX_X // width
        hi = (c + 1) * MAX_X // width
        cols.append(max(ground[lo:hi]))

    marks = [" "] * width
    for x, _ in targets:
        marks[min(width - 1, x * width // MAX_X)] = "#"
    for x, _ in oxen:
        marks[min(width - 1, x * width // MAX_X)] = "o"
    for x, _ in [r for r in runways if r]:
        marks[min(width - 1, x * width // MAX_X)] = "^"

    out = []
    for row in range(height, 0, -1):
        band = MAX_Y * row // height
        line = "".join("#" if h >= band else " " for h in cols)
        out.append(f"{band:3d} |{line}")
    out.append("    +" + "-" * width)
    out.append("     " + "".join(marks))
    out.append("     0" + " " * (width - 10) + f"{MAX_X}")
    return "\n".join(out)


def median_gap(targets):
    """Open ground between one building and the next, as the map reads it.

    The number to compare with the classic map's 111: it says whether the
    buildings are scattered across a world or huddled in a band.
    """
    xs = sorted(x for x, _kind in targets)
    if len(xs) < 2:
        return None
    gaps = sorted(b - (a + TARGET_WIDTH) for a, b in zip(xs, xs[1:]))
    return gaps[len(gaps) // 2]


def relief(ground):
    """How much the land does inside the walls: range, and how uneven."""
    inland = ground[EDGE_WIDTH:MAX_X - EDGE_WIDTH]
    mean = sum(inland) / len(inland)
    sd = (sum((h - mean) ** 2 for h in inland) / len(inland)) ** 0.5
    return max(inland) - min(inland), round(sd, 1)


def lint(ground, runways, targets, oxen):
    """Things that load and fly but make a poor map."""
    say = []

    inland = ground[EDGE_WIDTH:MAX_X - EDGE_WIDTH]
    peak = max(inland)
    if peak > WALL:
        say.append(f"the highest ground is {peak}; above {WALL} is a wall "
                   f"the aeroplane can barely get over")
    elif peak > FREE_AIR:
        say.append(f"the highest ground is {peak}; above {FREE_AIR} there is "
                   f"no room to turn round, so that stretch is a corridor")

    for slot, r in enumerate(runways):
        if not r:
            continue
        x, orient = r
        step = -1 if orient else 1
        pad = ground[x]
        for d in range(RUNWAY_SPAN, TAKEOFF_CLEAR):
            probe = x + step * d
            if not (0 <= probe < MAX_X):
                say.append(f"runway {slot + 1} at {x} faces "
                           f"{'left' if orient else 'right'} with only {d} "
                           f"columns of world in front of it; take-off needs "
                           f"{TAKEOFF_CLEAR}")
                break
            if ground[probe] > pad + 16:
                say.append(f"runway {slot + 1} at {x} has ground "
                           f"{ground[probe] - pad} above the field {d} "
                           f"columns ahead; it needs {TAKEOFF_RUN} clear to "
                           f"get airborne and {TAKEOFF_CLEAR} to clear "
                           f"building height")
                break

    for x, _ in targets:
        for r in runways:
            if r is None:
                continue
            rx, orient = r
            lo, hi = ((rx - CORRIDOR, rx) if orient
                      else (rx + RUNWAY_SPAN, rx + CORRIDOR))
            if x + TARGET_WIDTH - 1 >= lo and x <= hi:
                say.append(f"the building at {x} is in the take-off path of "
                           f"the field at {rx}")

    # Cattle: the hard clearance is enforced in place_oxen(); these are the
    # judgement calls.
    if len(oxen) == 2 and abs(oxen[0][0] - oxen[1][0]) < OX_APART:
        say.append(f"the two oxen are {abs(oxen[0][0] - oxen[1][0])} columns "
                   f"apart; under {OX_APART} they are one hazard rather than "
                   f"two, and a single stray bomb can cost "
                   f"{2 * OX_PENALTY}. The classic map leaves 232")
    for x, _y in oxen:
        for r in runways:
            if r is None:
                continue
            rx, orient = r
            lo, hi = ((rx - CORRIDOR, rx + RUNWAY_SPAN) if orient
                      else (rx, rx + CORRIDOR))
            if x + OX_WIDTH - 1 >= lo and x <= hi:
                say.append(f"the ox at {x} is on the strip at {rx} or in the "
                           f"run in front of it; an aeroplane that hits it is "
                           f"wounded and fined {OX_PENALTY} on take-off")
                break

    # How crowded it is, against the map everybody has flown.  The floor on
    # the count is enforced in place_buildings(); this is the judgement:
    # buildings packed into a band are a shooting gallery, however varied the
    # land around them.
    gap = median_gap(targets)
    if gap is not None and gap < CLASSIC_MEDIAN_GAP // 2:
        say.append(f"the median gap between buildings is {gap} columns; the "
                   f"classic map leaves {CLASSIC_MEDIAN_GAP}. Either widen "
                   f"the from=/to= band or ask for fewer -- terrain is what "
                   f"makes a map, and {len(targets)} buildings in a huddle "
                   f"read as one target")
    return say


# ---- main ----------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("recipe")
    ap.add_argument("-o", "--out", help="where to write the .map")
    ap.add_argument("--quiet", action="store_true",
                    help="write the map and print nothing")
    ap.add_argument("--json", action="store_true",
                    help="print one JSON object instead, for a program: the "
                         "path written, the counts, the profile and the "
                         "notes -- or an error field and exit 1")
    ap.add_argument("--version", action="version",
                    version=f"mapgen {VERSION} "
                            f"(map format {MAP_FORMAT_VERSION})")
    args = ap.parse_args()

    try:
        rec = parse(args.recipe)
        rng = Rng(rec["seed"])
        ground = build_terrain(rec["land"], rng)
        runways, ends = place_fields(ground, rec["fields"])
        targets, n_player = place_buildings(rec["buildings"], rec["singles"],
                                            runways, ends)
        oxen, ox_notes = place_oxen(ground, rec["oxen"], targets, runways)
    except Fail as e:
        if args.json:
            _json.dump({"ok": False, "recipe": args.recipe,
                        "error": str(e)}, sys.stdout)
            print()
        print(f"{args.recipe}: {e}", file=sys.stderr)
        return 1

    out = args.out or os.path.splitext(args.recipe)[0] + ".map"
    runs = write_map(out, rec, ground, runways, targets, oxen)
    notes = ox_notes + lint(ground, runways, targets, oxen)

    if args.json:
        _json.dump({
            "ok": True,
            "generator": VERSION,
            "map_format_version": MAP_FORMAT_VERSION,
            "recipe": args.recipe,
            "map": out,
            "name": rec["name"],
            "author": rec["author"] or None,
            "seed": rec["seed"],
            "terrain_runs": runs,
            "runways": len([r for r in runways if r]),
            "buildings": len(targets),
            "player_buildings": n_player,
            "oxen": len(oxen),
            "ground_min": min(ground),
            "ground_max": max(ground),
            "relief": relief(ground)[0],
            "unevenness": relief(ground)[1],
            "median_building_gap": median_gap(targets),
            "classic_median_building_gap": CLASSIC_MEDIAN_GAP,
            "notes": notes,
            "profile": profile(ground, runways, targets, oxen),
            "verify": "scripts/verify.sh " + out,
        }, sys.stdout, indent=2)
        print()
    elif not args.quiet:
        print(profile(ground, runways, targets, oxen))
        print()
        rel, uneven = relief(ground)
        print(f"{out}: {rec['name']}, {runs} terrain runs, "
              f"{len([r for r in runways if r])} runways, {len(targets)} "
              f"buildings ({n_player} the player's), {len(oxen)} oxen")
        print(f"  terrain relief {rel} (unevenness {uneven}), "
              f"median building gap {median_gap(targets)} "
              f"(classic: {CLASSIC_MEDIAN_GAP})")
        for w in notes:
            print(f"  note: {w}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
