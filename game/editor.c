/*
 * editor.c -- the level editor's model.
 *
 * Every change goes through the same shape: make it, ask level_check()
 * whether the result is still a level, and undo it if it is not.  The check
 * is the loader's own, so the editor cannot author anything the loader would
 * refuse -- which is what makes "fly it now" safe to offer at any moment, and
 * what keeps the rules in one place rather than two that drift apart.
 *
 * Undo is a whole-struct copy.  An editor_t is about four kilobytes, the
 * operations happen at the speed somebody presses keys, and a copy cannot get
 * out of step with the thing it is meant to restore the way a per-operation
 * undo record can.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "editor.h"
#include "paths.h"

#define NEW_GROUND_HEIGHT  60     /* a plain with room to dive into it */

static const char *const tool_name[ED_TOOL_COUNT] = {
    "TERRAIN", "BUILDING", "RUNWAY", "OX",
};
static const char *const kind_name[4] = {
    "HOUSE", "FACTORY", "FUEL DUMP", "HANGAR",
};
static const char *const orient_name[2] = {
    "FACING RIGHT", "FACING LEFT",
};

/* ---- housekeeping ------------------------------------------------------- */

static void status(editor_t *ed, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void status(editor_t *ed, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed->status, sizeof(ed->status), fmt, ap);
    va_end(ap);
}

/* Point the level_t at the arrays it describes.  Called after anything that
 * changes a count, and after an undo, so the view is never stale. */
static void sync(editor_t *ed)
{
    ed->view.name      = ed->name;
    ed->view.author    = ed->author[0] ? ed->author : NULL;
    ed->view.format    = LEVEL_FORMAT_VERSION;
    ed->view.width     = MAX_X;
    ed->view.height    = MAX_Y;
    ed->view.rand_seed = ed->seed;
    ed->view.ground    = ed->ground;
    ed->view.runways   = ed->runways;
    ed->view.n_runways = ed->n_runways;
    ed->view.targets   = ed->targets;
    ed->view.n_targets = ed->n_targets;
    ed->view.oxen      = ed->oxen;
    ed->view.n_oxen    = ed->n_oxen;
}

/* Keep the change if the result is still a level, put it back if it is not.
 * `what` names the thing that was attempted, for the status line. */
static int keep(editor_t *ed, const editor_t *before, const char *what)
{
    sync(ed);
    if (level_check(&ed->view) == 0) {
        ed->dirty = true;
        return 0;
    }

    char why[EDITOR_STATUS_MAX];
    snprintf(why, sizeof(why), "%s", level_error());
    *ed = *before;
    sync(ed);
    status(ed, "%s: %s", what, why);
    return -1;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Things are placed, and carried, centred on the cursor -- which is where
 * they look like they are going. */
static int centred(const editor_t *ed, int width)
{
    return clampi(ed->cursor - width / 2, 0, MAX_X - width);
}

/* "levels/salt-flats.lvl" -> "Salt Flats".  A level always has a name, and
 * the file it is being written to is a better guess than "Untitled". */
static void name_from_path(char *dst, size_t n, const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    /* Copied rather than snprintf'd: the path may be longer than a name is
     * allowed to be, and that is a trim, not a mistake worth a warning. */
    char tmp[LEVEL_NAME_MAX + 1];
    size_t len = strlen(base);
    if (len > LEVEL_NAME_MAX)
        len = LEVEL_NAME_MAX;
    memcpy(tmp, base, len);
    tmp[len] = '\0';

    char *dot = strrchr(tmp, '.');
    if (dot && dot != tmp)
        *dot = '\0';

    bool start = true;
    for (char *p = tmp; *p; p++) {
        if (*p == '-' || *p == '_')
            *p = ' ';
        if (*p == ' ') {
            start = true;
        } else {
            if (start && *p >= 'a' && *p <= 'z')
                *p = (char)(*p - 'a' + 'A');
            start = false;
        }
    }

    snprintf(dst, n, "%s", tmp[0] ? tmp : "Untitled");
}

/* ---- opening and saving ------------------------------------------------- */

void editor_new(editor_t *ed, const char *path)
{
    memset(ed, 0, sizeof(*ed));
    snprintf(ed->path, sizeof(ed->path), "%s", path ? path : "");
    name_from_path(ed->name, sizeof(ed->name), ed->path);
    ed->seed = 7491;                      /* the original's, as good as any */

    memset(ed->ground, NEW_GROUND_HEIGHT, sizeof(ed->ground));

    /* The two a level cannot do without: one home field each, far enough
     * apart to fly between. */
    ed->runways[0] = (level_runway_t){ .x = 400,  .orient = 0 };
    ed->runways[1] = (level_runway_t){ .x = 2400, .orient = 1 };
    ed->n_runways = 2;

    ed->cursor = MAX_X / 2;
    ed->brush = 12;
    ed->tool = ED_TERRAIN;
    sync(ed);
    status(ed, "new level");
}

int editor_open(editor_t *ed, const char *path)
{
    level_t *lv = NULL;
    if (level_load(path, &lv) < 0) {
        if (errno == ENOENT) {
            editor_new(ed, path);
            return 0;
        }
        return -1;
    }

    editor_new(ed, path);
    snprintf(ed->name, sizeof(ed->name), "%s", lv->name);
    if (lv->author)
        snprintf(ed->author, sizeof(ed->author), "%s", lv->author);
    ed->seed = lv->rand_seed;

    memcpy(ed->ground, lv->ground, MAX_X);
    ed->n_runways = lv->n_runways;
    memcpy(ed->runways, lv->runways,
           sizeof(level_runway_t) * (size_t)lv->n_runways);
    ed->n_targets = lv->n_targets;
    memcpy(ed->targets, lv->targets,
           sizeof(level_target_t) * (size_t)lv->n_targets);
    ed->n_oxen = lv->n_oxen;
    memcpy(ed->oxen, lv->oxen, sizeof(level_point_t) * (size_t)lv->n_oxen);

    level_free(lv);
    sync(ed);
    ed->dirty = false;
    status(ed, "opened %s", ed->name);
    return 0;
}

int editor_save(editor_t *ed)
{
    sync(ed);
    /* Saving into the level directory should not require having made it. */
    sw_make_parent(ed->path);
    if (level_save(ed->path, &ed->view) < 0) {
        status(ed, "not saved: %s", level_error());
        return -1;
    }
    ed->dirty = false;
    /* With the hash, so that somebody sending the file and somebody receiving
     * it have something short to compare. */
    status(ed, "saved [%08x] to %s", level_hash(&ed->view), ed->path);
    return 0;
}

const level_t *editor_level(const editor_t *ed)
{
    return &ed->view;
}

/* ---- navigation --------------------------------------------------------- */

/* Put what is being carried where the cursor is.  Returns -1, having changed
 * nothing, if it cannot go there. */
static int carry_follow(editor_t *ed)
{
    editor_t before = *ed;

    switch (ed->carry) {
    case ED_CARRY_TARGET:
        ed->targets[ed->carry_i].x =
            (uint16_t)centred(ed, LEVEL_TARGET_WIDTH);
        break;
    case ED_CARRY_RUNWAY:
        ed->runways[ed->carry_i].x =
            (uint16_t)centred(ed, LEVEL_RUNWAY_SPAN);
        break;
    case ED_CARRY_OX: {
        int x = centred(ed, LEVEL_TARGET_WIDTH);
        ed->oxen[ed->carry_i].x = (uint16_t)x;
        /* An ox walks on the ground rather than through it. */
        ed->oxen[ed->carry_i].y =
            (uint16_t)clampi(ed->ground[x] + 16, 0, MAX_Y - 1);
        break;
    }
    default:
        return 0;
    }

    return keep(ed, &before, "cannot move it there");
}

void editor_move(editor_t *ed, int dx)
{
    int want = clampi(ed->cursor + dx, 0, MAX_X - 1);
    ed->cursor = want;

    if (ed->carry == ED_CARRY_NONE)
        return;

    /* keep() puts the whole struct back when it refuses, cursor included, so
     * the cursor is re-applied afterwards: what is carried may be stuck, but
     * the hand holding it should not be. */
    if (carry_follow(ed) < 0)
        ed->cursor = want;
}

void editor_brush(editor_t *ed, int d)
{
    ed->brush = clampi(ed->brush + d, EDITOR_BRUSH_MIN, EDITOR_BRUSH_MAX);
    status(ed, "brush %d", ed->brush * 2 + 1);
}

void editor_tool(editor_t *ed, int d)
{
    int n = ED_TOOL_COUNT;
    ed->tool = (edtool_t)(((int)ed->tool + d % n + n) % n);
    ed->variant = 0;
    status(ed, "%s", editor_tool_name(ed));
}

int editor_variants(const editor_t *ed)
{
    switch (ed->tool) {
    case ED_TARGET: return 4;
    case ED_RUNWAY: return 2;
    default:        return 1;
    }
}

void editor_variant(editor_t *ed, int d)
{
    int n = editor_variants(ed);
    ed->variant = ((ed->variant + d % n) + n) % n;
    status(ed, "%s", editor_variant_name(ed));
}

const char *editor_tool_name(const editor_t *ed)
{
    return tool_name[ed->tool];
}

const char *editor_variant_name(const editor_t *ed)
{
    switch (ed->tool) {
    case ED_TARGET: return kind_name[ed->variant & 3];
    case ED_RUNWAY: return orient_name[ed->variant & 1];
    default:        return "";
    }
}

/* ---- terrain ------------------------------------------------------------ */

static void span(const editor_t *ed, int *lo, int *hi)
{
    *lo = clampi(ed->cursor - ed->brush, 0, MAX_X - 1);
    *hi = clampi(ed->cursor + ed->brush, 0, MAX_X - 1);
}

int editor_raise(editor_t *ed, int delta)
{
    editor_t before = *ed;
    int lo, hi;
    span(ed, &lo, &hi);

    for (int x = lo; x <= hi; x++)
        ed->ground[x] = (uint8_t)clampi(ed->ground[x] + delta,
                                        LEVEL_GROUND_MIN, LEVEL_GROUND_MAX);

    if (keep(ed, &before, delta >= 0 ? "cannot raise" : "cannot lower") < 0)
        return -1;
    status(ed, "%s %d columns", delta >= 0 ? "raised" : "lowered",
           hi - lo + 1);
    return 0;
}

int editor_smooth(editor_t *ed)
{
    editor_t before = *ed;
    int lo, hi;
    span(ed, &lo, &hi);

    /* A three-tap average, reading the heights as they were so the pass does
     * not chase its own output along the span. */
    uint8_t out[MAX_X];
    memcpy(out, ed->ground, sizeof(out));
    for (int x = lo; x <= hi; x++) {
        int l = x > 0 ? ed->ground[x - 1] : ed->ground[x];
        int r = x < MAX_X - 1 ? ed->ground[x + 1] : ed->ground[x];
        out[x] = (uint8_t)clampi((l + ed->ground[x] + r + 1) / 3,
                                 LEVEL_GROUND_MIN, LEVEL_GROUND_MAX);
    }
    memcpy(ed->ground, out, sizeof(out));

    if (keep(ed, &before, "cannot smooth") < 0)
        return -1;
    status(ed, "smoothed %d columns", hi - lo + 1);
    return 0;
}

int editor_flatten(editor_t *ed)
{
    editor_t before = *ed;
    int lo, hi;
    span(ed, &lo, &hi);

    uint8_t h = ed->ground[ed->cursor];
    for (int x = lo; x <= hi; x++)
        ed->ground[x] = h;

    if (keep(ed, &before, "cannot flatten") < 0)
        return -1;
    status(ed, "flattened %d columns at %d", hi - lo + 1, h);
    return 0;
}

/* ---- placing and erasing ------------------------------------------------ */

int editor_place(editor_t *ed)
{
    editor_t before = *ed;

    switch (ed->tool) {
    case ED_TERRAIN:
        return editor_flatten(ed);

    case ED_TARGET:
        if (ed->n_targets >= MAX_TARG) {
            status(ed, "cannot place: a level holds %d buildings", MAX_TARG);
            return -1;
        }
        ed->targets[ed->n_targets++] = (level_target_t){
            .x = (uint16_t)centred(ed, LEVEL_TARGET_WIDTH),
            .kind = (uint8_t)(ed->variant & 3),
        };
        if (keep(ed, &before, "cannot place") < 0)
            return -1;
        status(ed, "%s at %u", editor_variant_name(ed),
               ed->targets[ed->n_targets - 1].x);
        return 0;

    case ED_RUNWAY:
        if (ed->n_runways >= LEVEL_MAX_RUNWAYS) {
            status(ed, "cannot place: a level holds %d runways",
                   LEVEL_MAX_RUNWAYS);
            return -1;
        }
        ed->runways[ed->n_runways++] = (level_runway_t){
            .x = (uint16_t)centred(ed, LEVEL_RUNWAY_SPAN),
            .orient = (uint8_t)(ed->variant & 1),
        };
        if (keep(ed, &before, "cannot place") < 0)
            return -1;
        status(ed, "runway %d at %u", ed->n_runways,
               ed->runways[ed->n_runways - 1].x);
        return 0;

    case ED_OX: {
        if (ed->n_oxen >= MAX_OXEN) {
            status(ed, "cannot place: a level holds %d oxen", MAX_OXEN);
            return -1;
        }
        int x = centred(ed, LEVEL_TARGET_WIDTH);
        /* An ox stands on the ground, the same 16 above it a building does. */
        ed->oxen[ed->n_oxen++] = (level_point_t){
            .x = (uint16_t)x,
            .y = (uint16_t)clampi(ed->ground[x] + 16, 0, MAX_Y - 1),
        };
        if (keep(ed, &before, "cannot place") < 0)
            return -1;
        status(ed, "ox at %u", ed->oxen[ed->n_oxen - 1].x);
        return 0;
    }

    default:
        return -1;
    }
}

/* Whatever the cursor is inside, in the order things are drawn on top of each
 * other: the small things first, the strip they may be standing on last. */
int editor_erase(editor_t *ed)
{
    editor_t before = *ed;
    int c = ed->cursor;

    for (int i = 0; i < ed->n_oxen; i++) {
        if (c < ed->oxen[i].x || c >= ed->oxen[i].x + LEVEL_TARGET_WIDTH)
            continue;
        memmove(&ed->oxen[i], &ed->oxen[i + 1],
                sizeof(level_point_t) * (size_t)(ed->n_oxen - i - 1));
        ed->n_oxen--;
        if (keep(ed, &before, "cannot remove") < 0)
            return -1;
        status(ed, "ox removed");
        return 0;
    }

    for (int i = 0; i < ed->n_targets; i++) {
        if (c < ed->targets[i].x || c >= ed->targets[i].x + LEVEL_TARGET_WIDTH)
            continue;
        /* Shifted rather than swapped: which slot a building sits in decides
         * whose side it is on (game.c), so the order has to hold. */
        memmove(&ed->targets[i], &ed->targets[i + 1],
                sizeof(level_target_t) * (size_t)(ed->n_targets - i - 1));
        ed->n_targets--;
        if (keep(ed, &before, "cannot remove") < 0)
            return -1;
        status(ed, "building removed");
        return 0;
    }

    for (int i = 0; i < ed->n_runways; i++) {
        if (c < ed->runways[i].x || c >= ed->runways[i].x + LEVEL_RUNWAY_SPAN)
            continue;
        memmove(&ed->runways[i], &ed->runways[i + 1],
                sizeof(level_runway_t) * (size_t)(ed->n_runways - i - 1));
        ed->n_runways--;
        if (keep(ed, &before, "cannot remove") < 0)
            return -1;
        status(ed, "runway removed");
        return 0;
    }

    status(ed, "nothing at %d", c);
    return -1;
}

/* ---- moving something already placed ------------------------------------ */

int editor_grab(editor_t *ed)
{
    if (ed->carry != ED_CARRY_NONE)
        return 0;

    int c = ed->cursor;

    /* The same order erasing uses: the small things before the strip they may
     * be standing next to. */
    for (int i = 0; i < ed->n_oxen; i++)
        if (c >= ed->oxen[i].x && c < ed->oxen[i].x + LEVEL_TARGET_WIDTH) {
            ed->carry = ED_CARRY_OX;
            ed->carry_i = i;
            ed->carry_home = ed->oxen[i];
            status(ed, "carrying an ox -- g drops it, Esc puts it back");
            return 0;
        }

    for (int i = 0; i < ed->n_targets; i++)
        if (c >= ed->targets[i].x &&
            c < ed->targets[i].x + LEVEL_TARGET_WIDTH) {
            ed->carry = ED_CARRY_TARGET;
            ed->carry_i = i;
            ed->carry_home = (level_point_t){ ed->targets[i].x, 0 };
            status(ed, "carrying a %s -- g drops it, Esc puts it back",
                   kind_name[ed->targets[i].kind & 3]);
            return 0;
        }

    for (int i = 0; i < ed->n_runways; i++)
        if (c >= ed->runways[i].x &&
            c < ed->runways[i].x + LEVEL_RUNWAY_SPAN) {
            ed->carry = ED_CARRY_RUNWAY;
            ed->carry_i = i;
            ed->carry_home = (level_point_t){ ed->runways[i].x, 0 };
            status(ed, "carrying runway %d -- g drops it, Esc puts it back",
                   i + 1);
            return 0;
        }

    status(ed, "nothing to pick up at %d", c);
    return -1;
}

void editor_drop(editor_t *ed)
{
    if (ed->carry == ED_CARRY_NONE)
        return;
    ed->carry = ED_CARRY_NONE;
    status(ed, "put down");
}

void editor_ungrab(editor_t *ed)
{
    switch (ed->carry) {
    case ED_CARRY_TARGET:
        ed->targets[ed->carry_i].x = ed->carry_home.x;
        break;
    case ED_CARRY_RUNWAY:
        ed->runways[ed->carry_i].x = ed->carry_home.x;
        break;
    case ED_CARRY_OX:
        ed->oxen[ed->carry_i] = ed->carry_home;
        break;
    default:
        return;
    }
    ed->carry = ED_CARRY_NONE;
    sync(ed);
    status(ed, "put back where it was");
}

edcarry_t editor_carrying(const editor_t *ed)
{
    return ed->carry;
}

void editor_carry_span(const editor_t *ed, int *x, int *w)
{
    switch (ed->carry) {
    case ED_CARRY_TARGET:
        *x = ed->targets[ed->carry_i].x;
        *w = LEVEL_TARGET_WIDTH;
        return;
    case ED_CARRY_RUNWAY:
        *x = ed->runways[ed->carry_i].x;
        *w = LEVEL_RUNWAY_SPAN;
        return;
    case ED_CARRY_OX:
        *x = ed->oxen[ed->carry_i].x;
        *w = LEVEL_TARGET_WIDTH;
        return;
    default:
        *x = 0;
        *w = 0;
        return;
    }
}

/* ---- typing a name or an author ----------------------------------------- */

void editor_type_begin(editor_t *ed, edfield_t field)
{
    if (field != ED_FIELD_NAME && field != ED_FIELD_AUTHOR)
        return;
    ed->typing = field;
    snprintf(ed->typebuf, sizeof(ed->typebuf), "%s",
             field == ED_FIELD_NAME ? ed->name : ed->author);
    status(ed, "%s: Enter keeps it, Esc leaves it",
           editor_field_name(field));
}

void editor_type_char(editor_t *ed, char c)
{
    if (!ed->typing || c < 0x20 || c >= 0x7f)
        return;
    size_t n = strlen(ed->typebuf);
    if (n >= LEVEL_NAME_MAX) {
        status(ed, "that is as long as a %s can be",
               editor_field_name(ed->typing));
        return;
    }
    ed->typebuf[n] = c;
    ed->typebuf[n + 1] = '\0';
}

void editor_type_back(editor_t *ed)
{
    if (!ed->typing)
        return;
    size_t n = strlen(ed->typebuf);
    if (n)
        ed->typebuf[n - 1] = '\0';
}

/* Leading and trailing spaces are invisible in a menu and would survive a
 * round trip as nothing at all, the loader trimming them on the way back in. */
static void trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
}

int editor_type_end(editor_t *ed, bool keep_it)
{
    if (!ed->typing)
        return -1;

    if (!keep_it) {
        ed->typing = ED_FIELD_NONE;
        status(ed, "left as it was");
        return 0;
    }

    editor_t before = *ed;
    trim(ed->typebuf);

    if (ed->typing == ED_FIELD_NAME)
        snprintf(ed->name, sizeof(ed->name), "%s", ed->typebuf);
    else
        snprintf(ed->author, sizeof(ed->author), "%s", ed->typebuf);

    /* keep() restores the whole struct on a refusal, which puts the field
     * back with the text still in it -- exactly where they need to be. */
    if (keep(ed, &before, "cannot rename") < 0)
        return -1;

    edfield_t was = ed->typing;
    ed->typing = ED_FIELD_NONE;
    if (was == ED_FIELD_NAME)
        status(ed, "named %s", ed->name);
    else if (ed->author[0])
        status(ed, "by %s", ed->author);
    else
        status(ed, "author cleared");
    return 0;
}

edfield_t editor_typing(const editor_t *ed)
{
    return ed->typing;
}

const char *editor_typing_text(const editor_t *ed)
{
    return ed->typebuf;
}

const char *editor_field_name(edfield_t field)
{
    return field == ED_FIELD_NAME ? "NAME"
         : field == ED_FIELD_AUTHOR ? "AUTHOR" : "";
}

void editor_note(editor_t *ed, const char *text)
{
    status(ed, "%s", text);
}

const char *editor_status(const editor_t *ed)
{
    return ed->status;
}

bool editor_dirty(const editor_t *ed)
{
    return ed->dirty;
}
