/*
 * level.c -- reading and writing level files.
 *
 * The format is specified in doc/LEVEL_FORMAT.md: UTF-8 text, one
 * "key value..." line per entry, terrain run-length encoded as count:height
 * pairs.  An unknown key is skipped so that a file written by a later version
 * still loads, but anything that would produce a half-valid world -- terrain
 * with holes in it, a runway on a cliff, two buildings on the same ground --
 * is refused outright, naming the line.  Levels get passed between strangers;
 * a loader that limps on is worse than one that says no.
 *
 * level_load() hands back a single heap block holding the level_t and every
 * array it points at.  Those blocks are kept on a list, so level_free() can
 * tell one of ours from a static level like level_classic and refuse the
 * latter rather than call free() on it.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "level.h"

#define LEVEL_MAGIC  "barnstormer-level"
#define GROUND_WRAP  68     /* start a new ground line past this column */

/* ---- error reporting ---------------------------------------------------- */

static char errmsg[256];

const char *level_error(void)
{
    return errmsg;
}

/* Line 0 means "no line to blame": a level held in memory rather than read
 * from a file, which is the case when level_save() validates its argument. */
static void seterr(int line, const char *fmt, ...)
{
    char body[200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    if (line > 0)
        snprintf(errmsg, sizeof(errmsg), "line %d: %s", line, body);
    else
        snprintf(errmsg, sizeof(errmsg), "%s", body);
}

/* ---- allocation --------------------------------------------------------- */

/* One block per loaded level: the level_t the caller sees, followed by the
 * storage its const pointers point at. */
typedef struct level_alloc {
    struct level_alloc *next;
    level_t pub;
    char name[LEVEL_NAME_MAX + 1];
    char author[LEVEL_NAME_MAX + 1];
    uint8_t ground[MAX_X];
    level_runway_t runways[LEVEL_MAX_RUNWAYS];
    level_target_t targets[MAX_TARG];
    level_point_t oxen[MAX_OXEN];
} level_alloc_t;

static level_alloc_t *loaded;

/* ---- validation --------------------------------------------------------- */

/* Where each entry came from, so a broken rule can name its line.  NULL when
 * the level did not come from a file. */
typedef struct {
    int runway[LEVEL_MAX_RUNWAYS];
    int target[MAX_TARG];
    int ox[MAX_OXEN];
} lines_t;

static int line_of(const int *lines, int i)
{
    return lines ? lines[i] : 0;
}

/* The rules from doc/LEVEL_FORMAT.md, applied to a finished level.  Heights
 * and enumerations are checked here as well as at parse time, because
 * level_save() runs this over a level nobody parsed. */
static int validate(const level_t *lv, const lines_t *ln)
{
    if (!lv->name || !lv->name[0]) {
        seterr(0, "a level needs a name");
        return -1;
    }
    if (strlen(lv->name) > LEVEL_NAME_MAX) {
        seterr(0, "the name is longer than %d bytes", LEVEL_NAME_MAX);
        return -1;
    }
    if (lv->author && strlen(lv->author) > LEVEL_NAME_MAX) {
        seterr(0, "the author is longer than %d bytes", LEVEL_NAME_MAX);
        return -1;
    }
    if (lv->format != LEVEL_FORMAT_VERSION) {
        seterr(0, "format version %u, expected %d",
               lv->format, LEVEL_FORMAT_VERSION);
        return -1;
    }
    if (lv->width != MAX_X || lv->height != MAX_Y) {
        seterr(0, "size must be %d %d in version %d",
               MAX_X, MAX_Y, LEVEL_FORMAT_VERSION);
        return -1;
    }
    if (!lv->ground) {
        seterr(0, "the level has no terrain");
        return -1;
    }

    for (int x = 0; x < lv->width; x++) {
        int h = lv->ground[x];
        if (h < LEVEL_GROUND_MIN || h > LEVEL_GROUND_MAX) {
            seterr(0, "height %d at column %d is outside %d..%d",
                   h, x, LEVEL_GROUND_MIN, LEVEL_GROUND_MAX);
            return -1;
        }
    }

    /* Runways. */
    if (lv->n_runways < LEVEL_MIN_RUNWAYS) {
        seterr(0, "a level needs at least %d runways, found %d",
               LEVEL_MIN_RUNWAYS, lv->n_runways);
        return -1;
    }
    if (lv->n_runways > LEVEL_MAX_RUNWAYS) {
        seterr(0, "more than %d runways", LEVEL_MAX_RUNWAYS);
        return -1;
    }
    for (int i = 0; i < lv->n_runways; i++) {
        const level_runway_t *rw = &lv->runways[i];
        int at = line_of(ln ? ln->runway : NULL, i);

        if (rw->orient > 1) {
            seterr(at, "runway orientation %u, expected 0 or 1", rw->orient);
            return -1;
        }
        if (rw->x + LEVEL_RUNWAY_SPAN > lv->width) {
            seterr(at, "the runway at %u runs off the end of the world",
                   rw->x);
            return -1;
        }
        int lo = LEVEL_GROUND_MAX, hi = 0;
        for (int x = rw->x; x < rw->x + LEVEL_RUNWAY_SPAN; x++) {
            int h = lv->ground[x];
            if (h < lo) lo = h;
            if (h > hi) hi = h;
        }
        if (hi - lo > LEVEL_RUNWAY_SLOP) {
            seterr(at, "the runway at %u is not flat: %d..%d over its "
                       "%d columns, more than %d apart",
                   rw->x, lo, hi, LEVEL_RUNWAY_SPAN, LEVEL_RUNWAY_SLOP);
            return -1;
        }
    }

    /* Buildings. */
    if (lv->n_targets > MAX_TARG) {
        seterr(0, "more than %d buildings", MAX_TARG);
        return -1;
    }
    for (int i = 0; i < lv->n_targets; i++) {
        const level_target_t *tg = &lv->targets[i];
        int at = line_of(ln ? ln->target : NULL, i);

        if (tg->kind > TARGET_HANGAR) {
            seterr(at, "building kind %u, expected 0..%d",
                   tg->kind, TARGET_HANGAR);
            return -1;
        }
        if (tg->x + LEVEL_TARGET_WIDTH > lv->width) {
            seterr(at, "the building at %u runs off the end of the world",
                   tg->x);
            return -1;
        }
        for (int j = 0; j < i; j++) {
            int gap = (int)tg->x - (int)lv->targets[j].x;
            if (gap < 0)
                gap = -gap;
            if (gap < LEVEL_TARGET_WIDTH) {
                seterr(at, "the buildings at %u and %u are less than %d "
                           "columns apart",
                       lv->targets[j].x, tg->x, LEVEL_TARGET_WIDTH);
                return -1;
            }
        }
    }

    /* Oxen. */
    if (lv->n_oxen > MAX_OXEN) {
        seterr(0, "more than %d oxen", MAX_OXEN);
        return -1;
    }
    for (int i = 0; i < lv->n_oxen; i++) {
        const level_point_t *ox = &lv->oxen[i];
        if (ox->x >= lv->width || ox->y >= lv->height) {
            seterr(line_of(ln ? ln->ox : NULL, i),
                   "the ox at %u,%u is outside the world", ox->x, ox->y);
            return -1;
        }
    }

    return 0;
}

/* ---- parsing ------------------------------------------------------------ */

/* Cut the next whitespace-delimited token off the front of *p. */
static char *token(char **p)
{
    char *s = *p;

    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s) {
        *p = s;
        return NULL;
    }

    char *start = s;
    while (*s && *s != ' ' && *s != '\t')
        s++;
    if (*s)
        *s++ = '\0';
    *p = s;
    return start;
}

static bool number(const char *s, long lo, long hi, long *out)
{
    if (!s || !*s)
        return false;

    int saved = errno;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    bool ok = errno == 0 && end != s && !*end && v >= lo && v <= hi;
    errno = saved;

    if (ok)
        *out = v;
    return ok;
}

/* The remainder of a line, for the free-text keys, trimmed both ends. */
static char *rest(char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    char *end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    *end = '\0';
    return p;
}

/* "count:height", one terrain run. */
static bool parse_run(const char *tok, long *count, long *height)
{
    const char *colon = strchr(tok, ':');
    if (!colon || colon == tok)
        return false;

    char head[16];
    size_t n = (size_t)(colon - tok);
    if (n >= sizeof(head))
        return false;
    memcpy(head, tok, n);
    head[n] = '\0';

    return number(head, 1, MAX_X, count) &&
           number(colon + 1, LEVEL_GROUND_MIN, LEVEL_GROUND_MAX, height);
}

int level_load(const char *path, level_t **out)
{
    errmsg[0] = '\0';

    if (!path || !out) {
        seterr(0, "no path to load");
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        int e = errno;
        seterr(0, "%s: %s", path, strerror(e));
        errno = e;
        return -1;
    }

    level_alloc_t *a = calloc(1, sizeof(*a));
    lines_t *ln = calloc(1, sizeof(*ln));
    char *line = NULL;
    size_t cap = 0;
    if (!a || !ln) {
        free(a);
        free(ln);
        fclose(f);
        seterr(0, "out of memory");
        errno = ENOMEM;
        return -1;
    }

    bool seen_magic = false, have_size = false;
    int width = MAX_X, height = MAX_Y;   /* until "size" says otherwise */
    int n_ground = 0, n_runways = 0, n_targets = 0, n_oxen = 0;
    int lineno = 0, bad = 0;
    uint32_t seed = 0;
    ssize_t len;

#define REJECT(...) do { seterr(lineno, __VA_ARGS__); bad = 1; } while (0)

    while (!bad && (len = getline(&line, &cap, f)) >= 0) {
        lineno++;
        line[strcspn(line, "\r\n")] = '\0';

        char *p = line;
        char *key = token(&p);
        if (!key || key[0] == '#')
            continue;

        if (!seen_magic) {
            long v;
            if (strcmp(key, LEVEL_MAGIC)) {
                REJECT("not a barnstormer level file");
                break;
            }
            if (!number(token(&p), 0, 1 << 20, &v)) {
                REJECT("the header has no version number");
                break;
            }
            if (v != LEVEL_FORMAT_VERSION) {
                REJECT("format version %ld, expected %d",
                       v, LEVEL_FORMAT_VERSION);
                break;
            }
            if (token(&p)) {
                REJECT("trailing text after the version");
                break;
            }
            seen_magic = true;
            continue;
        }

        if (!strcmp(key, "name") || !strcmp(key, "author")) {
            char *text = rest(p);
            if (!*text) {
                REJECT("%s is empty", key);
                break;
            }
            if (strlen(text) > LEVEL_NAME_MAX) {
                REJECT("%s is longer than %d bytes", key, LEVEL_NAME_MAX);
                break;
            }
            char *dst = key[0] == 'n' ? a->name : a->author;
            snprintf(dst, LEVEL_NAME_MAX + 1, "%s", text);
            continue;
        }

        if (!strcmp(key, "seed")) {
            long v;
            if (!number(token(&p), 0, 0xFFFFFFFFL, &v)) {
                REJECT("seed is not a number in 0..4294967295");
                break;
            }
            seed = (uint32_t)v;
        } else if (!strcmp(key, "size")) {
            long w, h;
            if (!number(token(&p), 1, 0xFFFF, &w) ||
                !number(token(&p), 1, 0xFFFF, &h)) {
                REJECT("size needs a width and a height");
                break;
            }
            /* Checked here rather than left to validate(), so that a level
             * built for a world of another size is refused at the line that
             * says so instead of at the terrain that follows it. */
            if (w != MAX_X || h != MAX_Y) {
                REJECT("size must be %d %d in version %d",
                       MAX_X, MAX_Y, LEVEL_FORMAT_VERSION);
                break;
            }
            width = (int)w;
            height = (int)h;
            have_size = true;
        } else if (!strcmp(key, "ground")) {
            char *tok;
            bool any = false;
            while ((tok = token(&p)) != NULL) {
                long count, h;
                if (!parse_run(tok, &count, &h)) {
                    REJECT("\"%s\" is not count:height with a height "
                           "in %d..%d", tok, LEVEL_GROUND_MIN,
                           LEVEL_GROUND_MAX);
                    break;
                }
                if (n_ground + count > MAX_X) {
                    REJECT("the terrain is longer than %d columns", MAX_X);
                    break;
                }
                memset(a->ground + n_ground, (int)h, (size_t)count);
                n_ground += (int)count;
                any = true;
            }
            if (bad)
                break;
            if (!any) {
                REJECT("a ground line with no runs on it");
                break;
            }
            continue;
        } else if (!strcmp(key, "runway")) {
            long x, orient;
            if (n_runways >= LEVEL_MAX_RUNWAYS) {
                REJECT("more than %d runways", LEVEL_MAX_RUNWAYS);
                break;
            }
            if (!number(token(&p), 0, 0xFFFF, &x) ||
                !number(token(&p), 0, 1, &orient)) {
                REJECT("a runway needs an x and an orientation, 0 or 1");
                break;
            }
            a->runways[n_runways].x = (uint16_t)x;
            a->runways[n_runways].orient = (uint8_t)orient;
            ln->runway[n_runways] = lineno;
            n_runways++;
        } else if (!strcmp(key, "target")) {
            long x, kind;
            if (n_targets >= MAX_TARG) {
                REJECT("more than %d buildings", MAX_TARG);
                break;
            }
            if (!number(token(&p), 0, 0xFFFF, &x) ||
                !number(token(&p), 0, TARGET_HANGAR, &kind)) {
                REJECT("a building needs an x and a kind, 0..%d",
                       TARGET_HANGAR);
                break;
            }
            a->targets[n_targets].x = (uint16_t)x;
            a->targets[n_targets].kind = (uint8_t)kind;
            ln->target[n_targets] = lineno;
            n_targets++;
        } else if (!strcmp(key, "ox")) {
            long x, y;
            if (n_oxen >= MAX_OXEN) {
                REJECT("more than %d oxen", MAX_OXEN);
                break;
            }
            if (!number(token(&p), 0, 0xFFFF, &x) ||
                !number(token(&p), 0, 0xFFFF, &y)) {
                REJECT("an ox needs an x and a y");
                break;
            }
            a->oxen[n_oxen].x = (uint16_t)x;
            a->oxen[n_oxen].y = (uint16_t)y;
            ln->ox[n_oxen] = lineno;
            n_oxen++;
        } else {
            continue;       /* forward compatibility: skip what we don't know */
        }

        /* Every fixed-arity key lands here with its arguments consumed. */
        if (token(&p)) {
            REJECT("trailing text after the %s", key);
            break;
        }
    }

    if (!bad && ferror(f)) {
        int e = errno;
        seterr(0, "%s: %s", path, strerror(e));
        free(a);
        free(ln);
        free(line);
        fclose(f);
        errno = e;
        return -1;
    }

    if (!bad && !seen_magic)
        REJECT("not a barnstormer level file");
    if (!bad && !have_size)
        REJECT("the file has no size line");
    if (!bad && n_ground != width)
        REJECT("the terrain covers %d of the %d columns", n_ground, width);

#undef REJECT

    free(line);
    fclose(f);

    if (bad) {
        free(a);
        free(ln);
        errno = EINVAL;
        return -1;
    }

    a->pub.name      = a->name;
    a->pub.author    = a->author[0] ? a->author : NULL;
    a->pub.format    = LEVEL_FORMAT_VERSION;
    a->pub.width     = (uint16_t)width;
    a->pub.height    = (uint16_t)height;
    a->pub.rand_seed = seed;
    a->pub.ground    = a->ground;
    a->pub.runways   = a->runways;
    a->pub.n_runways = n_runways;
    a->pub.targets   = a->targets;
    a->pub.n_targets = n_targets;
    a->pub.oxen      = a->oxen;
    a->pub.n_oxen    = n_oxen;

    if (validate(&a->pub, ln) < 0) {
        free(a);
        free(ln);
        errno = EINVAL;
        return -1;
    }
    free(ln);

    a->next = loaded;
    loaded = a;
    *out = &a->pub;
    return 0;
}

int level_free(level_t *lvl)
{
    errmsg[0] = '\0';
    if (!lvl) {
        errno = EINVAL;
        return -1;
    }

    for (level_alloc_t **pp = &loaded; *pp; pp = &(*pp)->next) {
        if (&(*pp)->pub != lvl)
            continue;
        level_alloc_t *a = *pp;
        *pp = a->next;
        free(a);
        return 0;
    }

    /* A built-in level, or one already freed.  Either way, not ours. */
    seterr(0, "that level was not loaded from a file");
    errno = EINVAL;
    return -1;
}

/* ---- saving ------------------------------------------------------------- */

/* The canonical serialisation: no comments, terrain wrapped at a fixed
 * column, sections in a fixed order.  doc/LEVEL_FORMAT.md's hash is defined
 * over exactly these bytes. */
static void emit(FILE *f, const level_t *lv)
{
    fprintf(f, "%s %d\n", LEVEL_MAGIC, LEVEL_FORMAT_VERSION);
    fprintf(f, "name %s\n", lv->name);
    if (lv->author && lv->author[0])
        fprintf(f, "author %s\n", lv->author);
    fprintf(f, "seed %u\n", lv->rand_seed);
    fprintf(f, "size %u %u\n\n", lv->width, lv->height);

    int col = 0;
    for (int x = 0; x < lv->width; ) {
        int h = lv->ground[x];
        int run = 1;
        while (x + run < lv->width && lv->ground[x + run] == h)
            run++;
        x += run;

        if (col == 0)
            col = fprintf(f, "ground");
        col += fprintf(f, " %d:%d", run, h);
        if (col >= GROUND_WRAP) {
            fputc('\n', f);
            col = 0;
        }
    }
    if (col)
        fputc('\n', f);

    if (lv->n_runways)
        fputc('\n', f);
    for (int i = 0; i < lv->n_runways; i++)
        fprintf(f, "runway %u %u\n", lv->runways[i].x, lv->runways[i].orient);

    if (lv->n_targets)
        fputc('\n', f);
    for (int i = 0; i < lv->n_targets; i++)
        fprintf(f, "target %u %u\n", lv->targets[i].x, lv->targets[i].kind);

    if (lv->n_oxen)
        fputc('\n', f);
    for (int i = 0; i < lv->n_oxen; i++)
        fprintf(f, "ox %u %u\n", lv->oxen[i].x, lv->oxen[i].y);
}

int level_save(const char *path, const level_t *lvl)
{
    errmsg[0] = '\0';

    if (!path || !lvl) {
        seterr(0, "nothing to save");
        errno = EINVAL;
        return -1;
    }
    if (validate(lvl, NULL) < 0) {
        errno = EINVAL;
        return -1;
    }

    /* Write beside the target and rename, so an interrupted write cannot
     * truncate a level that was already there. */
    size_t n = strlen(path) + sizeof(".tmp");
    char *tmp = malloc(n);
    if (!tmp) {
        seterr(0, "out of memory");
        errno = ENOMEM;
        return -1;
    }
    snprintf(tmp, n, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        int e = errno;
        seterr(0, "%s: %s", tmp, strerror(e));
        free(tmp);
        errno = e;
        return -1;
    }

    emit(f, lvl);

    if (fflush(f) != 0 || ferror(f) || fclose(f) != 0) {
        int e = errno ? errno : EIO;
        seterr(0, "%s: %s", tmp, strerror(e));
        unlink(tmp);
        free(tmp);
        errno = e;
        return -1;
    }
    if (rename(tmp, path) != 0) {
        int e = errno;
        seterr(0, "%s: %s", path, strerror(e));
        unlink(tmp);
        free(tmp);
        errno = e;
        return -1;
    }

    free(tmp);
    return 0;
}
