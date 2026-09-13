/*
 * editor.h -- the level editor's model.
 *
 * A mutable working copy of a level, plus the operations the editor's keys
 * drive.  It is deliberately separate from the drawing and the key handling:
 * everything here runs headlessly, which is what lets tests/edittest.c hold
 * the editing rules without a compositor.
 *
 * The working level is always valid.  Every operation that changes it checks
 * the result against level_check() -- the loader's own rules -- and puts the
 * level back the way it was if the change would break one, leaving the reason
 * in editor_status().  So a level under construction can always be flown, and
 * saving it can never produce a file the game would refuse to load.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include "level.h"

#define EDITOR_BRUSH_MIN   1
#define EDITOR_BRUSH_MAX   80
#define EDITOR_PATH_MAX    512
#define EDITOR_STATUS_MAX  200

/* What the placing keys act on.  Terrain is the tool you start in. */
typedef enum {
    ED_TERRAIN = 0,
    ED_TARGET,
    ED_RUNWAY,
    ED_OX,
    ED_TOOL_COUNT,
} edtool_t;

/* Which text field is being typed, if any. */
typedef enum {
    ED_FIELD_NONE = 0,
    ED_FIELD_NAME,
    ED_FIELD_AUTHOR,
} edfield_t;

typedef struct {
    char name[LEVEL_NAME_MAX + 1];
    char author[LEVEL_NAME_MAX + 1];
    uint32_t seed;

    uint8_t ground[MAX_X];
    level_runway_t runways[LEVEL_MAX_RUNWAYS];
    int n_runways;
    level_target_t targets[MAX_TARG];
    int n_targets;
    level_point_t oxen[MAX_OXEN];
    int n_oxen;

    level_t view;              /* a level_t over the arrays above          */

    int cursor;                /* the world column the tools act on        */
    int brush;                 /* terrain brush half-width, in columns     */
    edtool_t tool;
    int variant;               /* building kind, or runway orientation     */

    edfield_t typing;          /* the field being typed, if any            */
    char typebuf[LEVEL_NAME_MAX + 1];

    bool dirty;                /* changed since the last save              */
    char path[EDITOR_PATH_MAX];
    char status[EDITOR_STATUS_MAX];
} editor_t;

/* Start a level from nothing: a flat plain and the two runways every level
 * needs, named after the file it will be saved to. */
void editor_new(editor_t *ed, const char *path);

/* Open `path`, or start a new level if there is nothing there yet.  Returns
 * 0, or -1 with level_error() set when the file is there but will not load --
 * an unreadable level is never silently replaced with a blank one. */
int editor_open(editor_t *ed, const char *path);

int editor_save(editor_t *ed);

/* The working level, for flying, drawing or saving.  Valid at all times. */
const level_t *editor_level(const editor_t *ed);

/* Navigation and tool selection; these cannot fail. */
void editor_move(editor_t *ed, int dx);
void editor_brush(editor_t *ed, int d);
void editor_tool(editor_t *ed, int d);
void editor_variant(editor_t *ed, int d);
int  editor_variants(const editor_t *ed);     /* how many the tool has     */
const char *editor_tool_name(const editor_t *ed);
const char *editor_variant_name(const editor_t *ed);

/* Changes.  0 on success; -1 with nothing changed and editor_status() saying
 * which rule stopped it. */
int editor_raise(editor_t *ed, int delta);    /* the brush span, clamped   */
int editor_smooth(editor_t *ed);
int editor_flatten(editor_t *ed);             /* the span, level with the
                                                 column under the cursor   */
int editor_place(editor_t *ed);               /* per the current tool      */
int editor_erase(editor_t *ed);               /* whatever is at the cursor */

/* ---- typing a name or an author ---------------------------------------- */

/* Begin editing a field, seeded with what is in it.  Everything else is put
 * on hold until the typing ends one way or the other. */
void editor_type_begin(editor_t *ed, edfield_t field);
void editor_type_char(editor_t *ed, char c);
void editor_type_back(editor_t *ed);

/* Finish: `keep` commits, otherwise the field is left as it was.  Returns 0,
 * or -1 when what was typed is not allowed -- an empty name, say -- in which
 * case the field stays open with the text still in it, because throwing it
 * away would be a worse answer than letting them fix it. */
int editor_type_end(editor_t *ed, bool keep);

edfield_t   editor_typing(const editor_t *ed);
const char *editor_typing_text(const editor_t *ed);
const char *editor_field_name(edfield_t field);

/* Say something in the status line -- for the program around the editor, so
 * that its messages land where the editor's own do. */
void editor_note(editor_t *ed, const char *text);

const char *editor_status(const editor_t *ed);
bool editor_dirty(const editor_t *ed);

#endif /* EDITOR_H */
