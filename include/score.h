/*
 * score.h -- the local high score table.
 *
 * Three boards, one per play mode, ten entries each, three initials and a
 * score.  Kept deliberately outside the simulation: game_tick() has a
 * deterministic replay test pinned to a hash, and file I/O has no business
 * anywhere near it.
 */
#ifndef SCORE_H
#define SCORE_H

#include <stdbool.h>

#include "sopwith.h"

#define SCORE_NAME_LEN  3
#define SCORE_ROWS     10
#define SCORE_BOARDS    3       /* novice, single, computer               */

typedef struct {
    char name[SCORE_NAME_LEN + 1];
    int  score;
    /* Set on an entry the board shows with a mark beside it.  Nothing
     * written today sets it; entries saved by a build that did keep theirs,
     * which is why the loader and the board still know about it. */
    bool marked;
} score_entry_t;

typedef struct {
    score_entry_t e[SCORE_ROWS];
} score_table_t;

/* A personal best per level, keyed by level_hash() -- the identity the file
 * format guarantees (doc/LEVEL_FORMAT.md), so two copies of a level share a
 * best and an edited level is a different level.  Authored levels never
 * reach the boards, since a board is a comparison between runs on one fixed
 * world; this is the player against themselves on a world of their own.
 *
 * Oldest first: a new level past the end of a full table evicts the least
 * recently beaten, which is also the order the file is written in. */
#define SCORE_BESTS 64

typedef struct {
    uint32_t level;                      /* level_hash()                  */
    int      score;
} score_best_t;

typedef struct {
    score_table_t board[SCORE_BOARDS];
    score_best_t best[SCORE_BESTS];
    int  n_best;
    char last_name[SCORE_NAME_LEN + 1];  /* pre-filled on the next entry  */
    bool dials;                          /* the throttle/airspeed strip   */
    bool loaded_defaults;                /* nothing on disk yet           */
} scores_t;

/* Which board a mode plays for; -1 for modes that do not rank. */
int  score_board_of(playmode_t mode);

/* Human-readable board name, for the heading. */
const char *score_board_name(playmode_t mode);

/* Read the table, falling back to the built-in defaults when the file is
 * missing, short or unreadable.  Always leaves `s` usable. */
void scores_load(scores_t *s);

/* Where `score` would land on `mode`'s board, or -1 if it does not rank.
 * A score must be above zero and must beat the tenth entry; ties rank below
 * the entry already holding the slot. */
int  scores_rank(const scores_t *s, playmode_t mode, int score);

/* Slot an entry in, dropping the tenth.  Returns the row it landed on, or
 * -1 if it did not rank after all. */
int  scores_insert(scores_t *s, playmode_t mode, const char *name, int score);

/* The best score recorded on the level with this hash, or 0 if there is no
 * record of it.  A hash of 0 means "no hash to compare" and never matches. */
int  scores_best(const scores_t *s, uint32_t level);

/* Record `score` against a level if it beats what is there.  True when it
 * did, so the caller can say so on screen. */
bool scores_best_set(scores_t *s, uint32_t level, int score);

/* Write the table out.  False means the score could not be saved, which the
 * caller should say on screen rather than treat as fatal. */
bool scores_save(const scores_t *s);

/* Fold a character into the entry alphabet: A-Z and space, or 0 if it is
 * not one of them. */
char score_char_valid(char c);

/* Step a name cell through the alphabet, `dir` being +1 or -1. */
char score_char_cycle(char c, int dir);

#endif /* SCORE_H */
