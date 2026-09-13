/*
 * edittest.c -- the level editor's model, headless.
 *
 * The editor's promise is that the level under construction is always a level:
 * whatever you do to it, it can be flown, and it can be saved and loaded back.
 * Most of what follows is that one property, approached from different sides.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "editor.h"

static int failures;
static char sandbox[256];
static editor_t ed;

static void ok(const char *what, bool cond)
{
    printf("%-56s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond) {
        failures++;
        printf("    status: %s\n", editor_status(&ed));
    }
}

static char *path_in(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", sandbox, name);
    return buf;
}

static void write_file(const char *name, const char *body)
{
    char path[512];
    FILE *f = fopen(path_in(path, sizeof(path), name), "w");
    fputs(body, f);
    fclose(f);
}

/* Put the cursor somewhere and set the brush, in one line. */
static void at(int cursor, int brush)
{
    ed.cursor = cursor;
    ed.brush = brush;
}

static void use(edtool_t tool, int variant)
{
    ed.tool = tool;
    ed.variant = variant;
}

int main(void)
{
    snprintf(sandbox, sizeof(sandbox), "/tmp/barnstormer-edittest-%d",
             (int)getpid());
    char cmd[512], path[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0) { /* first run: nothing to remove */ }
    mkdir(sandbox, 0755);

    /* ---- a level from nothing ---- */

    path_in(path, sizeof(path), "salt-flats.lvl");
    editor_new(&ed, path);
    ok("a new level is a level", level_check(editor_level(&ed)) == 0);
    ok("it has the two runways it cannot do without",
       editor_level(&ed)->n_runways == 2);
    ok("and is named after the file", !strcmp(editor_level(&ed)->name,
                                              "Salt Flats"));
    ok("nothing to save yet", !editor_dirty(&ed));

    /* ---- opening ---- */

    ok("opening what is not there starts a new level",
       editor_open(&ed, path) == 0 && editor_level(&ed)->n_runways == 2);

    write_file("broken.lvl",
               "barnstormer-level 1\nname Broken\nsize 3000 200\n"
               "ground 2999:100\nrunway 100 0\nrunway 200 1\n");
    editor_t kept = ed;
    ok("a file that will not load is not replaced with a blank one",
       editor_open(&ed, path_in(path, sizeof(path), "broken.lvl")) < 0 &&
       strstr(level_error(), "covers 2999"));
    ed = kept;

    /* ---- the round trip ---- */

    path_in(path, sizeof(path), "work.lvl");
    editor_new(&ed, path);
    at(1000, 10);
    editor_raise(&ed, 30);
    use(ED_TARGET, TARGET_FUEL);
    at(1500, 10);
    editor_place(&ed);
    use(ED_OX, 0);
    at(1700, 10);
    editor_place(&ed);
    ok("a level with work in it is dirty", editor_dirty(&ed));
    ok("it saves", editor_save(&ed) == 0);
    ok("and is no longer dirty", !editor_dirty(&ed));

    {
        level_t *back = NULL;
        ok("it loads back", level_load(path, &back) == 0);
        if (back) {
            const level_t *w = editor_level(&ed);
            ok("with the same terrain", !memcmp(back->ground, w->ground,
                                                MAX_X));
            ok("the same building", back->n_targets == 1 &&
               back->targets[0].kind == TARGET_FUEL);
            ok("and the same ox", back->n_oxen == 1 &&
               back->oxen[0].y == back->ground[back->oxen[0].x] + 16);
            level_free(back);
        }
    }

    editor_t opened;
    ok("reopening it gives back what was saved",
       editor_open(&opened, path) == 0 &&
       !memcmp(editor_level(&opened)->ground, editor_level(&ed)->ground,
               MAX_X) &&
       !editor_dirty(&opened));

    /* ---- terrain ---- */

    editor_new(&ed, path);
    at(1000, 10);
    for (int i = 0; i < 60; i++)
        editor_raise(&ed, 5);
    ok("raising stops at the ceiling",
       editor_level(&ed)->ground[1000] == LEVEL_GROUND_MAX);
    for (int i = 0; i < 60; i++)
        editor_raise(&ed, -5);
    ok("and lowering at the floor",
       editor_level(&ed)->ground[1000] == LEVEL_GROUND_MIN);

    editor_new(&ed, path);
    at(1000, 10);
    editor_raise(&ed, 30);
    at(990, 3);
    editor_smooth(&ed);
    ok("smoothing turns a step into a ramp",
       editor_level(&ed)->ground[989] > 60 &&
       editor_level(&ed)->ground[990] < 90);

    editor_new(&ed, path);
    at(1000, 10);
    editor_raise(&ed, 30);
    at(1000, 40);
    editor_flatten(&ed);
    ok("flattening levels the span with the cursor",
       editor_level(&ed)->ground[965] == 90 &&
       editor_level(&ed)->ground[1035] == 90);

    /* ---- terrain that would strand an aircraft ---- */

    editor_new(&ed, path);
    {
        uint8_t before[MAX_X];
        memcpy(before, editor_level(&ed)->ground, MAX_X);
        at(395, 5);                        /* half on the runway at 400 */
        int rc = editor_raise(&ed, 10);
        ok("digging into a runway is refused", rc < 0);
        ok("with the rule that stopped it",
           strstr(editor_status(&ed), "not flat") != NULL);
        ok("and the terrain is exactly as it was",
           !memcmp(before, editor_level(&ed)->ground, MAX_X));
        ok("a refused change leaves nothing to save", !editor_dirty(&ed));
    }

    /* ---- placing ---- */

    editor_new(&ed, path);
    use(ED_TARGET, TARGET_HOUSE);
    at(1000, 10);
    ok("a building lands centred on the cursor",
       editor_place(&ed) == 0 &&
       editor_level(&ed)->targets[0].x == 1000 - LEVEL_TARGET_WIDTH / 2);

    at(405, 10);
    ok("a building on a landing strip is refused", editor_place(&ed) < 0);
    ok("naming the strip", strstr(editor_status(&ed), "stands on the runway"));

    at(1004, 10);
    ok("so is one on top of another building", editor_place(&ed) < 0 &&
       strstr(editor_status(&ed), "columns apart"));

    editor_new(&ed, path);
    use(ED_TARGET, TARGET_FACTORY);
    for (int i = 0; i < MAX_TARG; i++) {
        at(1000 + i * 20, 5);
        editor_place(&ed);
    }
    at(1000 + MAX_TARG * 20, 5);
    ok("a level takes twenty buildings and no more",
       editor_level(&ed)->n_targets == MAX_TARG && editor_place(&ed) < 0);

    editor_new(&ed, path);
    use(ED_RUNWAY, 1);
    for (int i = 0; i < LEVEL_MAX_RUNWAYS; i++) {
        at(1000 + i * 40, 5);
        editor_place(&ed);
    }
    ok("and eight runways and no more",
       editor_level(&ed)->n_runways == LEVEL_MAX_RUNWAYS &&
       editor_place(&ed) < 0);

    editor_new(&ed, path);
    use(ED_OX, 0);
    at(1000, 5);
    editor_place(&ed);
    at(1100, 5);
    editor_place(&ed);
    at(1200, 5);
    ok("two oxen and no more",
       editor_level(&ed)->n_oxen == MAX_OXEN && editor_place(&ed) < 0);

    /* ---- erasing ---- */

    editor_new(&ed, path);
    use(ED_TARGET, TARGET_HOUSE);
    int xs[3] = { 1000, 1100, 1200 };
    for (int i = 0; i < 3; i++) {
        at(xs[i], 5);
        editor_place(&ed);
    }
    at(1100, 5);
    ok("erasing takes the building under the cursor",
       editor_erase(&ed) == 0 && editor_level(&ed)->n_targets == 2);
    ok("and the rest keep their order, which decides whose they are",
       editor_level(&ed)->targets[0].x == 1000 - 8 &&
       editor_level(&ed)->targets[1].x == 1200 - 8);

    at(2000, 5);
    ok("erasing empty ground says so", editor_erase(&ed) < 0 &&
       strstr(editor_status(&ed), "nothing at"));

    editor_new(&ed, path);
    at(405, 5);
    ok("erasing one of the last two runways is refused",
       editor_erase(&ed) < 0 &&
       strstr(editor_status(&ed), "at least 2 runways"));

    /* ---- the promise, hammered on ---- */
    {
        editor_new(&ed, path);
        uint32_t rng = 12345;
        int applied = 0, refused = 0;
        bool always_valid = true;

        for (int i = 0; i < 4000; i++) {
            rng = rng * 1103515245u + 12345u;
            unsigned r = (rng >> 16) & 0xFFFF;
            int rc = 0;

            switch (r % 9) {
            case 0: editor_move(&ed, (int)(r % 401) - 200); break;
            case 1: editor_brush(&ed, (int)(r % 21) - 10);  break;
            case 2: editor_tool(&ed, 1);                    break;
            case 3: editor_variant(&ed, 1);                 break;
            case 4: rc = editor_raise(&ed, (int)(r % 41) - 20); break;
            case 5: rc = editor_smooth(&ed);                break;
            case 6: rc = editor_flatten(&ed);               break;
            case 7: rc = editor_place(&ed);                 break;
            default: rc = editor_erase(&ed);                break;
            }
            rc < 0 ? refused++ : applied++;

            if (level_check(editor_level(&ed)) != 0) {
                always_valid = false;
                break;
            }
        }
        printf("%-56s %s\n", "4000 random edits leave a level every time",
               always_valid ? "ok" : "FAILED");
        if (!always_valid)
            failures++;
        printf("    %d applied, %d refused\n", applied, refused);

        path_in(path, sizeof(path), "fuzz.lvl");
        snprintf(ed.path, sizeof(ed.path), "%s", path);
        ok("and what comes out of it saves", editor_save(&ed) == 0);
        level_t *back = NULL;
        ok("and loads back", level_load(path, &back) == 0);
        if (back)
            level_free(back);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0)
        printf("warning: could not clean %s\n", sandbox);

    printf("\n%s (%d failure%s)\n", failures ? "FAILURES" : "all ok",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
