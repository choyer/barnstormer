/*
 * score.c -- the local high score table.
 *
 * One file holds all three boards, a mode tag first on every line:
 *
 *     # barnstormer scores v1
 *     LAST CRH
 *     COMPUTER DHH 64850
 *
 * The name occupies exactly three columns after the tag, because space is a
 * legal initial and would otherwise be lost to whitespace splitting.
 *
 * Nothing here is allowed to be fatal.  A missing, short, or corrupt file
 * leaves the built-in defaults in place and the game plays on; a file that
 * cannot be parsed is moved aside rather than overwritten, so a bug in this
 * code can never quietly eat somebody's board.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "score.h"

#define SCORE_HEADER "# barnstormer scores v1"

static const char *const board_tag[SCORE_BOARDS] = {
    "NOVICE", "SINGLE", "COMPUTER",
};

/* Seeded from the real ceilings rather than round numbers: a perfect level
 * is 2175 (1800 of enemy buildings plus the 375 clear bonus), so several of
 * these are an exact number of clean levels and mean something to beat.
 * The tenth entry sets the bar for ranking at all, and the three differ on
 * purpose -- novice welcomes a first attempt, the computer board does not. */
static const score_entry_t defaults[SCORE_BOARDS][SCORE_ROWS] = {
    {   /* novice */
        { "DLC", 15225 }, { "CRH", 13050 }, { "PUP", 11300 },
        { "DHH", 10875 }, { "RRH",  8700 }, { "CAM",  7150 },
        { "ACE",  6525 }, { "SKY",  4350 }, { "OWL",  2900 },
        { "PIP",  1225 },
    },
    {   /* single player */
        { "DLC", 12400 }, { "CRH", 10875 }, { "VON",  9650 },
        { "RRH",  8700 }, { "SOP",  7125 }, { "DHH",  6525 },
        { "RED",  5050 }, { "CAM",  4350 }, { "ACE",  2975 },
        { "PUP",  1650 },
    },
    {   /* against the computer */
        { "DHH", 64850 }, { "CRH", 52300 }, { "VON", 43775 },
        { "DLC", 38200 }, { "RED", 31450 }, { "RRH", 26900 },
        { "ACE", 21325 }, { "SOP", 16750 }, { "CAM", 11200 },
        { "TRI",  7650 },
    },
};

int score_board_of(playmode_t mode)
{
    switch (mode) {
    case PLAY_NOVICE:   return 0;
    case PLAY_SINGLE:   return 1;
    case PLAY_COMPUTER: return 2;
    default:            return -1;   /* networked play does not rank */
    }
}

const char *score_board_name(playmode_t mode)
{
    switch (mode) {
    case PLAY_NOVICE:   return "NOVICE";
    case PLAY_SINGLE:   return "SINGLE PLAYER";
    case PLAY_COMPUTER: return "VS COMPUTER";
    default:            return "";
    }
}

char score_char_valid(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || c == ' ')
        return c;
    return 0;
}

/* The alphabet is a ring: space sits below A and above Z. */
char score_char_cycle(char c, int dir)
{
    if (c == ' ')
        return dir > 0 ? 'A' : 'Z';
    if (dir > 0)
        return c >= 'Z' ? ' ' : (char)(c + 1);
    return c <= 'A' ? ' ' : (char)(c - 1);
}

/* ---- paths -------------------------------------------------------------- */

static bool score_dir(char *buf, size_t n)
{
    const char *data = getenv("XDG_DATA_HOME");
    if (data && *data == '/')
        return snprintf(buf, n, "%s/barnstormer", data) < (int)n;

    const char *home = getenv("HOME");
    if (!home || *home != '/')
        return false;
    return snprintf(buf, n, "%s/.local/share/barnstormer", home) < (int)n;
}

static bool score_file(char *buf, size_t n, const char *suffix)
{
    char dir[512];
    if (!score_dir(dir, sizeof(dir)))
        return false;
    return snprintf(buf, n, "%s/scores%s", dir, suffix) < (int)n;
}

/* mkdir -p, ignoring everything that is already there. */
static bool make_dirs(const char *path)
{
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
        return false;

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

/* ---- table shuffling ---------------------------------------------------- */

static void set_name(char *dst, const char *src)
{
    for (int i = 0; i < SCORE_NAME_LEN; i++) {
        char c = src[i] ? score_char_valid(src[i]) : ' ';
        dst[i] = c ? c : ' ';
    }
    dst[SCORE_NAME_LEN] = '\0';
}

static void sort_board(score_table_t *t, int n)
{
    /* Insertion sort, stable, so entries that arrived earlier keep the
     * higher slot on a tie -- the same rule scores_rank() applies. */
    for (int i = 1; i < n; i++) {
        score_entry_t key = t->e[i];
        int j = i - 1;
        while (j >= 0 && t->e[j].score < key.score) {
            t->e[j + 1] = t->e[j];
            j--;
        }
        t->e[j + 1] = key;
    }
}

/* Top up a short board from the defaults, skipping rows it already has. */
static void pad_board(score_table_t *t, int have, int b)
{
    for (int i = 0; i < SCORE_ROWS && have < SCORE_ROWS; i++) {
        const score_entry_t *d = &defaults[b][i];
        bool dup = false;
        for (int j = 0; j < have; j++)
            if (t->e[j].score == d->score && !strcmp(t->e[j].name, d->name))
                dup = true;
        if (!dup)
            t->e[have++] = *d;
    }
    while (have < SCORE_ROWS)
        t->e[have++] = defaults[b][SCORE_ROWS - 1];
    sort_board(t, SCORE_ROWS);
}

int scores_rank(const scores_t *s, playmode_t mode, int score)
{
    int b = score_board_of(mode);
    if (b < 0 || score <= 0)
        return -1;
    if (score <= s->board[b].e[SCORE_ROWS - 1].score)
        return -1;

    for (int i = 0; i < SCORE_ROWS; i++)
        if (score > s->board[b].e[i].score)
            return i;
    return -1;
}

int scores_insert(scores_t *s, playmode_t mode, const char *name, int score)
{
    int at = scores_rank(s, mode, score);
    if (at < 0)
        return -1;

    score_table_t *t = &s->board[score_board_of(mode)];
    for (int i = SCORE_ROWS - 1; i > at; i--)
        t->e[i] = t->e[i - 1];
    set_name(t->e[at].name, name);
    t->e[at].score = score;

    set_name(s->last_name, name);
    return at;
}

/* ---- persistence -------------------------------------------------------- */

static void load_defaults(scores_t *s)
{
    memcpy(s->board, defaults, sizeof(s->board));
    set_name(s->last_name, "AAA");
    s->loaded_defaults = true;
}

/* "COMPUTER DHH 64850" -- the name is three columns wide, spaces included. */
static bool parse_entry(const char *line, int *board, char *name, int *score)
{
    for (int b = 0; b < SCORE_BOARDS; b++) {
        size_t len = strlen(board_tag[b]);
        if (strncmp(line, board_tag[b], len) || line[len] != ' ')
            continue;

        const char *p = line + len + 1;
        char n[SCORE_NAME_LEN + 1];
        for (int i = 0; i < SCORE_NAME_LEN; i++) {
            if (!p[i] || !score_char_valid(p[i]))
                return false;
            n[i] = score_char_valid(p[i]);
        }
        n[SCORE_NAME_LEN] = '\0';

        if (p[SCORE_NAME_LEN] != ' ')
            return false;
        char *end = NULL;
        long v = strtol(p + SCORE_NAME_LEN + 1, &end, 10);
        if (end == p + SCORE_NAME_LEN + 1 || v <= 0 || v > 1000000000L)
            return false;

        *board = b;
        *score = (int)v;
        memcpy(name, n, sizeof(n));
        return true;
    }
    return false;
}

void scores_load(scores_t *s)
{
    memset(s, 0, sizeof(*s));
    load_defaults(s);

    char path[600];
    if (!score_file(path, sizeof(path), ""))
        return;

    FILE *f = fopen(path, "r");
    if (!f)
        return;                     /* nothing saved yet: defaults stand   */

    char line[256];
    if (!fgets(line, sizeof(line), f) ||
        strncmp(line, "# barnstormer scores", 20)) {
        /* Not ours, or damaged.  Move it aside rather than overwrite it. */
        fclose(f);
        char bak[620];
        if (score_file(bak, sizeof(bak), ".bak"))
            rename(path, bak);
        return;
    }

    score_table_t got[SCORE_BOARDS];
    int n[SCORE_BOARDS] = { 0, 0, 0 };
    memset(got, 0, sizeof(got));

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;

        if (!strncmp(line, "LAST ", 5)) {
            set_name(s->last_name, line + 5);
            continue;
        }

        int b, sc;
        char name[SCORE_NAME_LEN + 1];
        if (!parse_entry(line, &b, name, &sc))
            continue;               /* unknown line: ignore, do not fail   */
        if (n[b] < SCORE_ROWS) {
            set_name(got[b].e[n[b]].name, name);
            got[b].e[n[b]].score = sc;
            n[b]++;
        }
    }
    fclose(f);

    for (int b = 0; b < SCORE_BOARDS; b++) {
        if (n[b] == 0)
            continue;               /* keep the defaults for this board    */
        sort_board(&got[b], n[b]);
        pad_board(&got[b], n[b], b);
        s->board[b] = got[b];
        s->loaded_defaults = false;
    }
}

bool scores_save(const scores_t *s)
{
    char dir[512], path[600], tmp[620];
    if (!score_dir(dir, sizeof(dir)) || !make_dirs(dir) ||
        !score_file(path, sizeof(path), "") ||
        !score_file(tmp, sizeof(tmp), ".tmp"))
        return false;

    FILE *f = fopen(tmp, "w");
    if (!f)
        return false;

    fprintf(f, "%s\n", SCORE_HEADER);
    fprintf(f, "LAST %s\n", s->last_name);
    for (int b = 0; b < SCORE_BOARDS; b++)
        for (int i = 0; i < SCORE_ROWS; i++)
            fprintf(f, "%s %s %d\n", board_tag[b],
                    s->board[b].e[i].name, s->board[b].e[i].score);

    if (fflush(f) != 0 || ferror(f)) {
        fclose(f);
        unlink(tmp);
        return false;
    }
    fclose(f);

    /* Rename last, so an interrupted write cannot truncate the real file. */
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}
