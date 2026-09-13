/*
 * paths.h -- where the game keeps things.
 *
 * One place for the XDG rules, so the score file and the level directory
 * cannot disagree about where "the game's data" is.
 */
#ifndef PATHS_H
#define PATHS_H

#include "sopwith.h"

/* $XDG_DATA_HOME/barnstormer, or ~/.local/share/barnstormer when that is
 * unset.  False if neither is usable, which is a reason to do without rather
 * than to fail: nothing here is worth refusing to play over. */
bool sw_data_dir(char *buf, size_t n);

/* The same, with `rel` appended: sw_data_path(b, n, "levels") or
 * sw_data_path(b, n, "scores.tmp"). */
bool sw_data_path(char *buf, size_t n, const char *rel);

/* mkdir -p, ignoring everything that is already there. */
bool sw_make_dirs(const char *path);

/* mkdir -p of everything above `path`, for writing a file into a directory
 * that may not exist yet. */
bool sw_make_parent(const char *path);

#endif /* PATHS_H */
