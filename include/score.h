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
} score_entry_t;

typedef struct {
    score_entry_t e[SCORE_ROWS];
} score_table_t;

typedef struct {
    score_table_t board[SCORE_BOARDS];
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

/* Write the table out.  False means the score could not be saved, which the
 * caller should say on screen rather than treat as fatal. */
bool scores_save(const scores_t *s);

/* Fold a character into the entry alphabet: A-Z and space, or 0 if it is
 * not one of them. */
char score_char_valid(char c);

/* Step a name cell through the alphabet, `dir` being +1 or -1. */
char score_char_cycle(char c, int dir);

#endif /* SCORE_H */
