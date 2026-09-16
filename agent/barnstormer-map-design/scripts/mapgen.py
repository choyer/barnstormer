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

VERSION = "1.0.0"          # this skill's version; see ../manifest.json
MAP_FORMAT_VERSION = 1     # what the generated file declares in its header

MAX_X, MAX_Y = 3000, 200
GROUND_MIN, GROUND_MAX = 26, 199
RUNWAY_SPAN, TARGET_WIDTH = 21, 16
MAX_RUNWAYS, MAX_TARGETS, MAX_OXEN = 8, 20, 2
KINDS = {"house": 0, "factory": 1, "fuel": 2, "hangar": 3}

# Measured with `make probe`; see references/rules.md.
TAKEOFF_RUN = 103          # columns before the wheels leave the ground
TAKEOFF_CLEAR = 137        # columns before it is above building height
CORRIDOR = 170             # what the original leaves clear ahead of a field
EDGE_WIDTH = 140           # the wall at each end of the world
EDGE_PEAK = 186            # high enough that it reads as the end, not a hill
FREE_AIR = 130             # ground above this leaves no room to turn round
WALL = 160                 # ground above this is a wall, not scenery


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

    Slots are positional in the game: 0 is the player, 7 the enemy, and vs the
    computer 1 and 6 as well.  Laying slots 0-3 at the player's end and 4-7 at
    the enemy's means every mode spawns friends at one end and enemies at the
    other, whichever table it indexes.
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

        lo, hi = min(xs) - 8, max(xs) + RUNWAY_SPAN + 8
        span = [orig[i] for i in range(max(0, lo), min(MAX_X, hi))]
        pad = sorted(span)[len(span) // 2]

        # The strips, and the run in front of them.
        apron_lo = lo if out > 0 else lo - CORRIDOR
        apron_hi = hi + CORRIDOR if out > 0 else hi
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

    return runways


# ---- buildings and cattle ------------------------------------------------

def cluster_positions(lo, hi, count, rng):
    """Where a group of buildings actually goes.

    Evenly spacing them reads as fence posts: one structure right next to
    another, all the way across the map.  Real installations come in twos and
    threes with open ground between them, which also gives a pilot somewhere
    to make a second pass from.
    """
    if count <= 1:
        return [lo]

    sizes, left = [], count
    while left > 0:
        n = 2 + (rng.next() % 3)              # clusters of two to four
        if left - n == 1:                     # never leave a lone straggler
            n = left
        n = min(n, left)
        sizes.append(n)
        left -= n

    # A pitch per cluster, not one for the whole map: buildings the same
    # distance apart all the way across read as a fence however they are
    # grouped.  Sixteen wide plus half as much again, at least.
    pitches = [TARGET_WIDTH + 16 + rng.next() % 12 for _ in sizes]
    inside = sum((n - 1) * p for n, p in zip(sizes, pitches))
    gaps = len(sizes) - 1
    room = hi - lo

    if room < inside + gaps * 70:
        raise Fail(f"{count} buildings between {lo} and {hi} do not fit as "
                   f"{len(sizes)} clusters with room between them; give them "
                   f"{inside + gaps * 70 + 40} columns, or ask for fewer")

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


def place_buildings(groups, singles, runways):
    """Space each group out, keeping clear of the strips and the ends.

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

    for n, o in singles:
        owner = o.get("owner")
        if owner not in out:
            raise Fail(f"line {n}: a building needs owner=player or "
                       f"owner=enemy")
        if "at" not in o:
            raise Fail(f"line {n}: a building needs at=")
        kind = KINDS.get(o.get("kind", "house"))
        if kind is None:
            raise Fail(f"line {n}: {o.get('kind')!r} is not a building kind")
        out[owner].append((int(o["at"]), kind))

    for n, o in groups:
        owner = o.get("owner")
        if owner not in out:
            raise Fail(f"line {n}: buildings need owner=player or owner=enemy")
        try:
            lo, hi, count = int(o["from"]), int(o["to"]), int(o["count"])
        except KeyError:
            raise Fail(f"line {n}: buildings need from= to= count=")
        kinds = [KINDS[k] for k in o.get("kinds", "house,factory").split(",")
                 if k in KINDS] or [0]
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
                   f"{MAX_TARGETS}")

    # [enemy x7][the player's][the rest of the enemy's]
    ordered = enemy[:7] + player + enemy[7:]
    return ordered, len(player)


def place_oxen(ground, oxen):
    out = []
    for n, o in oxen[:MAX_OXEN]:
        if "at" not in o:
            raise Fail(f"line {n}: an ox needs at=")
        x = max(0, min(MAX_X - TARGET_WIDTH, int(o["at"])))
        out.append((x, min(MAX_Y - 1, ground[x] + 16)))
    return out


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


def lint(ground, runways, targets, n_player):
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

    if targets and n_player != 3:
        say.append(f"{n_player} buildings are the player's; the game gives "
                   f"the player exactly the ones at index 7, 8 and 9, so "
                   f"three is the number that works")
    if 0 < len(targets) < 10:
        say.append(f"{len(targets)} buildings: with fewer than ten, indices "
                   f"7-9 may not exist and the player may own none of them")
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
        runways = place_fields(ground, rec["fields"])
        targets, n_player = place_buildings(rec["buildings"], rec["singles"],
                                            runways)
        oxen = place_oxen(ground, rec["oxen"])
    except Fail as e:
        if args.json:
            _json.dump({"ok": False, "recipe": args.recipe,
                        "error": str(e)}, sys.stdout)
            print()
        print(f"{args.recipe}: {e}", file=sys.stderr)
        return 1

    out = args.out or os.path.splitext(args.recipe)[0] + ".map"
    runs = write_map(out, rec, ground, runways, targets, oxen)
    notes = lint(ground, runways, targets, n_player)

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
            "notes": notes,
            "profile": profile(ground, runways, targets, oxen),
            "verify": "scripts/verify.sh " + out,
        }, sys.stdout, indent=2)
        print()
    elif not args.quiet:
        print(profile(ground, runways, targets, oxen))
        print()
        print(f"{out}: {rec['name']}, {runs} terrain runs, "
              f"{len([r for r in runways if r])} runways, {len(targets)} "
              f"buildings ({n_player} the player's), {len(oxen)} oxen")
        for w in notes:
            print(f"  note: {w}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
