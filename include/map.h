/*
 * map.h -- world description.
 *
 * A map is the terrain height field plus the placement of runways,
 * buildings, cattle and bird flocks.  The running game never reads the
 * built-in tables directly: game_start() takes a map_t, so a map editor
 * (doc/ROADMAP.md) only has to produce one of these structures.
 *
 * The on-disk format is specified in doc/MAP_FORMAT.md and implemented in
 * game/map.c.
 */
#ifndef MAP_H
#define MAP_H

#include "sopwith.h"

#define MAP_FORMAT_VERSION 1

/* Building types, matching the four original silhouettes. */
enum {
    TARGET_HANGAR  = 0,   /* flagged shed with the open front            */
    TARGET_FACTORY = 1,   /* windowed block with twin chimneys            */
    TARGET_FUEL    = 2,   /* fuel dump -- worth 200 instead of 100        */
    TARGET_TANK    = 3,   /* turret and a gun over tracks                 */
};

/* Limits the loader enforces.  They are the shapes the simulation already
 * assumes, not taste: the spawn tables index eight runway slots, game_t has
 * room for MAX_TARG buildings and MAX_OXEN cattle, ground below 26 shows
 * through the instrument band, and a building is 16 columns wide standing on
 * a pad the game levels under it. */
#define MAP_MAX_RUNWAYS     8
#define MAP_MIN_RUNWAYS     2
#define MAP_GROUND_MIN      26
#define MAP_GROUND_MAX      (MAX_Y - 1)
#define MAP_RUNWAY_SPAN     21        /* columns an aircraft rests on     */
#define MAP_RUNWAY_SLOP     4         /* how uneven that strip may be     */
#define MAP_TARGET_WIDTH    16
#define MAP_NAME_MAX        63        /* bytes, name and author alike     */

typedef struct { uint16_t x; uint8_t kind; } map_target_t;
typedef struct { uint16_t x; uint8_t orient; } map_runway_t;
typedef struct { uint16_t x, y; } map_point_t;

typedef struct {
    const char *name;
    const char *author;             /* may be NULL; not written if unset  */
    uint32_t format;
    uint16_t width, height;
    uint32_t rand_seed;

    const uint8_t *ground;          /* width entries, height above bottom */

    const map_runway_t *runways;    /* slot order matches the original    */
    int n_runways;
    const map_target_t *targets;
    int n_targets;
    const map_point_t *oxen;
    int n_oxen;
} map_t;

extern const map_t map_classic;

/* Which runway slot a given player index uses, per play mode.  The original
 * indexed three different tables here (inits/initc/initm in SWINIT.C). */
int map_runway_slot(playmode_t mode, int player_index);

/* ---- persistence (doc/MAP_FORMAT.md) ----------------------------------- */

/* All three return 0 on success and -1 with errno set on failure.
 *
 * map_load() allocates the map and everything it points at as one block;
 * pass the result to map_free() and to nothing else.  A map that breaks
 * one of the format's rules is refused with errno EINVAL rather than loaded
 * half-valid.  map_free() refuses a pointer it did not hand out, so a
 * built-in map like map_classic cannot be freed by accident.
 *
 * map_save() writes the canonical form -- no comments, terrain wrapped at a
 * fixed width -- through a temporary file, so an interrupted write cannot
 * truncate an existing map.  It refuses to write a map that would not
 * load back.
 *
 * None of this is thread-safe; the game is single-threaded. */
int map_load(const char *path, map_t **out);
int map_free(map_t *mp);
int map_save(const char *path, const map_t *mp);

/* ---- the map directory ------------------------------------------------- */

#define MAP_LIST_MAX 64

typedef struct {
    char path[512];
    char name[MAP_NAME_MAX + 1];
    char author[MAP_NAME_MAX + 1];
    uint32_t hash;                  /* map_hash(), the map's identity     */
} map_info_t;

/* Where user maps live: $XDG_DATA_HOME/barnstormer/maps, or
 * ~/.local/share/barnstormer/maps.  False if neither can be worked out. */
bool map_dir(char *buf, size_t n);

/* Every *.map in there that loads, by name.  Returns how many were written to
 * `out`; `skipped`, if given, counts the files that are there and will not
 * load -- worth saying out loud, since the alternative is a map silently
 * missing from the list. */
int map_list(map_info_t *out, int max, int *skipped);

/* Whether a map in memory obeys the format's rules -- 0 if it does, -1 with
 * errno EINVAL and map_error() set if it does not.  map_save() applies
 * this itself; the editor uses it to check a change before keeping it. */
int map_check(const map_t *mp);

/* FNV-1a over the canonical serialisation -- the exact bytes map_save()
 * writes -- so two people can check they are holding the same map however
 * their copies are laid out, and networked peers can refuse to start if they
 * are not.  Returns 0 for a map that does not pass map_check(), which
 * means "no hash to compare" rather than any particular map. */
uint32_t map_hash(const map_t *mp);

/* Why the last map_load(), map_save() or map_check() failed, in a form worth showing
 * to whoever is editing the file: "line 12: height 210 is outside 26..199".
 * Empty until one of them fails, and valid until the next call. */
const char *map_error(void);

#endif /* MAP_H */
