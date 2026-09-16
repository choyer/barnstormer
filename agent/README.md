# Agent skills

Instructions written for whoever or whatever is doing a job on this project,
kept deliberately free of any one agent's or harness's conventions. A human can
follow them by reading; a program can follow them by reading `manifest.json`
and running what it names.

| Skill | What it does |
|---|---|
| [barnstormer-map-design](barnstormer-map-design/SKILL.md) | design and generate `.map` files from a text recipe, and check that the result can actually be flown |

## Consuming one

Each skill directory is self-contained and relocatable:

```
barnstormer-map-design/
  SKILL.md        the instructions -- the entry point for a person or an agent
  manifest.json   the same thing machine-readable: tools, arguments, exit
                  codes, requirements, what it produces
  references/     the background the instructions tell you to read
  scripts/        the tools, no dependencies beyond python3 and bash
  examples/       input that is known to work
```

Three ways in, in increasing order of automation:

1. **Read `SKILL.md`.** It is the complete instruction set, with the shell
   commands to run.
2. **Point a harness at the directory.** Any system with a notion of a skill,
   a prompt fragment or a tool bundle can take `SKILL.md` as the text and
   `scripts/` as the tools. The YAML front matter carries `id`, `version`,
   `description` and `entrypoint` for systems that index on those.
3. **Read `manifest.json` and drive the tools directly.** Every tool takes
   `--json` and returns documented exit codes, so nothing has to parse prose.

Binaries the tools need are found through environment variables first
(`$BARNSTORMER`, `$FLYTEST`), then the repository's own `build/`, then `PATH`.
Nothing is installed, and nothing is written except the file you asked for,
so a skill can be copied out of this repository and pointed at a game built
somewhere else.

## Versioning

Skills carry their own `version` and are released independently of the game.
What ties a skill to the game is not a version number but the interfaces it
declares in `manifest.json`: the map format version it writes, and the
minimum game build it needs. A skill improving its own judgement is a bump
here and nothing in the game; a change to a format is a bump on both sides.

## Adding one

Same shape as above: a directory named after the skill, `SKILL.md` as the
entry point, `manifest.json` beside it, tools under `scripts/` that work when
invoked from anywhere. Keep the harness-specific parts out -- if an
instruction only makes sense inside one agent's tooling, it belongs in that
tooling's configuration, not here.
