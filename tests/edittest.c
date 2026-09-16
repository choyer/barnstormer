/*
 * edittest.c -- the map editor's model, headless.
 *
 * The editor's promise is that the map under construction is always a map:
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

    /* ---- a map from nothing ---- */

    path_in(path, sizeof(path), "salt-flats.map");
    editor_new(&ed, path);
    ok("a new map is a map", map_check(editor_map(&ed)) == 0);
    ok("it has the two runways it cannot do without",
       editor_map(&ed)->n_runways == 2);
    ok("and is named after the file", !strcmp(editor_map(&ed)->name,
                                              "Salt Flats"));
    ok("nothing to save yet", !editor_dirty(&ed));

    /* ---- opening ---- */

    ok("opening what is not there starts a new map",
       editor_open(&ed, path) == 0 && editor_map(&ed)->n_runways == 2);

    write_file("broken.map",
               "barnstormer-map 1\nname Broken\nsize 3000 200\n"
               "ground 2999:100\nrunway 100 0\nrunway 200 1\n");
    editor_t kept = ed;
    ok("a file that will not load is not replaced with a blank one",
       editor_open(&ed, path_in(path, sizeof(path), "broken.map")) < 0 &&
       strstr(map_error(), "covers 2999"));
    ed = kept;

    /* ---- the round trip ---- */

    path_in(path, sizeof(path), "work.map");
    editor_new(&ed, path);
    at(1000, 10);
    editor_raise(&ed, 30);
    use(ED_TARGET, TARGET_FUEL);
    at(1500, 10);
    editor_place(&ed);
    use(ED_OX, 0);
    at(1700, 10);
    editor_place(&ed);
    ok("a map with work in it is dirty", editor_dirty(&ed));
    ok("it saves", editor_save(&ed) == 0);
    ok("and is no longer dirty", !editor_dirty(&ed));

    {
        map_t *back = NULL;
        ok("it loads back", map_load(path, &back) == 0);
        if (back) {
            const map_t *w = editor_map(&ed);
            ok("with the same terrain", !memcmp(back->ground, w->ground,
                                                MAX_X));
            ok("the same building", back->n_targets == 1 &&
               back->targets[0].kind == TARGET_FUEL);
            ok("and the same ox", back->n_oxen == 1 &&
               back->oxen[0].y == back->ground[back->oxen[0].x] + 16);
            map_free(back);
        }
    }

    editor_t opened;
    ok("reopening it gives back what was saved",
       editor_open(&opened, path) == 0 &&
       !memcmp(editor_map(&opened)->ground, editor_map(&ed)->ground,
               MAX_X) &&
       !editor_dirty(&opened));

    /* ---- terrain ---- */

    editor_new(&ed, path);
    at(1000, 10);
    for (int i = 0; i < 60; i++)
        editor_raise(&ed, 5);
    ok("raising stops at the ceiling",
       editor_map(&ed)->ground[1000] == MAP_GROUND_MAX);
    for (int i = 0; i < 60; i++)
        editor_raise(&ed, -5);
    ok("and lowering at the floor",
       editor_map(&ed)->ground[1000] == MAP_GROUND_MIN);

    editor_new(&ed, path);
    at(1000, 10);
    editor_raise(&ed, 30);
    at(990, 3);
    editor_smooth(&ed);
    ok("smoothing turns a step into a ramp",
       editor_map(&ed)->ground[989] > 60 &&
       editor_map(&ed)->ground[990] < 90);

    editor_new(&ed, path);
    at(1000, 10);
    editor_raise(&ed, 30);
    at(1000, 40);
    editor_flatten(&ed);
    ok("flattening levels the span with the cursor",
       editor_map(&ed)->ground[965] == 90 &&
       editor_map(&ed)->ground[1035] == 90);

    /* ---- terrain that would strand an aircraft ---- */

    editor_new(&ed, path);
    {
        uint8_t before[MAX_X];
        memcpy(before, editor_map(&ed)->ground, MAX_X);
        at(395, 5);                        /* half on the runway at 400 */
        int rc = editor_raise(&ed, 10);
        ok("digging into a runway is refused", rc < 0);
        ok("with the rule that stopped it",
           strstr(editor_status(&ed), "not flat") != NULL);
        ok("and the terrain is exactly as it was",
           !memcmp(before, editor_map(&ed)->ground, MAX_X));
        ok("a refused change leaves nothing to save", !editor_dirty(&ed));
    }

    /* ---- placing ---- */

    editor_new(&ed, path);
    use(ED_TARGET, TARGET_HOUSE);
    at(1000, 10);
    ok("a building lands centred on the cursor",
       editor_place(&ed) == 0 &&
       editor_map(&ed)->targets[0].x == 1000 - MAP_TARGET_WIDTH / 2);

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
    ok("a map takes twenty buildings and no more",
       editor_map(&ed)->n_targets == MAX_TARG && editor_place(&ed) < 0);

    editor_new(&ed, path);
    use(ED_RUNWAY, 1);
    for (int i = 0; i < MAP_MAX_RUNWAYS; i++) {
        at(1000 + i * 40, 5);
        editor_place(&ed);
    }
    ok("and eight runways and no more",
       editor_map(&ed)->n_runways == MAP_MAX_RUNWAYS &&
       editor_place(&ed) < 0);

    editor_new(&ed, path);
    use(ED_OX, 0);
    at(1000, 5);
    editor_place(&ed);
    at(1100, 5);
    editor_place(&ed);
    at(1200, 5);
    ok("two oxen and no more",
       editor_map(&ed)->n_oxen == MAX_OXEN && editor_place(&ed) < 0);

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
       editor_erase(&ed) == 0 && editor_map(&ed)->n_targets == 2);
    ok("and the rest keep their order, which decides whose they are",
       editor_map(&ed)->targets[0].x == 1000 - 8 &&
       editor_map(&ed)->targets[1].x == 1200 - 8);

    at(2000, 5);
    ok("erasing empty ground says so", editor_erase(&ed) < 0 &&
       strstr(editor_status(&ed), "nothing at"));

    editor_new(&ed, path);
    at(405, 5);
    ok("erasing one of the last two runways is refused",
       editor_erase(&ed) < 0 &&
       strstr(editor_status(&ed), "at least 2 runways"));

    /* ---- moving something already placed ---- */

    editor_new(&ed, path);
    use(ED_TARGET, TARGET_HOUSE);
    for (int i = 0; i < 3; i++) {
        at(1000 + i * 100, 5);
        editor_place(&ed);
    }

    at(2000, 5);
    ok("there is nothing to pick up on empty ground",
       editor_grab(&ed) < 0 && editor_carrying(&ed) == ED_CARRY_NONE);

    at(1100, 5);
    ok("a building can be picked up",
       editor_grab(&ed) == 0 && editor_carrying(&ed) == ED_CARRY_TARGET);

    editor_move(&ed, 300);
    ok("and follows the cursor",
       editor_map(&ed)->targets[1].x == 1400 - MAP_TARGET_WIDTH / 2);
    ok("keeping its place in the map, which decides whose it is",
       editor_map(&ed)->n_targets == 3 &&
       editor_map(&ed)->targets[0].x == 1000 - MAP_TARGET_WIDTH / 2 &&
       editor_map(&ed)->targets[2].x == 1200 - MAP_TARGET_WIDTH / 2);

    editor_drop(&ed);
    ok("putting it down leaves it there",
       editor_carrying(&ed) == ED_CARRY_NONE &&
       editor_map(&ed)->targets[1].x == 1400 - MAP_TARGET_WIDTH / 2);

    at(1400, 5);
    editor_grab(&ed);
    editor_move(&ed, 500);
    editor_ungrab(&ed);
    ok("Esc puts it back where it came from",
       editor_carrying(&ed) == ED_CARRY_NONE &&
       editor_map(&ed)->targets[1].x == 1400 - MAP_TARGET_WIDTH / 2);

    /* Carried into a place it cannot go: the map has to stay valid, so it
     * stays where it was -- but the cursor must not get stuck with it. */
    at(1400, 5);
    editor_grab(&ed);
    editor_move(&ed, -400);            /* onto the building at 1000 */
    ok("it will not be carried onto another building",
       editor_map(&ed)->targets[1].x == 1400 - MAP_TARGET_WIDTH / 2 &&
       strstr(editor_status(&ed), "columns apart"));
    ok("but the cursor keeps going", ed.cursor == 1000);
    editor_move(&ed, -400);            /* on past it */
    ok("and it catches up once the way is clear",
       editor_map(&ed)->targets[1].x == 600 - MAP_TARGET_WIDTH / 2);
    editor_drop(&ed);

    /* A runway carried onto ground that is not flat enough for it. */
    editor_new(&ed, path);
    at(1500, 10);
    editor_raise(&ed, 40);
    at(410, 5);
    ok("a runway can be picked up",
       editor_grab(&ed) == 0 && editor_carrying(&ed) == ED_CARRY_RUNWAY);
    int home = editor_map(&ed)->runways[0].x;
    editor_move(&ed, 1080);            /* onto the edge of the hill */
    ok("and will not be carried onto a slope",
       editor_map(&ed)->runways[0].x == home &&
       strstr(editor_status(&ed), "not flat"));
    editor_ungrab(&ed);

    /* An ox is carried along the ground rather than at a fixed height. */
    editor_new(&ed, path);
    use(ED_OX, 0);
    at(1000, 5);
    editor_place(&ed);
    at(1500, 10);
    editor_raise(&ed, 30);
    at(1000, 5);
    editor_grab(&ed);
    editor_move(&ed, 500);
    ok("an ox carried uphill stands on the ground when it arrives",
       editor_map(&ed)->oxen[0].y ==
           editor_map(&ed)->ground[editor_map(&ed)->oxen[0].x] + 16);
    editor_drop(&ed);

    /* ---- naming it ---- */

    path_in(path, sizeof(path), "work.map");
    editor_new(&ed, path);
    ok("a new map is named after its file",
       !strcmp(editor_map(&ed)->name, "Work"));

    editor_type_begin(&ed, ED_FIELD_NAME);
    ok("typing starts from what is already there",
       editor_typing(&ed) == ED_FIELD_NAME &&
       !strcmp(editor_typing_text(&ed), "Work"));

    for (int i = 0; i < 4; i++)
        editor_type_back(&ed);
    for (const char *p2 = "Bridge Too Far"; *p2; p2++)
        editor_type_char(&ed, *p2);
    ok("Enter keeps what was typed",
       editor_type_end(&ed, true) == 0 &&
       !strcmp(editor_map(&ed)->name, "Bridge Too Far") &&
       editor_dirty(&ed));
    ok("and the field closes", editor_typing(&ed) == ED_FIELD_NONE);

    editor_type_begin(&ed, ED_FIELD_NAME);
    for (const char *p2 = "Nonsense"; *p2; p2++)
        editor_type_char(&ed, *p2);
    ok("Esc leaves the field as it was",
       editor_type_end(&ed, false) == 0 &&
       !strcmp(editor_map(&ed)->name, "Bridge Too Far"));

    editor_type_begin(&ed, ED_FIELD_NAME);
    while (*editor_typing_text(&ed))
        editor_type_back(&ed);
    ok("a map cannot be left without a name",
       editor_type_end(&ed, true) < 0 &&
       strstr(editor_status(&ed), "needs a name"));
    ok("so the field stays open to be fixed",
       editor_typing(&ed) == ED_FIELD_NAME);
    ok("and the name it had is untouched",
       !strcmp(editor_map(&ed)->name, "Bridge Too Far"));
    editor_type_end(&ed, false);

    editor_type_begin(&ed, ED_FIELD_NAME);
    while (*editor_typing_text(&ed))
        editor_type_back(&ed);
    for (const char *p2 = "   Spaced Out   "; *p2; p2++)
        editor_type_char(&ed, *p2);
    ok("spaces at either end are trimmed",
       editor_type_end(&ed, true) == 0 &&
       !strcmp(editor_map(&ed)->name, "Spaced Out"));

    editor_type_begin(&ed, ED_FIELD_NAME);
    for (int i = 0; i < 200; i++)
        editor_type_char(&ed, 'x');
    ok("a name stops at the length the format allows",
       (int)strlen(editor_typing_text(&ed)) == MAP_NAME_MAX);
    editor_type_end(&ed, false);

    /* ---- and its author ---- */

    editor_type_begin(&ed, ED_FIELD_AUTHOR);
    for (const char *p2 = "CRH"; *p2; p2++)
        editor_type_char(&ed, *p2);
    ok("an author can be typed",
       editor_type_end(&ed, true) == 0 &&
       editor_map(&ed)->author &&
       !strcmp(editor_map(&ed)->author, "CRH"));

    ok("name and author survive a save and a load", editor_save(&ed) == 0 && ({
        map_t *back = NULL;
        bool good = map_load(path, &back) == 0 && back &&
                    !strcmp(back->name, "Spaced Out") &&
                    back->author && !strcmp(back->author, "CRH");
        if (back)
            map_free(back);
        good;
    }));

    editor_type_begin(&ed, ED_FIELD_AUTHOR);
    while (*editor_typing_text(&ed))
        editor_type_back(&ed);
    ok("an author can be cleared, unlike a name",
       editor_type_end(&ed, true) == 0 && editor_map(&ed)->author == NULL);
    ok("and then no author is written", editor_save(&ed) == 0 && ({
        map_t *back = NULL;
        bool good = map_load(path, &back) == 0 && back &&
                    back->author == NULL;
        if (back)
            map_free(back);
        good;
    }));

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

            switch (r % 11) {
            case 0: editor_move(&ed, (int)(r % 401) - 200); break;
            case 1: editor_brush(&ed, (int)(r % 21) - 10);  break;
            case 2: editor_tool(&ed, 1);                    break;
            case 3: editor_variant(&ed, 1);                 break;
            case 4: rc = editor_raise(&ed, (int)(r % 41) - 20); break;
            case 5: rc = editor_smooth(&ed);                break;
            case 6: rc = editor_flatten(&ed);               break;
            case 7: rc = editor_place(&ed);                 break;
            case 8: rc = editor_grab(&ed);                  break;
            case 9: (r & 1) ? editor_drop(&ed) : editor_ungrab(&ed); break;
            default:
                /* Erasing is ignored while something is carried, the way the
                 * editor's keys ignore it. */
                if (editor_carrying(&ed) == ED_CARRY_NONE)
                    rc = editor_erase(&ed);
                break;
            }
            rc < 0 ? refused++ : applied++;

            if (map_check(editor_map(&ed)) != 0) {
                always_valid = false;
                break;
            }
        }
        printf("%-56s %s\n", "4000 random edits leave a map every time",
               always_valid ? "ok" : "FAILED");
        if (!always_valid)
            failures++;
        printf("    %d applied, %d refused\n", applied, refused);

        path_in(path, sizeof(path), "fuzz.map");
        snprintf(ed.path, sizeof(ed.path), "%s", path);
        ok("and what comes out of it saves", editor_save(&ed) == 0);
        map_t *back = NULL;
        ok("and loads back", map_load(path, &back) == 0);
        if (back)
            map_free(back);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", sandbox);
    if (system(cmd) != 0)
        printf("warning: could not clean %s\n", sandbox);

    printf("\n%s (%d failure%s)\n", failures ? "FAILURES" : "all ok",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
