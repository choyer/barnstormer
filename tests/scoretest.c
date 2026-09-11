/*
 * scoretest.c -- the high score table, headless.
 *
 * Everything runs against a throwaway XDG_DATA_HOME, so the tests never see
 * or touch a real board.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "score.h"

static int failures;
static char sandbox[256];

static void ok(const char *what, bool cond)
{
    printf("%-46s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond)
        failures++;
}

static void scratch(void)
{
    snprintf(sandbox, sizeof(sandbox), "/tmp/barnstormer-scoretest-%d",
             (int)getpid());
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0) { /* first run: nothing to remove */ }
    mkdir(sandbox, 0755);
    setenv("XDG_DATA_HOME", sandbox, 1);
}

static void wipe(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0)
        printf("warning: could not clean %s\n", sandbox);
}

static char *file_path(char *buf, size_t n, const char *suffix)
{
    snprintf(buf, n, "%s/barnstormer/scores%s", sandbox, suffix);
    return buf;
}

static void write_file(const char *suffix, const char *body)
{
    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/barnstormer", sandbox);
    mkdir(dir, 0755);
    FILE *f = fopen(file_path(path, sizeof(path), suffix), "w");
    fputs(body, f);
    fclose(f);
}

int main(void)
{
    scratch();
    scores_t s;

    /* ---- defaults ---- */
    scores_load(&s);
    ok("no file yet: defaults load",
       s.loaded_defaults && s.board[2].e[0].score == 64850);
    ok("defaults are descending", ({
        bool desc = true;
        for (int b = 0; b < SCORE_BOARDS; b++)
            for (int i = 1; i < SCORE_ROWS; i++)
                if (s.board[b].e[i].score > s.board[b].e[i - 1].score)
                    desc = false;
        desc;
    }));
    ok("last name defaults to AAA", !strcmp(s.last_name, "AAA"));

    /* ---- ranking rules ---- */
    ok("a zero score never ranks",
       scores_rank(&s, PLAY_SINGLE, 0) < 0);
    ok("a negative score never ranks",
       scores_rank(&s, PLAY_SINGLE, -450) < 0);
    ok("a score below the tenth does not rank",
       scores_rank(&s, PLAY_SINGLE, 1649) < 0);
    ok("equalling the tenth does not rank",
       scores_rank(&s, PLAY_SINGLE, 1650) < 0);
    ok("beating the tenth ranks last",
       scores_rank(&s, PLAY_SINGLE, 1651) == SCORE_ROWS - 1);
    ok("beating the first ranks first",
       scores_rank(&s, PLAY_SINGLE, 99999) == 0);
    ok("a tie ranks below the incumbent",
       scores_rank(&s, PLAY_SINGLE, 9650) == 3);
    ok("boards are independent",
       scores_rank(&s, PLAY_NOVICE, 8000) == 5 &&
       scores_rank(&s, PLAY_COMPUTER, 8000) == 9);

    /* ---- insertion ---- */
    scores_load(&s);
    int dropped = s.board[1].e[SCORE_ROWS - 1].score;
    int at = scores_insert(&s, PLAY_SINGLE, "ZZZ", 10000);
    ok("insert lands on the ranked row", at == 2);
    ok("inserted entry is present",
       !strcmp(s.board[1].e[2].name, "ZZZ") && s.board[1].e[2].score == 10000);
    ok("the row below shifted down", s.board[1].e[3].score == 9650);
    ok("the tenth dropped off",
       s.board[1].e[SCORE_ROWS - 1].score != dropped);
    ok("still ten rows and descending", ({
        bool good = true;
        for (int i = 1; i < SCORE_ROWS; i++)
            if (s.board[1].e[i].score > s.board[1].e[i - 1].score)
                good = false;
        good;
    }));
    ok("insert remembers the initials", !strcmp(s.last_name, "ZZZ"));
    ok("an unranked insert changes nothing",
       scores_insert(&s, PLAY_SINGLE, "BAD", 5) < 0);

    /* ---- round trip ---- */
    ok("save writes the file", scores_save(&s));
    scores_t back;
    scores_load(&back);
    ok("reload is not defaults", !back.loaded_defaults);
    ok("reload round-trips every board",
       !memcmp(back.board, s.board, sizeof(s.board)));
    ok("reload round-trips the last initials",
       !strcmp(back.last_name, "ZZZ"));

    /* ---- a name containing a space survives whitespace splitting ---- */
    scores_insert(&back, PLAY_NOVICE, "A B", 99999);
    scores_save(&back);
    scores_t spaced;
    scores_load(&spaced);
    ok("a space inside a name round-trips",
       !strcmp(spaced.board[0].e[0].name, "A B"));

    /* ---- corrupt file ---- */
    write_file("", "this is not a score file at all\n");
    scores_load(&s);
    char bak[512];
    ok("a corrupt file falls back to defaults", s.loaded_defaults);
    ok("a corrupt file is moved aside, not overwritten",
       access(file_path(bak, sizeof(bak), ".bak"), F_OK) == 0);

    /* ---- short and junk-laden file ---- */
    write_file("",
               "# barnstormer scores v1\n"
               "LAST BOB\n"
               "SINGLE FOO 5000\n"
               "SINGLE BAR 4000\n"
               "GIBBERISH\n"
               "SINGLE ZZ 10\n"              /* short name: rejected      */
               "SINGLE QQQ notanumber\n"     /* bad score: rejected       */
               "SINGLE NEG -50\n"            /* negative: rejected        */
               "FUTURE MODE 123\n");         /* unknown tag: ignored      */
    scores_load(&s);
    ok("a short board is padded back to ten", ({
        int filled = 0;
        for (int i = 0; i < SCORE_ROWS; i++)
            if (s.board[1].e[i].score > 0)
                filled++;
        filled == SCORE_ROWS;
    }));
    ok("the entries it did have survived",
       s.board[1].e[0].score == 12400 && ({
           bool found = false;
           for (int i = 0; i < SCORE_ROWS; i++)
               if (!strcmp(s.board[1].e[i].name, "FOO"))
                   found = true;
           found;
       }));
    ok("malformed lines are ignored, not fatal", ({
        bool clean = true;
        for (int i = 0; i < SCORE_ROWS; i++)
            if (s.board[1].e[i].score <= 0)
                clean = false;
        clean;
    }));
    ok("untouched boards keep their defaults",
       s.board[2].e[0].score == 64850);
    ok("LAST is read back", !strcmp(s.last_name, "BOB"));

    /* ---- nowhere to write ---- */
    setenv("XDG_DATA_HOME", "/proc/nonexistent/nope", 1);
    ok("an unwritable location fails without crashing", !scores_save(&s));
    scores_load(&s);
    ok("and still yields a usable table", s.board[0].e[0].score == 15225);
    setenv("XDG_DATA_HOME", sandbox, 1);

    /* ---- the entry alphabet ---- */
    ok("lower case folds up", score_char_valid('c') == 'C');
    ok("space is a legal initial", score_char_valid(' ') == ' ');
    ok("digits are not", score_char_valid('7') == 0);
    ok("cycling wraps Z to space", score_char_cycle('Z', 1) == ' ');
    ok("cycling wraps space to A", score_char_cycle(' ', 1) == 'A');
    ok("cycling back from A is space", score_char_cycle('A', -1) == ' ');
    ok("cycling back from space is Z", score_char_cycle(' ', -1) == 'Z');

    wipe();
    printf("\n%s (%d failure%s)\n", failures ? "FAILURES" : "all ok",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
