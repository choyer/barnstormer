#!/usr/bin/env python3
"""Generate the application icon from the game's own artwork.

Emits an SVG built out of the plane sprite's pixels, so the launcher icon is
the same aeroplane the game draws rather than a separate asset that can drift
out of step with it.

The team argument picks the livery: the windowed game gets the player's
aircraft, the overlay entry gets the opposition's, so the two launcher icons
tell each other apart at a glance.

    python3 tools/make_icon.py origsrc barnstormer.svg          # player
    python3 tools/make_icon.py origsrc barnstormer-overlay.svg enemy
"""

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from extract_sprites import GEOM, parse_arrays, unpack  # noqa: E402

# Matches render/raster.c's palette entries for each side's aircraft.
TEAMS = {
    "player": {1: "#63e6f0", 2: "#2a93a8", 3: "#f2f2f2"},
    "enemy":  {1: "#f25fbe", 2: "#a13279", 3: "#f2f2f2"},
}
SKY = "#0b1220"
PAD = 2       # sprite pixels of margin around the aeroplane


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "origsrc"
    dst = sys.argv[2] if len(sys.argv) > 2 else "barnstormer.svg"
    team = sys.argv[3] if len(sys.argv) > 3 else "player"
    if team not in TEAMS:
        raise SystemExit("team must be one of: " + ", ".join(TEAMS))
    colours = TEAMS[team]

    with open(src + "/SWPLANES.C", errors="replace") as f:
        for name, vals in parse_arrays(f.read()):
            if name != "swplnsym":
                continue
            w, h = GEOM[name]
            px = unpack(vals, w, h)[0]     # frame 0: level flight, facing right
            break
        else:
            raise SystemExit("swplnsym not found in " + src)

    side = w + PAD * 2
    out = [
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
        'shape-rendering="crispEdges">' % (side, side),
        '<rect width="%d" height="%d" rx="3" fill="%s"/>' % (side, side, SKY),
    ]
    for y in range(h):
        for x in range(w):
            v = px[y * w + x]
            if v:
                out.append('<rect x="%d" y="%d" width="1" height="1" fill="%s"/>'
                           % (x + PAD, y + PAD, colours[v]))
    out.append("</svg>")

    with open(dst, "w") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%dx%d sprite pixels, %s livery)" % (dst, w, h, team))


if __name__ == "__main__":
    main()
