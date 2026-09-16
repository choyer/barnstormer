/*
 * maptest.c -- the map file format, headless.
 *
 * Everything is written to a throwaway directory under /tmp, so the tests
 * never touch a real map.  The round trip is checked against the classic
 * map rather than a toy one: whatever the format cannot carry, it cannot
 * carry for the map the whole game is balanced around.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "map.h"

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
    snprintf(path, sizeof(path), "%s/bad.map", sandbox);
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);

    map_t *lv = NULL;
    errno = 0;
    int rc = map_load(path, &lv);
    bool good = rc < 0 && errno == EINVAL && lv == NULL &&
                strstr(map_error(), expect) != NULL;
    ok(what, good);
    if (!good)
        printf("    wanted \"%s\", got \"%s\"\n", expect, map_error());
    if (rc == 0)
        map_free(lv);
}

/* The other half: a map that sits just inside the rules must still load. */
static void accepts(const char *what, const char *body)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/good.map", sandbox);
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);

    map_t *lv = NULL;
    int rc = map_load(path, &lv);
    ok(what, rc == 0);
    if (rc < 0)
        printf("    rejected: %s\n", map_error());
    else
        map_free(lv);
}

static void write_in(const char *dir, const char *name, const char *body)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("    could not write %s\n", path);
        return;
    }
    fputs(body, f);
    fclose(f);
}

/* Write a map out, load it back and hash it -- the hash of what a file
 * means, whatever the file looks like. */
static uint32_t hash_of(const char *body)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/hash.map", sandbox);
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);

    map_t *lv = NULL;
    if (map_load(path, &lv) < 0)
        return 0;
    uint32_t h = map_hash(lv);
    map_free(lv);
    return h;
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

/* The smallest map that loads: flat ground, two runways on it. */
#define VALID \
    "barnstormer-map 1\n"    /* 1 */ \
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
    snprintf(sandbox, sizeof(sandbox), "/tmp/barnstormer-maptest-%d",
             (int)getpid());
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0) { /* first run: nothing to remove */ }
    mkdir(sandbox, 0755);

    char path[512], other[512];
    path_in(path, sizeof(path), "classic.map");

    /* ---- the classic map survives a round trip ---- */

    ok("the classic map saves", map_save(path, &map_classic) == 0);

    map_t *lv = NULL;
    ok("and loads back", map_load(path, &lv) == 0 && lv != NULL);

    if (lv) {
        ok("name survives", !strcmp(lv->name, map_classic.name));
        ok("seed survives", lv->rand_seed == map_classic.rand_seed);
        ok("size survives",
           lv->width == map_classic.width &&
           lv->height == map_classic.height);
        ok("every one of the 3000 columns survives",
           !memcmp(lv->ground, map_classic.ground, MAX_X));
        ok("all eight runways survive, in order",
           lv->n_runways == map_classic.n_runways &&
           !memcmp(lv->runways, map_classic.runways,
                   sizeof(map_runway_t) * (size_t)lv->n_runways));
        ok("all twenty buildings survive, in order",
           lv->n_targets == map_classic.n_targets &&
           !memcmp(lv->targets, map_classic.targets,
                   sizeof(map_target_t) * (size_t)lv->n_targets));
        ok("both oxen survive",
           lv->n_oxen == map_classic.n_oxen &&
           !memcmp(lv->oxen, map_classic.oxen,
                   sizeof(map_point_t) * (size_t)lv->n_oxen));

        /* ---- and the serialisation is canonical ---- */
        path_in(other, sizeof(other), "classic2.map");
        ok("the reloaded map saves again", map_save(other, lv) == 0);

        size_t n1 = 0, n2 = 0;
        char *a = slurp(path, &n1), *b = slurp(other, &n2);
        ok("byte for byte the same file", a && b && n1 == n2 && !strcmp(a, b));
        ok("the whole classic map fits in 8 KB of text", n1 < 8192);
        free(a);
        free(b);

        ok("freeing it succeeds", map_free(lv) == 0);
        ok("freeing it twice does not", map_free(lv) < 0 && errno == EINVAL);
    }

    ok("a built-in map cannot be freed",
       map_free((map_t *)&map_classic) < 0 && errno == EINVAL);

    /* ---- what a hand-edited file may contain ---- */

    path_in(path, sizeof(path), "hand.map");
    FILE *f = fopen(path, "w");
    fputs("# a map someone typed\n"
          "barnstormer-map 1\n"
          "\n"
          "name  Bridge Too Far  \n"
          "author CRH\n"
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
       map_load(path, &lv) == 0 && lv != NULL);
    if (lv) {
        ok("the name is trimmed, inner spaces kept",
           !strcmp(lv->name, "Bridge Too Far"));
        ok("the author is read", lv->author && !strcmp(lv->author, "CRH"));
        ok("ground lines concatenate", lv->ground[0] == 100 &&
           lv->ground[2999] == 100);
        ok("the seed is read", lv->rand_seed == 12345);
        ok("one building, one ox", lv->n_targets == 1 && lv->n_oxen == 1);

        path_in(other, sizeof(other), "hand2.map");
        ok("it saves with the author intact", map_save(other, lv) == 0);
        char *txt = slurp(other, NULL);
        ok("which is on its own line", txt && strstr(txt, "\nauthor CRH\n"));
        ok("and no comments are written", txt && !strchr(txt, '#'));
        free(txt);
        map_free(lv);
    }

    /* ---- what it may not ---- */

    rejects("a file that is not a map at all",
            "hello\n", "not a barnstormer map file");
    rejects("an empty file",
            "", "not a barnstormer map file");
    rejects("a version from the future",
            variant(1, "barnstormer-map 2"), "format version 2");
    rejects("a header with no version",
            variant(1, "barnstormer-map"), "no version number");
    rejects("an empty name",
            variant(2, "name   "), "line 2: name is empty");
    rejects("a name that will not fit",
            variant(2, "name 0123456789012345678901234567890123456789"
                       "012345678901234567890123456789"),
            "line 2: name is longer than 63 bytes");
    rejects("a world of another size",
            variant(4, "size 2000 200"), "line 4: size must be 3000 200");
    rejects("a file with no size line",
            "barnstormer-map 1\nname Test\nground 3000:100\n"
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
            "barnstormer-map 1\nname Test\nsize 3000 200\n"
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
            variant(1, "barnstormer-map 1 and a half"),
            "line 1: trailing text");

    /* Counted limits need a generated file. */
    {
        char big[8192];
        int n = snprintf(big, sizeof(big), "barnstormer-map 1\nname Test\n"
                         "size 3000 200\nground 3000:100\n");
        for (int i = 0; i < MAP_MAX_RUNWAYS + 1; i++)
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

    /* ---- the hash ---- */
    {
        /* Pinned, like the replay hash: the canonical form is a promise to
         * everyone who has already exchanged one, so it must not drift
         * without somebody deciding that it should. */
        ok("the classic map hashes to what it always has",
           map_hash(&map_classic) == 0xc22d60a3u);

        path_in(path, sizeof(path), "hashed.map");
        map_t *back = NULL;
        ok("a map keeps its hash through a save and a load",
           map_save(path, &map_classic) == 0 &&
           map_load(path, &back) == 0 &&
           map_hash(back) == map_hash(&map_classic));
        if (back)
            map_free(back);

        uint32_t canonical = hash_of(VALID);
        uint32_t messy = hash_of(
            "# a map somebody typed, their way\n"
            "barnstormer-map 1\n"
            "size 3000 200\n"
            "\n"
            "name Test\n"
            "wingspan 12\n"          /* an unknown key */
            "seed 7491\n"
            "ground 1500:100\n"
            "   ground 1500:100\n"   /* split, and indented */
            "runway 100 0\n"
            "runway 200 1\n");
        ok("comments, order and line breaks do not change it",
           canonical != 0 && messy == canonical);

        ok("a different name does", hash_of(variant(2, "name Other")) !=
           canonical);
        ok("so does a single column of terrain",
           hash_of(variant(5, "ground 2999:100 1:101")) != canonical);
        ok("and so does moving a runway",
           hash_of(variant(6, "runway 101 0")) != canonical);

        map_t not_a_map = map_classic;
        not_a_map.n_runways = 1;
        ok("something that is not a map has no hash",
           map_hash(&not_a_map) == 0 && map_hash(NULL) == 0);
    }

    /* ---- the map directory ---- */
    {
        setenv("XDG_DATA_HOME", sandbox, 1);

        char dir[512];
        ok("the map directory sits under XDG_DATA_HOME",
           map_dir(dir, sizeof(dir)) && strstr(dir, sandbox) &&
           strstr(dir, "/maps"));

        map_info_t list[MAP_LIST_MAX];
        int skipped = -1;
        ok("an empty listing is not an error",
           map_list(list, MAP_LIST_MAX, &skipped) == 0 && skipped == 0);

        char cmd2[600];
        snprintf(cmd2, sizeof(cmd2), "mkdir -p %s", dir);
        if (system(cmd2) != 0)
            printf("    could not make %s\n", dir);

        /* One at a time: variant() hands back the same static buffer every
         * call, so a table of them would be four pointers to the last one. */
        const char *made[][2] = {
            { "zebra.map",  "name Zebra" },
            { "alpha.map",  "name alpha field" },
            { "mid.map",    "name Mid" },
        };
        for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); i++)
            write_in(dir, made[i][0], variant(2, made[i][1]));
        write_in(dir, "broken.map", variant(5, "ground 2999:100"));
        write_in(dir, "notes.txt", "not a map at all\n");

        int n = map_list(list, MAP_LIST_MAX, &skipped);
        ok("only the maps that load are offered", n == 3);
        ok("and the broken one is counted, not hidden", skipped == 1);
        ok("sorted by name, regardless of case",
           n == 3 && !strcmp(list[0].name, "alpha field") &&
           !strcmp(list[1].name, "Mid") && !strcmp(list[2].name, "Zebra"));
        ok("each with the path it came from",
           n == 3 && strstr(list[0].path, "alpha.map") != NULL);

        /* A listing that cannot fit everything must not overrun. */
        int few = map_list(list, 2, &skipped);
        ok("a short list stops at the space it was given", few == 2);
    }

    /* ---- failures that are not the file's fault ---- */

    lv = NULL;
    errno = 0;
    ok("a missing file reports ENOENT, not EINVAL",
       map_load("/nonexistent/nope.map", &lv) < 0 && errno == ENOENT);

    ok("saving somewhere unwritable fails without crashing",
       map_save("/proc/nonexistent/nope.map", &map_classic) < 0);

    {
        /* An interrupted save must not leave debris beside the map. */
        path_in(path, sizeof(path), "debris.map");
        ok("a good save leaves no .tmp behind",
           map_save(path, &map_classic) == 0 && ({
               char tmp[520];
               snprintf(tmp, sizeof(tmp), "%s.tmp", path);
               access(tmp, F_OK) != 0;
           }));
    }

    {
        /* An in-memory map is validated too, not just a parsed one. */
        map_t bad = map_classic;
        bad.n_runways = 1;
        path_in(path, sizeof(path), "never.map");
        ok("saving a map that would not load is refused",
           map_save(path, &bad) < 0 && errno == EINVAL &&
           strstr(map_error(), "at least 2 runways"));
        ok("and nothing is written", access(path, F_OK) != 0);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0)
        printf("warning: could not clean %s\n", sandbox);

    printf("\n%s (%d failure%s)\n", failures ? "FAILURES" : "all ok",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
