# Design notes

Everything here is written to be read before changing the code: what the
pieces are, which behaviours are load-bearing, and why the awkward decisions
were made that way.

| File | What is in it |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | tree layout, the `game/` seam and its invariants, the tick-versus-frame timing, front-end states, dependencies, build and test entry points |
| [PLATFORM.md](PLATFORM.md) | the Wayland backend, overlay keyboard grab and recovery, fractional scale, Hyprland and Omarchy integration, where the game's files live |
| [MAP_FORMAT.md](MAP_FORMAT.md) | the on-disk map format, the rules the loader enforces, and the hash |
| [LEADERBOARD.md](LEADERBOARD.md) | boards, ranking, the score file, and the per-map bests |
| [ORIGINAL.md](ORIGINAL.md) | what came from the 1984 sources, what was changed, and the licence position |
| [ROADMAP.md](ROADMAP.md) | the three big features: the map builder, now built, and the two still to come |

## Where things are written down

The root `README.md` is the author's, maintained by hand. It is the
introduction for somebody who has just found the project: what it is, how to
build it, how to play it.

Everything else -- design rationale, invariants that must not be broken,
hard-won findings about the compositor or the desktop, notes for whoever picks
a feature up next -- belongs in this directory, added to the file above whose
subject it shares, or to a new one when the subject is genuinely new.

One directory is not design notes and does not live here: `../agent/` holds
skills -- instructions and tools written to be followed, by a person or by an
agent, rather than read for background. `agent/README.md` says how to consume
one. They carry their own versions and declare which map format and which
game build they need, so they are released independently of the game.
