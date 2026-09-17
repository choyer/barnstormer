#!/usr/bin/env python3
"""mapshot.py -- draw a whole Barnstormer map as one picture.

The game shows a viewport; a map is 3000 columns wide.  This draws all of it
at once, in the game's own palette, so a map can be looked at, compared or
posted without launching anything:

    mapshot.py salt-flats.map                      # -> salt-flats.svg
    mapshot.py salt-flats.map --format png         # -> salt-flats.png
    mapshot.py salt-flats.map --layout map         # the world on its own

Two layouts:

    full   the world, then a panel with the map's name, its hash and the
           recipe that made it -- what to hand to somebody, or to review
    map    the world and nothing else -- a thumbnail, or an illustration
           something else captions

The recipe is found beside the map (`NAME.recipe`) unless `--recipe` names
one; `--no-recipe`, or a map with no recipe next to it, draws the panel with
the map's details and says so rather than inventing one.

Output is SVG by default: pure stdlib, no rasteriser, and small.  `--format
png` shells out to rsvg-convert or magick, whichever is on PATH, and exits 3
if neither is.

Exit status: 0 drawn, 1 the map (or the recipe) is not usable, 2 usage,
3 png was asked for and there is no rasteriser.

## Serving this

It is written to be run by a request handler as well as by a person:

* **Stateless and deterministic.** The same map gives byte-identical output
  forever -- no timestamps, no randomness, no locale in the numbers.  Cache
  on `(map hash, layout, scale, VERSION)`; `--json` reports all four.
* **Pure stdlib** for SVG.  Nothing is imported that is not in Python 3.8.
* **Untrusted input is expected.** Map files come from strangers: the parser
  refuses anything that is not a map of this format, caps the file at
  `--max-bytes`, holds names to the format's 63 bytes, drops control
  characters, and XML-escapes every string that reaches the drawing.  It
  never runs the game, never touches the network and writes only where it
  was told to.
* **Bounded work.** One pass over 3000 columns; `--scale 1` is a 3000x200
  world and the cheapest useful thumbnail, `--scale 2` the default.
* **`-o -`** writes the image to stdout, for piping straight into a
  response.

The hash is computed here, from the canonical serialisation in
doc/MAP_FORMAT.md, so a preview can be labelled without the game binary.
"""
import argparse
import json as _json
import os
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape

VERSION = "1.5.0"              # bump when the drawing changes: it is a cache key
MAP_FORMAT_VERSION = 1
MAP_MAGIC = "barnstormer-map"
MAP_MAGIC_WAS = "barnstormer-level"

MAX_X, MAX_Y = 3000, 200
GROUND_MIN, GROUND_MAX = 26, 199
NAME_MAX = 63
RUNWAY_SPAN, TARGET_WIDTH = 21, 16
MAX_RUNWAYS, MAX_TARGETS, MAX_OXEN = 8, 20, 2
CORRIDOR = 170                 # what a field needs clear in front of it
EDGE_WIDTH = 140               # the wall at each end of the world
FREE_AIR, WALL = 130, 160      # the two heights that decide how a map flies
GROUND_WRAP = 68               # where a canonical `ground` line breaks
FNV_OFFSET, FNV_PRIME = 2166136261, 16777619

DEFAULT_MAX_BYTES = 1 << 20    # a hand-drawn map is about 6 KB

# The game's palette, from render/raster.c -- a preview should look like the
# thing it is a preview of.
SKY_TOP, SKY = "#060A14", "#16243A"
GROUND, GROUND_EDGE = "#6B4A2F", "#4E8F3A"
TEAM1, TEAM1_ACC = "#63E6F0", "#2A93A8"
TEAM2, TEAM2_ACC = "#F25FBE", "#A13279"
HUD, HUD_DIM, WILDLIFE = "#F2F2F2", "#7A8290", "#D8C08A"
PAPER, PANEL = "#0B0E16", "#101822"
FONT = "JetBrainsMono Nerd Font, DejaVu Sans Mono, monospace"

# The buildings and the cattle, pixel for pixel as the game draws them:
# sw_sprite_sets[SPRITE_TARGET] frames 0-3 and [SPRITE_OX] frame 0 from
# data/sprites.c, which tools/extract_sprites.py derives from the original
# CGA artwork (Copyright (C) 1984-2000 David L. Clark; see
# LICENSE.origsopwith.txt).  Kept here rather than read from the repository
# so the skill still draws the right thing when it is copied out of it.
#
# One character per pixel: 0 transparent, 1 the owner's colour, 2 its
# accent, 3 shared detail -- the same values obj_color() resolves in
# render/scene.c.  Row 0 is the top of the sprite.
SPRITE_TARGET = (
    # hangar (kind 0)
    ("0000000000020000",
     "0000000000021111",
     "0000000000021111",
     "0000000000020000",
     "0000000000020000",
     "0000000000020000",
     "0000000000020000",
     "1111111111111111",
     "1111111111111111",
     "1122222222222211",
     "1121111111111211",
     "1121111111111211",
     "1121111111111211",
     "1121111111111211",
     "1121111111111211",
     "1121111111111211"),
    # factory (kind 1)
    ("0000000000220022",
     "0000000000220022",
     "0000000000220022",
     "1111111111220022",
     "1111111111220022",
     "1112121211220022",
     "1111111111111122",
     "1112121211111122",
     "1111111111111122",
     "1112121212121122",
     "1111111111111122",
     "1112121212121122",
     "1111111111111122",
     "1112121212121122",
     "1111111111111122",
     "1111111111111122"),
    # fuel dump (kind 2)
    ("0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0001111111111000",
     "0111111111111110",
     "1111112121111111",
     "1111112221111111",
     "1111112121111111",
     "1111112221111111",
     "0111112121111110",
     "0011112221111100",
     "0022002020002200",
     "0022002220002200",
     "0022002020002200"),
    # tank (kind 3)
    ("0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000011111100000",
     "0000011111111111",
     "0000011111100000",
     "1111111111111111",
     "1111111111111111",
     "1222222222222221",
     "2111111111111112",
     "2111111111111112",
     "0222222222222220"),
)

SPRITE_OX = (
    # standing
    ("0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000000000",
     "0000000000300300",
     "0000000000322200",
     "0000000000231330",
     "0002222223223330",
     "0222222223223323",
     "3222222223222330",
     "3222222222330000",
     "3222222222220000",
     "3220220022022000",
     "0220220022022000",
     "0330330033033000"),
)


class Fail(Exception):
    pass


# ---- reading a map -------------------------------------------------------

def load_map(path, max_bytes=DEFAULT_MAX_BYTES):
    """Parse a map file, enforcing what the game's loader enforces.

    The rules are not decoration: everything below is something the drawing
    would otherwise have to guess about, and a preview that draws a broken
    file as if it were a map is worse than one that says no.
    """
    if path == "-":
        text = sys.stdin.read(max_bytes + 1)
    else:
        size = os.path.getsize(path)
        if size > max_bytes:
            raise Fail(f"{size} bytes is more than --max-bytes {max_bytes}")
        with open(path, "rb") as f:
            text = f.read().decode("utf-8", "replace")
    if len(text) > max_bytes:
        raise Fail(f"more than --max-bytes {max_bytes} of input")

    m = {"name": None, "author": "", "seed": 0, "width": MAX_X, "height": MAX_Y,
         "ground": [], "runways": [], "targets": [], "oxen": []}
    header = False

    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        word, _, rest = line.partition(" ")
        rest = rest.strip()

        def fixed(count):
            f = rest.split()
            if len(f) != count:
                raise Fail(f"line {n}: {word} takes {count} numbers, "
                           f"not {len(f)}")
            try:
                return [int(v) for v in f]
            except ValueError:
                raise Fail(f"line {n}: {word} takes numbers")

        if not header:
            if word in (MAP_MAGIC, MAP_MAGIC_WAS) and rest == "1":
                header = True
                continue
            raise Fail(f"line {n}: not a barnstormer map file")

        if word == "name":
            m["name"] = clean(rest, n, "name")
        elif word == "author":
            m["author"] = clean(rest, n, "author")
        elif word == "seed":
            m["seed"] = fixed(1)[0]
        elif word == "size":
            w, h = fixed(2)
            if (w, h) != (MAX_X, MAX_Y):
                raise Fail(f"line {n}: version {MAP_FORMAT_VERSION} maps are "
                           f"{MAX_X} {MAX_Y}, not {w} {h}")
            m["width"], m["height"] = w, h
        elif word == "ground":
            for run in rest.split():
                c, _, h = run.partition(":")
                try:
                    c, h = int(c), int(h)
                except ValueError:
                    raise Fail(f"line {n}: {run!r} is not count:height")
                if c < 1 or not GROUND_MIN <= h <= GROUND_MAX:
                    raise Fail(f"line {n}: {run!r} is not count:height with a "
                               f"height in {GROUND_MIN}..{GROUND_MAX}")
                if len(m["ground"]) + c > MAX_X:
                    raise Fail(f"line {n}: the ground runs past column {MAX_X}")
                m["ground"] += [h] * c
        elif word == "runway":
            x, o = fixed(2)
            if not 0 <= x <= MAX_X - RUNWAY_SPAN or o not in (0, 1):
                raise Fail(f"line {n}: a runway is x 0..{MAX_X - RUNWAY_SPAN} "
                           f"and orientation 0 or 1")
            m["runways"].append((x, o))
        elif word == "target":
            x, k = fixed(2)
            if not 0 <= x <= MAX_X - TARGET_WIDTH or not 0 <= k <= 3:
                raise Fail(f"line {n}: a target is x 0..{MAX_X - TARGET_WIDTH} "
                           f"and kind 0..3")
            m["targets"].append((x, k))
        elif word == "ox":
            x, y = fixed(2)
            if not 0 <= x < MAX_X or not 0 <= y < MAX_Y:
                raise Fail(f"line {n}: an ox stands inside the world")
            m["oxen"].append((x, y))
        # Unknown keys are skipped, as the game's loader skips them: a map
        # written by a later version should still draw.

    if not header:
        raise Fail("empty file: not a barnstormer map")
    if not m["name"]:
        raise Fail("the map has no name")
    if len(m["ground"]) != MAX_X:
        raise Fail(f"the ground covers {len(m['ground'])} columns, not {MAX_X}")
    if not 2 <= len(m["runways"]) <= MAX_RUNWAYS:
        raise Fail(f"{len(m['runways'])} runways; a map has 2 to {MAX_RUNWAYS}")
    if len(m["targets"]) > MAX_TARGETS:
        raise Fail(f"{len(m['targets'])} buildings; a map holds {MAX_TARGETS}")
    if len(m["oxen"]) > MAX_OXEN:
        raise Fail(f"{len(m['oxen'])} oxen; a map holds {MAX_OXEN}")
    return m


def clean(s, line, what):
    """A name goes into the picture, so it is held to the format's limits."""
    s = "".join(c for c in s if c.isprintable())
    if len(s.encode("utf-8")) > NAME_MAX:
        raise Fail(f"line {line}: the {what} is longer than {NAME_MAX} bytes")
    return s


def canonical(m):
    """The bytes map_save() writes, which is what the hash is taken over."""
    out = [f"{MAP_MAGIC} {MAP_FORMAT_VERSION}\n", f"name {m['name']}\n"]
    if m["author"]:
        out.append(f"author {m['author']}\n")
    out.append(f"seed {m['seed']}\n")
    out.append(f"size {m['width']} {m['height']}\n\n")

    g, col, x = m["ground"], 0, 0
    while x < MAX_X:
        h, run = g[x], 1
        while x + run < MAX_X and g[x + run] == h:
            run += 1
        x += run
        if col == 0:
            out.append("ground")
            col = len("ground")
        piece = f" {run}:{h}"
        out.append(piece)
        col += len(piece)
        if col >= GROUND_WRAP:
            out.append("\n")
            col = 0
    if col:
        out.append("\n")

    for key, items, fmt in (("runway", m["runways"], "runway {} {}\n"),
                            ("target", m["targets"], "target {} {}\n"),
                            ("ox", m["oxen"], "ox {} {}\n")):
        if items:
            out.append("\n")
            out += [fmt.format(a, b) for a, b in items]
    return "".join(out).encode("utf-8")


def map_hash(m):
    """FNV-1a over the canonical form: the same value the game prints."""
    h = FNV_OFFSET
    for b in canonical(m):
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


# ---- drawing it ----------------------------------------------------------

def obj_color(v, enemy):
    """obj_color() from render/scene.c: a sprite pixel's palette slot."""
    if v >= 3:
        return HUD                          # PAL_NEUTRAL, shared detail
    if v == 2:
        return TEAM2_ACC if enemy else TEAM1_ACC
    return TEAM2 if enemy else TEAM1


def blit_at(frame, px, py, s, enemy):
    """A sprite with its top-left pixel at device (px, py), s px per pixel.

    Runs of one colour along a row become a single rect, which is the whole
    optimisation: a 16x16 outline sprite is mostly transparent, and twenty
    buildings drawn pixel by pixel would be thousands of elements.
    """
    out = []
    for row, bits in enumerate(frame):
        y = py + row * s
        col = 0
        while col < len(bits):
            v = bits[col]
            run = 1
            while col + run < len(bits) and bits[col + run] == v:
                run += 1
            if v != "0":
                out.append(f'<rect x="{px + col * s}" y="{y}" '
                           f'width="{run * s}" height="{s}" '
                           f'fill="{obj_color(int(v), enemy)}"/>')
            col += run
    return "".join(out)


def pad_height(ground, x):
    """game_pad_height(): the level a building's 16 columns are cut down to."""
    lo = min(ground[max(0, min(MAX_X - 1, i))] for i in range(x, x + TARGET_WIDTH))
    hi = max(ground[max(0, min(MAX_X - 1, i))] for i in range(x, x + TARGET_WIDTH))
    ave = (lo + hi) >> 1
    while ave + 16 >= MAX_Y:
        ave -= 1
    return ave


def bake_terrain(m):
    """The terrain the game will actually build, not the height field.

    init_targets() levels a pad under every building before anything is
    drawn, so a preview of the raw field puts hangars on slopes the player
    will never see.  Buildings are 16 columns apart at least, so no pad can
    disturb another and the order does not matter.
    """
    ground = list(m["ground"])
    pads = {}
    for x, _kind in m["targets"]:
        h = pad_height(m["ground"], x)
        pads[x] = h
        for i in range(x, min(MAX_X, x + TARGET_WIDTH)):
            ground[i] = h
    return ground, pads


def render(m, recipe=None, layout="full", scale=2, label=None):
    """The whole map as one SVG, in the game's palette.

    `recipe` is the text to print under it, `label` the name to print for the
    file it came from.  Nothing here depends on a font metric beyond the
    monospace advance, so the same string lands in the same place wherever it
    is rendered.
    """
    if layout not in ("full", "map"):
        raise Fail(f"{layout!r} is not a layout: full or map")
    if not 1 <= scale <= 8:
        raise Fail(f"--scale is 1 to 8, not {scale}")

    s = scale
    g, pads = bake_terrain(m)
    margin = 20 * s
    mw, mh = MAX_X * s, MAX_Y * s
    ruler_h = 22 * s
    fs = int(9.5 * s)                       # recipe text
    lh = 13 * s
    title_fs = 20 * s
    meta_fs = 11 * s
    lines = recipe.rstrip("\n").split("\n") if recipe else []
    if layout == "full":
        panel_h = 32 * s + (len(lines) + (0 if lines else 1)) * lh + 23 * s
    else:
        panel_h = 0

    w = mw + 2 * margin
    h = margin + mh + (ruler_h + panel_h if layout == "full" else 0) + margin
    top, base = margin, margin + mh

    def wx(x):
        return margin + x * s

    def wy(height):
        return base - height * s

    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
         f'viewBox="0 0 {w} {h}">',
         '<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">'
         f'<stop offset="0" stop-color="{SKY_TOP}"/>'
         f'<stop offset="1" stop-color="{SKY}"/></linearGradient></defs>',
         f'<rect width="{w}" height="{h}" fill="{PAPER}"/>',
         f'<rect x="{margin}" y="{top}" width="{mw}" height="{mh}" '
         f'fill="url(#sky)"/>']

    # One point per column: the height field is the map, and smoothing it
    # would hide exactly the ridge a pilot has to clear.  Two world rows of
    # turf over earth, as draw_ground() does it.
    turf = " ".join(f"{wx(x)},{wy(g[x])}" for x in range(MAX_X))
    earth = " ".join(f"{wx(x)},{wy(g[x] - 2)}" for x in range(MAX_X))
    o.append(f'<polygon points="{wx(0)},{base} {turf} {wx(MAX_X - 1)},{base}" '
             f'fill="{GROUND_EDGE}"/>')
    o.append(f'<polygon points="{wx(0)},{base} {earth} {wx(MAX_X - 1)},{base}" '
             f'fill="{GROUND}"/>')

    for height, lab, col in (
            (FREE_AIR, f"{FREE_AIR}  above this there is no room to turn round",
             "#B08A4A"),
            (WALL, f"{WALL}  above this is a wall, not scenery", "#C0564F")):
        o.append(f'<line x1="{margin}" y1="{wy(height)}" x2="{margin + mw}" '
                 f'y2="{wy(height)}" stroke="{col}" stroke-width="1.5" '
                 f'stroke-dasharray="{7 * s} {6 * s}" opacity="0.7"/>')
        o.append(f'<text x="{wx(160)}" y="{wy(height) - 4 * s}" '
                 f'font-family="{FONT}" font-size="{int(9 * s)}" fill="{col}">'
                 f'{lab}</text>')

    # Both ends of the world are walled off; say so, or the map looks like it
    # has 140 columns of unusable flat at each end for no reason.
    for x0 in (0, MAX_X - EDGE_WIDTH):
        o.append(f'<rect x="{wx(x0)}" y="{top}" width="{EDGE_WIDTH * s}" '
                 f'height="{mh}" fill="#000" opacity="0.30"/>')
    o.append(f'<text x="{wx(4)}" y="{top + 11 * s}" font-family="{FONT}" '
             f'font-size="{int(8.5 * s)}" fill="{HUD_DIM}">edge of the world</text>')
    o.append(f'<text x="{wx(MAX_X - EDGE_WIDTH - 4)}" y="{top + 11 * s}" '
             f'font-family="{FONT}" font-size="{int(8.5 * s)}" fill="{HUD_DIM}" '
             f'text-anchor="end">edge of the world</text>')

    # Airfields: the strip, the run it needs in the direction it faces, and
    # which way that is -- the three things that decide whether it is flyable.
    for rx, orient in sorted(m["runways"]):
        pad = g[rx]
        lo = rx - CORRIDOR if orient else rx + RUNWAY_SPAN
        o.append(f'<rect x="{wx(lo)}" y="{wy(pad) - s}" '
                 f'width="{CORRIDOR * s}" height="{2 * s}" fill="{HUD}" '
                 f'opacity="0.22"/>')
        o.append(f'<rect x="{wx(rx)}" y="{wy(pad) - 3 * s}" '
                 f'width="{RUNWAY_SPAN * s}" height="{4 * s}" fill="{HUD}"/>')
        d = -1 if orient else 1
        ax = wx(rx + (0 if orient else RUNWAY_SPAN)) + d * 5 * s
        o.append(f'<polygon points="{ax + d * 13 * s},{wy(pad) + 5 * s} '
                 f'{ax},{wy(pad) + 2 * s} {ax},{wy(pad) + 8 * s}" '
                 f'fill="{HUD}" opacity="0.85"/>')

    # Buildings and cattle are the game's own sprites, drawn where the game
    # draws them: 16x16 with row 0 at the top, standing on the levelled pad.
    # The player owns exactly the ones at index 7, 8 and 9 (init_targets()).
    for i, (x, kind) in enumerate(m["targets"]):
        o.append(blit_at(SPRITE_TARGET[kind], wx(x), wy(pads[x] + 16), s,
                         not 7 <= i <= 9))
    # The ox is drawn with clr 1, so it wears the player's colours in the
    # game as well as here.
    for x, y in m["oxen"]:
        o.append(blit_at(SPRITE_OX[0], wx(x), wy(y), s, False))

    o.append(f'<rect x="{margin}" y="{top}" width="{mw}" height="{mh}" '
             f'fill="none" stroke="{HUD_DIM}" stroke-width="1.5"/>')

    if layout == "map":
        o.append("</svg>")
        return "\n".join(o)

    # Where things are, in the coordinates the recipe is written in.
    ry = base + 1
    o.append(f'<line x1="{margin}" y1="{ry + 7 * s}" x2="{margin + mw}" '
             f'y2="{ry + 7 * s}" stroke="{HUD_DIM}" stroke-width="1.5"/>')
    for x in range(0, MAX_X + 1, 100):
        big = x % 500 == 0
        xx = wx(min(x, MAX_X - 1))
        o.append(f'<line x1="{xx}" y1="{ry + 7 * s}" x2="{xx}" '
                 f'y2="{ry + (12 if big else 9.5) * s:.0f}" '
                 f'stroke="{HUD if big else HUD_DIM}" '
                 f'stroke-width="{2 if big else 1}"/>')
        if big:
            o.append(f'<text x="{xx}" y="{ry + 21 * s}" font-family="{FONT}" '
                     f'font-size="{int(9 * s)}" fill="{HUD_DIM}" '
                     f'text-anchor="middle">{x}</text>')

    py = base + ruler_h + 4 * s
    o.append(f'<rect x="{margin}" y="{py}" width="{mw}" height="{panel_h - 8 * s}" '
             f'fill="{PANEL}" stroke="{HUD_DIM}" stroke-width="1.5"/>')
    name = m["name"]
    o.append(f'<text x="{margin + 13 * s}" y="{py + 23 * s}" font-family="{FONT}" '
             f'font-size="{title_fs}" fill="{HUD}">{escape(name)}</text>')

    meta = (f"hash [{map_hash(m):08x}]   {len(m['targets'])} buildings, "
            f"{len(m['runways'])} strips, {len(m['oxen'])} oxen")
    if m["author"]:
        meta = f"by {escape(m['author'])}   " + meta
    if label:
        meta += f"   {escape(label)}"
    o.append(f'<text x="{margin + 13 * s + int((len(name) + 2) * title_fs * 0.6)}" '
             f'y="{py + 23 * s}" font-family="{FONT}" font-size="{meta_fs}" '
             f'fill="{WILDLIFE}">{escape(meta)}</text>')

    # The legend draws the artwork itself: a kind is recognisable from its
    # sprite, and no colour key can say which box is a fuel dump.  Each kind
    # carries the number of them on this map, split by owner -- the game
    # gives the player the buildings at index 7, 8 and 9 -- so the picture
    # is its own inventory.
    KIND_NAME = ("hangar", "factory", "fuel dump", "tank")
    mine = [k for i, (_x, k) in enumerate(m["targets"]) if 7 <= i <= 9]
    theirs = [k for i, (_x, k) in enumerate(m["targets"]) if not 7 <= i <= 9]

    # Right to left, because the panel's right edge is the fixed end.  A
    # sprite item carries its kind and its count separately: the count is
    # the number being read off, so it is drawn in the HUD white while the
    # word stays dim.
    items = [("strip", None, "airfield", None),
             ("sprite", (SPRITE_OX[0], False), "ox", len(m["oxen"]))]
    for kinds, enemy, who in ((mine, False, "Player:"),
                              (theirs, True, "Enemy:")):
        for kind in (3, 2, 1, 0):
            n = kinds.count(kind)
            if n:
                items.append(("sprite", (SPRITE_TARGET[kind], enemy),
                              KIND_NAME[kind], n))
        items.append(("label", TEAM2 if enemy else TEAM1, who, None))

    ls = s                                  # the artwork at the map's scale
    fw = 10 * s * 0.605                     # a monospace advance at this size
    lx = margin + mw - 13 * s
    yy = py + 23 * s

    def legend_text(x, text, col):
        o.append(f'<text x="{x}" y="{yy - s}" font-family="{FONT}" '
                 f'font-size="{int(10 * s)}" fill="{col}" '
                 f'text-anchor="end">{text}</text>')
        return x - int(len(text) * fw)

    for what, swatch, lab, count in items:
        if what != "label":
            lx -= TARGET_WIDTH * ls
            if what == "strip":
                o.append(f'<rect x="{lx}" y="{yy - 5 * ls}" '
                         f'width="{TARGET_WIDTH * ls}" height="{2 * ls}" '
                         f'fill="{HUD}"/>')
            else:
                frame, enemy = swatch
                o.append(blit_at(frame, lx, yy - 16 * ls, ls, enemy))
            lx -= 4 * s
        if count is not None:
            lx = legend_text(lx, str(count), HUD) - int(fw)
        lx = legend_text(lx, lab, swatch if what == "label" else HUD_DIM)
        lx -= 13 * s

    ty = py + 32 * s + 12 * s
    if not lines:
        o.append(f'<text x="{margin + 13 * s}" y="{ty}" font-family="{FONT}" '
                 f'font-size="{fs}" fill="{HUD_DIM}">no recipe was supplied '
                 f'for this map</text>')
    for i, ln in enumerate(lines):
        col = HUD_DIM if ln.lstrip().startswith("#") else HUD
        o.append(f'<text x="{margin + 13 * s}" y="{ty + i * lh}" '
                 f'font-family="{FONT}" font-size="{fs}" fill="{col}" '
                 f'xml:space="preserve">{escape(ln)}</text>')

    o.append("</svg>")
    return "\n".join(o)


# ---- png, if something on this machine can do it -------------------------

def rasterise(svg, out):
    """SVG to PNG with whatever is installed; nothing is bundled.

    `out` is None for stdout.  The SVG goes in on stdin so that no temporary
    file is needed and nothing untrusted reaches a shell.
    """
    rsvg = shutil.which("rsvg-convert")
    magick = shutil.which("magick") or shutil.which("convert")
    if rsvg:
        cmd = [rsvg, "--format=png"] + (["-o", out] if out else [])
    elif magick:
        cmd = [magick, "svg:-", out or "png:-"]
    else:
        raise Fail("no rasteriser for --format png: install librsvg "
                   "(rsvg-convert) or ImageMagick, or use --format svg")

    r = subprocess.run(cmd, input=svg.encode("utf-8"),
                       stdout=subprocess.PIPE if not out else None,
                       stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise Fail(f"{os.path.basename(cmd[0])} failed: "
                   f"{r.stderr.decode('utf-8', 'replace').strip()}")
    return r.stdout if not out else None


# ---- main ----------------------------------------------------------------

def recipe_for(map_path, given):
    """The recipe that made a map: the one asked for, or the one beside it."""
    if given:
        with open(given) as f:
            return f.read(), os.path.basename(given)
    beside = os.path.splitext(map_path)[0] + ".recipe"
    if map_path != "-" and os.path.exists(beside):
        with open(beside) as f:
            return f.read(), os.path.basename(beside)
    return None, None


def main():
    ap = argparse.ArgumentParser(
        description="Draw a whole Barnstormer map as one picture.",
        epilog="exit: 0 drawn, 1 bad map, 2 usage, 3 no png rasteriser")
    ap.add_argument("maps", nargs="+", metavar="MAP",
                    help="map files, or - for stdin")
    ap.add_argument("-o", "--out", help="output file, or - for stdout "
                                        "(one map only)")
    ap.add_argument("--out-dir", help="write NAME.svg/.png here "
                                      "(default: beside the map)")
    ap.add_argument("--layout", choices=("full", "map"), default="full",
                    help="full: the world plus name, hash and recipe; "
                         "map: the world on its own (default: full)")
    ap.add_argument("--recipe", help="the recipe to print "
                                     "(default: NAME.recipe beside the map)")
    ap.add_argument("--no-recipe", action="store_true",
                    help="full layout without a recipe: name and hash only")
    ap.add_argument("--scale", type=int, default=2,
                    help="pixels per world column, 1-8 (default: 2)")
    ap.add_argument("--format", choices=("svg", "png"), default="svg",
                    help="png needs rsvg-convert or magick (default: svg)")
    ap.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES,
                    help=f"refuse a bigger map file (default: "
                         f"{DEFAULT_MAX_BYTES})")
    ap.add_argument("--json", action="store_true",
                    help="one JSON object per map on stdout")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--version", action="version",
                    version=f"mapshot.py {VERSION}")
    args = ap.parse_args()

    if args.out and len(args.maps) > 1:
        ap.error("--out takes one map; use --out-dir for several")
    if args.out == "-" and args.json:
        ap.error("--out - and --json both want stdout")

    status = 0
    for path in args.maps:
        record = {"map": path, "ok": False, "version": VERSION,
                  "layout": args.layout, "scale": args.scale}
        try:
            m = load_map(path, args.max_bytes)
            recipe, label = ((None, None)
                             if args.layout == "map" or args.no_recipe
                             else recipe_for(path, args.recipe))
            svg = render(m, recipe, args.layout, args.scale,
                         label or (None if path == "-"
                                   else os.path.basename(path)))

            if args.out == "-":
                out = None
            elif args.out:
                out = args.out
            else:
                stem = "map" if path == "-" else os.path.splitext(
                    os.path.basename(path))[0]
                where = args.out_dir or ("." if path == "-"
                                         else os.path.dirname(path) or ".")
                out = os.path.join(where, f"{stem}.{args.format}")
            if out and args.out_dir:
                os.makedirs(args.out_dir, exist_ok=True)

            if args.format == "png":
                data = rasterise(svg, out)
                if out is None:
                    sys.stdout.buffer.write(data)
            elif out is None:
                sys.stdout.write(svg)
            else:
                with open(out, "w") as f:
                    f.write(svg)

            record.update(ok=True, name=m["name"], author=m["author"] or None,
                          hash=f"{map_hash(m):08x}", format=args.format,
                          image=out or "-",
                          bytes=(os.path.getsize(out) if out else None),
                          recipe=label, buildings=len(m["targets"]),
                          runways=len(m["runways"]), oxen=len(m["oxen"]))
            if args.json:
                print(_json.dumps(record))
            elif not args.quiet and out:
                print(f"{out}: {m['name']} [{map_hash(m):08x}], "
                      f"{args.layout} at {args.scale}x")
        except Fail as e:
            record["error"] = str(e)
            if args.json:
                print(_json.dumps(record))
            print(f"{path}: {e}", file=sys.stderr)
            status = max(status, 3 if "rasteriser" in str(e) else 1)
        except OSError as e:
            record["error"] = str(e)
            if args.json:
                print(_json.dumps(record))
            print(f"{path}: {e}", file=sys.stderr)
            status = max(status, 1)
    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
