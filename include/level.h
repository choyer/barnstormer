/*
 * level.h -- world description.
 *
 * A level is the terrain height field plus the placement of runways,
 * buildings, cattle and bird flocks.  The running game never reads the
 * built-in tables directly: game_start() takes a level_t, so a level editor
 * (doc/ROADMAP.md) only has to produce one of these structures.
 *
 * The on-disk format is specified in doc/LEVEL_FORMAT.md.  Loading and saving
 * are deliberately not implemented yet -- the stubs below exist so callers can
 * be written against the final API.
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

typedef struct { uint16_t x; uint8_t kind; } level_target_t;
typedef struct { uint16_t x; uint8_t orient; } level_runway_t;
typedef struct { uint16_t x, y; } level_point_t;

typedef struct {
    const char *name;
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

/* ---- persistence (planned, see doc/LEVEL_FORMAT.md) -------------------- */

/* Both return 0 on success and -1 with errno set on failure.  Neither is
 * implemented yet; they currently fail with ENOSYS. */
int level_load(const char *path, level_t **out);
int level_free(level_t *lvl);
int level_save(const char *path, const level_t *lvl);

#endif /* LEVEL_H */
