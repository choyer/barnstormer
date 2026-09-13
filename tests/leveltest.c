/*
 * leveltest.c -- the level file format, headless.
 *
 * Everything is written to a throwaway directory under /tmp, so the tests
 * never touch a real level.  The round trip is checked against the classic
 * level rather than a toy one: whatever the format cannot carry, it cannot
 * carry for the level the whole game is balanced around.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "level.h"

static int failures;
static char sandbox[256];

static void ok(const char *what, bool cond)
{
    printf("%-56s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond)
        failures++;
}

/* A rejection is only useful if it says what is wrong and where. */
static void rejects(const char *what, const char *body, const char *expect)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/bad.lvl", sandbox);
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);

    level_t *lv = NULL;
    errno = 0;
    int rc = level_load(path, &lv);
    bool good = rc < 0 && errno == EINVAL && lv == NULL &&
                strstr(level_error(), expect) != NULL;
    ok(what, good);
    if (!good)
        printf("    wanted \"%s\", got \"%s\"\n", expect, level_error());
    if (rc == 0)
        level_free(lv);
}

/* The other half: a level that sits just inside the rules must still load. */
static void accepts(const char *what, const char *body)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/good.lvl", sandbox);
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);

    level_t *lv = NULL;
    int rc = level_load(path, &lv);
    ok(what, rc == 0);
    if (rc < 0)
        printf("    rejected: %s\n", level_error());
    else
        level_free(lv);
}

static char *path_in(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", sandbox, name);
    return buf;
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap);
    size_t got;
    while ((got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (n + 1 >= cap)
            buf = realloc(buf, cap *= 2);
    }
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

/* The smallest level that loads: flat ground, two runways on it. */
#define VALID \
    "barnstormer-level 1\n"  /* 1 */ \
    "name Test\n"            /* 2 */ \
    "seed 7491\n"            /* 3 */ \
    "size 3000 200\n"        /* 4 */ \
    "ground 3000:100\n"      /* 5 */ \
    "runway 100 0\n"         /* 6 */ \
    "runway 200 1\n"         /* 7 */

/* VALID with one line replaced, so a test can name the line it broke. */
static const char *variant(int lineno, const char *replacement)
{
    static char out[8192];
    char src[] = VALID;
    char *p = out;
    int n = 0;

    for (char *line = strtok(src, "\n"); line; line = strtok(NULL, "\n")) {
        n++;
        const char *use = n == lineno ? replacement : line;
        p += snprintf(p, sizeof(out) - (size_t)(p - out), "%s\n", use);
    }
    return out;
}

int main(void)
{
    snprintf(sandbox, sizeof(sandbox), "/tmp/barnstormer-leveltest-%d",
             (int)getpid());
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0) { /* first run: nothing to remove */ }
    mkdir(sandbox, 0755);

    char path[512], other[512];
    path_in(path, sizeof(path), "classic.lvl");

    /* ---- the classic level survives a round trip ---- */

    ok("the classic level saves", level_save(path, &level_classic) == 0);

    level_t *lv = NULL;
    ok("and loads back", level_load(path, &lv) == 0 && lv != NULL);

    if (lv) {
        ok("name survives", !strcmp(lv->name, level_classic.name));
        ok("seed survives", lv->rand_seed == level_classic.rand_seed);
        ok("size survives",
           lv->width == level_classic.width &&
           lv->height == level_classic.height);
        ok("every one of the 3000 columns survives",
           !memcmp(lv->ground, level_classic.ground, MAX_X));
        ok("all eight runways survive, in order",
           lv->n_runways == level_classic.n_runways &&
           !memcmp(lv->runways, level_classic.runways,
                   sizeof(level_runway_t) * (size_t)lv->n_runways));
        ok("all twenty buildings survive, in order",
           lv->n_targets == level_classic.n_targets &&
           !memcmp(lv->targets, level_classic.targets,
                   sizeof(level_target_t) * (size_t)lv->n_targets));
        ok("both oxen survive",
           lv->n_oxen == level_classic.n_oxen &&
           !memcmp(lv->oxen, level_classic.oxen,
                   sizeof(level_point_t) * (size_t)lv->n_oxen));

        /* ---- and the serialisation is canonical ---- */
        path_in(other, sizeof(other), "classic2.lvl");
        ok("the reloaded level saves again", level_save(other, lv) == 0);

        size_t n1 = 0, n2 = 0;
        char *a = slurp(path, &n1), *b = slurp(other, &n2);
        ok("byte for byte the same file", a && b && n1 == n2 && !strcmp(a, b));
        ok("the whole classic level fits in 8 KB of text", n1 < 8192);
        free(a);
        free(b);

        ok("freeing it succeeds", level_free(lv) == 0);
        ok("freeing it twice does not", level_free(lv) < 0 && errno == EINVAL);
    }

    ok("a built-in level cannot be freed",
       level_free((level_t *)&level_classic) < 0 && errno == EINVAL);

    /* ---- what a hand-edited file may contain ---- */

    path_in(path, sizeof(path), "hand.lvl");
    FILE *f = fopen(path, "w");
    fputs("# a level someone typed\n"
          "barnstormer-level 1\n"
          "\n"
          "name  Bridge Too Far  \n"
          "author carl\n"
          "seed 12345\n"
          "size 3000 200\n"
          "wingspan 12\n"                 /* unknown key: skipped */
          "ground 1500:100\n"
          "   ground   1500:100\n"        /* leading space, run-on line */
          "runway 100 0\n"
          "runway 2000 1\n"
          "target 500 2\n"
          "ox 700 80\n", f);
    fclose(f);

    lv = NULL;
    ok("comments, blank lines and unknown keys are fine",
       level_load(path, &lv) == 0 && lv != NULL);
    if (lv) {
        ok("the name is trimmed, inner spaces kept",
           !strcmp(lv->name, "Bridge Too Far"));
        ok("the author is read", lv->author && !strcmp(lv->author, "carl"));
        ok("ground lines concatenate", lv->ground[0] == 100 &&
           lv->ground[2999] == 100);
        ok("the seed is read", lv->rand_seed == 12345);
        ok("one building, one ox", lv->n_targets == 1 && lv->n_oxen == 1);

        path_in(other, sizeof(other), "hand2.lvl");
        ok("it saves with the author intact", level_save(other, lv) == 0);
        char *txt = slurp(other, NULL);
        ok("which is on its own line", txt && strstr(txt, "\nauthor carl\n"));
        ok("and no comments are written", txt && !strchr(txt, '#'));
        free(txt);
        level_free(lv);
    }

    /* ---- what it may not ---- */

    rejects("a file that is not a level at all",
            "hello\n", "not a barnstormer level file");
    rejects("an empty file",
            "", "not a barnstormer level file");
    rejects("a version from the future",
            variant(1, "barnstormer-level 2"), "format version 2");
    rejects("a header with no version",
            variant(1, "barnstormer-level"), "no version number");
    rejects("an empty name",
            variant(2, "name   "), "line 2: name is empty");
    rejects("a name that will not fit",
            variant(2, "name 0123456789012345678901234567890123456789"
                       "012345678901234567890123456789"),
            "line 2: name is longer than 63 bytes");
    rejects("a world of another size",
            variant(4, "size 2000 200"), "line 4: size must be 3000 200");
    rejects("a file with no size line",
            "barnstormer-level 1\nname Test\nground 3000:100\n"
            "runway 100 0\nrunway 200 1\n", "no size line");
    rejects("ground below the instrument band",
            variant(5, "ground 3000:25"), "line 5: \"3000:25\" is not");
    rejects("ground above the world",
            variant(5, "ground 3000:200"), "line 5: \"3000:200\" is not");
    rejects("a run that is not count:height",
            variant(5, "ground 3000-100"), "line 5: \"3000-100\" is not");
    rejects("terrain with a hole in it",
            variant(5, "ground 2999:100"), "covers 2999 of the 3000");
    rejects("terrain that overruns the world",
            variant(5, "ground 3000:100 1:100"), "longer than 3000 columns");
    rejects("a ground line with nothing on it",
            variant(5, "ground"), "no runs on it");
    rejects("a single runway",
            "barnstormer-level 1\nname Test\nsize 3000 200\n"
            "ground 3000:100\nrunway 100 0\n", "at least 2 runways");
    rejects("a runway facing sideways",
            variant(6, "runway 100 2"), "line 6: a runway needs");
    rejects("a runway off the end of the world",
            variant(6, "runway 2990 0"), "line 6: the runway at 2990 runs off");
    rejects("a runway on a slope",
            variant(5, "ground 110:100 100:26 2790:100"),
            "line 6: the runway at 100 is not flat");
    rejects("a building off the end of the world",
            VALID "target 2990 0\n", "line 8: the building at 2990 runs off");
    rejects("a building of no known kind",
            VALID "target 500 4\n", "line 8: a building needs");
    rejects("two buildings on the same ground",
            VALID "target 500 0\ntarget 510 1\n",
            "line 9: the buildings at 500 and 510 are less than 16");
    rejects("a building across a landing strip",
            VALID "target 110 0\n",
            "line 8: the building at 110 stands on the runway at 100");
    rejects("a building overlapping a strip by one column",
            VALID "target 85 0\n",
            "line 8: the building at 85 stands on the runway at 100");
    accepts("a building one column clear of a strip either side",
            VALID "target 84 0\ntarget 121 1\n");
    rejects("an ox outside the world",
            VALID "ox 100 250\n", "line 8: the ox at 100,250 is outside");
    rejects("trailing junk after a runway",
            variant(6, "runway 100 0 please"), "line 6: trailing text");
    rejects("trailing junk after the version",
            variant(1, "barnstormer-level 1 and a half"),
            "line 1: trailing text");

    /* Counted limits need a generated file. */
    {
        char big[8192];
        int n = snprintf(big, sizeof(big), "barnstormer-level 1\nname Test\n"
                         "size 3000 200\nground 3000:100\n");
        for (int i = 0; i < LEVEL_MAX_RUNWAYS + 1; i++)
            n += snprintf(big + n, sizeof(big) - (size_t)n,
                          "runway %d 0\n", 100 + i * 100);
        rejects("a ninth runway", big, "more than 8 runways");

        n = snprintf(big, sizeof(big), VALID);
        for (int i = 0; i < MAX_TARG + 1; i++)
            n += snprintf(big + n, sizeof(big) - (size_t)n,
                          "target %d 0\n", 500 + i * 20);
        rejects("a twenty-first building", big, "more than 20 buildings");

        n = snprintf(big, sizeof(big), VALID);
        for (int i = 0; i < MAX_OXEN + 1; i++)
            n += snprintf(big + n, sizeof(big) - (size_t)n,
                          "ox %d 80\n", 700 + i * 20);
        rejects("a third ox", big, "more than 2 oxen");
    }

    /* ---- failures that are not the file's fault ---- */

    lv = NULL;
    errno = 0;
    ok("a missing file reports ENOENT, not EINVAL",
       level_load("/nonexistent/nope.lvl", &lv) < 0 && errno == ENOENT);

    ok("saving somewhere unwritable fails without crashing",
       level_save("/proc/nonexistent/nope.lvl", &level_classic) < 0);

    {
        /* An interrupted save must not leave debris beside the level. */
        path_in(path, sizeof(path), "debris.lvl");
        ok("a good save leaves no .tmp behind",
           level_save(path, &level_classic) == 0 && ({
               char tmp[520];
               snprintf(tmp, sizeof(tmp), "%s.tmp", path);
               access(tmp, F_OK) != 0;
           }));
    }

    {
        /* An in-memory level is validated too, not just a parsed one. */
        level_t bad = level_classic;
        bad.n_runways = 1;
        path_in(path, sizeof(path), "never.lvl");
        ok("saving a level that would not load is refused",
           level_save(path, &bad) < 0 && errno == EINVAL &&
           strstr(level_error(), "at least 2 runways"));
        ok("and nothing is written", access(path, F_OK) != 0);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0)
        printf("warning: could not clean %s\n", sandbox);

    printf("\n%s (%d failure%s)\n", failures ? "FAILURES" : "all ok",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
