/*
 * level.h -- world description.
 *
 * A level is the terrain height field plus the placement of runways,
 * buildings, cattle and bird flocks.  The running game never reads the
 * built-in tables directly: game_start() takes a level_t, so a level editor
 * (doc/ROADMAP.md) only has to produce one of these structures.
 *
 * The on-disk format is specified in doc/LEVEL_FORMAT.md and implemented in
 * game/level.c.
 */
#ifndef LEVEL_H
#define LEVEL_H

#include "sopwith.h"

#define LEVEL_FORMAT_VERSION 1

/* Building types, matching the four original silhouettes. */
enum {
    TARGET_HOUSE   = 0,   /* pitched roof with a flagpole                 */
    TARGET_FACTORY = 1,   /* windowed block with twin chimneys            */
    TARGET_FUEL    = 2,   /* fuel dump -- worth 200 instead of 100        */
    TARGET_HANGAR  = 3,   /* wide shed                                    */
};

/* Limits the loader enforces.  They are the shapes the simulation already
 * assumes, not taste: the spawn tables index eight runway slots, game_t has
 * room for MAX_TARG buildings and MAX_OXEN cattle, ground below 26 shows
 * through the instrument band, and a building is 16 columns wide standing on
 * a pad the game levels under it. */
#define LEVEL_MAX_RUNWAYS   8
#define LEVEL_MIN_RUNWAYS   2
#define LEVEL_GROUND_MIN    26
#define LEVEL_GROUND_MAX    (MAX_Y - 1)
#define LEVEL_RUNWAY_SPAN   21          /* columns an aircraft rests on   */
#define LEVEL_RUNWAY_SLOP   4           /* how uneven that strip may be   */
#define LEVEL_TARGET_WIDTH  16
#define LEVEL_NAME_MAX      63          /* bytes, name and author alike   */

typedef struct { uint16_t x; uint8_t kind; } level_target_t;
typedef struct { uint16_t x; uint8_t orient; } level_runway_t;
typedef struct { uint16_t x, y; } level_point_t;

typedef struct {
    const char *name;
    const char *author;             /* may be NULL; not written if unset  */
    uint32_t format;
    uint16_t width, height;
    uint32_t rand_seed;

    const uint8_t *ground;          /* width entries, height above bottom */

    const level_runway_t *runways;  /* slot order matches the original    */
    int n_runways;
    const level_target_t *targets;
    int n_targets;
    const level_point_t *oxen;
    int n_oxen;
} level_t;

extern const level_t level_classic;

/* Which runway slot a given player index uses, per play mode.  The original
 * indexed three different tables here (inits/initc/initm in SWINIT.C). */
int level_runway_slot(playmode_t mode, int player_index);

/* ---- persistence (doc/LEVEL_FORMAT.md) --------------------------------- */

/* All three return 0 on success and -1 with errno set on failure.
 *
 * level_load() allocates the level and everything it points at as one block;
 * pass the result to level_free() and to nothing else.  A level that breaks
 * one of the format's rules is refused with errno EINVAL rather than loaded
 * half-valid.  level_free() refuses a pointer it did not hand out, so a
 * built-in level like level_classic cannot be freed by accident.
 *
 * level_save() writes the canonical form -- no comments, terrain wrapped at a
 * fixed width -- through a temporary file, so an interrupted write cannot
 * truncate an existing level.  It refuses to write a level that would not
 * load back.
 *
 * None of this is thread-safe; the game is single-threaded. */
int level_load(const char *path, level_t **out);
int level_free(level_t *lvl);
int level_save(const char *path, const level_t *lvl);

/* ---- the level directory ----------------------------------------------- */

#define LEVEL_LIST_MAX 64

typedef struct {
    char path[512];
    char name[LEVEL_NAME_MAX + 1];
    char author[LEVEL_NAME_MAX + 1];
} level_info_t;

/* Where user levels live: $XDG_DATA_HOME/barnstormer/levels, or
 * ~/.local/share/barnstormer/levels.  False if neither can be worked out. */
bool level_dir(char *buf, size_t n);

/* Every *.lvl in there that loads, by name.  Returns how many were written to
 * `out`; `skipped`, if given, counts the files that are there and will not
 * load -- worth saying out loud, since the alternative is a level silently
 * missing from the list. */
int level_list(level_info_t *out, int max, int *skipped);

/* Whether a level in memory obeys the format's rules -- 0 if it does, -1 with
 * errno EINVAL and level_error() set if it does not.  level_save() applies
 * this itself; the editor uses it to check a change before keeping it. */
int level_check(const level_t *lvl);

/* FNV-1a over the canonical serialisation -- the exact bytes level_save()
 * writes -- so two people can check they are holding the same level however
 * their copies are laid out, and networked peers can refuse to start if they
 * are not.  Returns 0 for a level that does not pass level_check(), which
 * means "no hash to compare" rather than any particular level. */
uint32_t level_hash(const level_t *lvl);

/* Why the last level_load(), level_save() or level_check() failed, in a form worth showing
 * to whoever is editing the file: "line 12: height 210 is outside 26..199".
 * Empty until one of them fails, and valid until the next call. */
const char *level_error(void);

#endif /* LEVEL_H */
