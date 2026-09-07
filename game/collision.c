/*
 * collision.c -- broad phase over the x-sorted list, then per-pixel tests.
 *
 * The original drew each pair of sprites into a scratch video page and looked
 * for overlapping ink; we compare the bitmaps directly, which is the same test
 * without the detour through video memory.  Ground contact is checked the same
 * way: for every column the sprite covers, is there sprite ink at or below the
 * terrain height?
 */
#include <string.h>

#include "game.h"
#include "sprites.h"

typedef struct { object_t *victim, *agent; } kill_t;

/* Reads one pixel of an object's current sprite in sprite-local coordinates
 * (column from the left, row down from the top). */
static int obj_pixel(const object_t *ob, int col, int row)
{
    if (col < 0 || row < 0 || col >= ob->symw || row >= ob->symh)
        return 0;
    if (ob->sprite_set < 0)
        return ob->sprite_frame ? ob->sprite_frame : 3;   /* point sprite  */
    const sprite_set_t *s = &sw_sprite_sets[ob->sprite_set];
    if (col >= s->w || row >= s->h)
        return 0;
    return sprite_frame(ob->sprite_set, ob->sprite_frame)[row * s->w + col];
}

static bool sprites_overlap(const object_t *a, const object_t *b)
{
    /* World-space intersection of the two bounding boxes.  y counts up, and
     * (x, y) is the top-left corner, so the box is y-symh+1 .. y. */
    int x0 = a->x > b->x ? a->x : b->x;
    int x1 = (a->x + a->symw) < (b->x + b->symw)
                 ? (a->x + a->symw) : (b->x + b->symw);
    int yhi = a->y < b->y ? a->y : b->y;
    int alo = a->y - a->symh + 1, blo = b->y - b->symh + 1;
    int ylo = alo > blo ? alo : blo;

    for (int x = x0; x < x1; x++)
        for (int y = ylo; y <= yhi; y++)
            if (obj_pixel(a, x - a->x, a->y - y) &&
                obj_pixel(b, x - b->x, b->y - y))
                return true;
    return false;
}

static bool hits_ground(const game_t *g, const object_t *ob)
{
    for (int x = ob->x; x < ob->x + ob->symw; x++) {
        int row = ob->y - game_ground(g, x);
        if (row < 0)
            return true;                  /* terrain rises past the top    */
        if (row >= ob->symh)
            continue;                     /* ground is below the sprite    */
        if (obj_pixel(ob, x - ob->x, row))
            return true;
    }
    return false;
}

/* Bombs and crashing aircraft dig a shallow, permanent hollow. */
static void crater(game_t *g, const object_t *ob)
{
    static const int depth[8] = { 1, 2, 2, 3, 3, 2, 2, 1 };
    int xmin = ob->x + (ob->symw - 8) / 2;

    for (int i = 0; i < 8; i++) {
        int x = xmin + i;
        if (x < 0 || x >= MAX_X)
            continue;
        int floor = g->level->ground[x] - 20;
        if (floor < 20)
            floor = 20;
        int ymin = g->ground[x] - depth[i] + 1;
        if (ymin <= floor)
            ymin = floor + 1;
        g->ground[x] = (uint8_t)(ymin - 1);
    }
    g->terrain_dirty = true;
}

/* Did `agent` do enough to `ob` for it to count as a kill worth points? */
static bool score_penalty(game_t *g, objtype_t ttype, object_t *agent,
                          int points)
{
    bool deliberate =
        ttype == OBJ_SHOT || ttype == OBJ_BOMB || ttype == OBJ_MISSILE ||
        (ttype == OBJ_PLANE && agent && !agent->athome &&
         (agent->state == ST_FLYING || agent->state == ST_WOUNDED ||
          (agent->state == ST_FALLING && agent->hitcount == FALLCOUNT)));
    if (deliberate) {
        sw_score(g, agent, points);
        return true;
    }
    return false;
}

static void kill_object(game_t *g, object_t *ob, object_t *agent)
{
    objtype_t ttype = agent ? agent->type : OBJ_GROUND;

    /* Wildlife bounces off everything except aircraft. */
    if ((ttype == OBJ_BIRD || ttype == OBJ_FLOCK) && ob->type != OBJ_PLANE)
        return;

    switch (ob->type) {
    case OBJ_BOMB:
    case OBJ_MISSILE:
        sw_init_explosion(g, ob, false);
        ob->life = -1;
        if (!agent)
            crater(g, ob);
        sw_sound_stop(g, ob);
        return;

    case OBJ_SHOT:
        ob->life = 1;
        return;

    case OBJ_STARBURST:
        if (ttype == OBJ_MISSILE || ttype == OBJ_BOMB || !agent)
            ob->life = 1;
        return;

    case OBJ_EXPLOSION:
        if (!agent) {
            ob->life = 1;
            sw_sound_stop(g, ob);
        }
        return;

    case OBJ_TARGET:
        if (ob->state != ST_STANDING)
            return;
        if (ttype == OBJ_EXPLOSION || ttype == OBJ_STARBURST)
            return;

        /* Bullets chip away; later games need more of them. */
        if (ttype == OBJ_SHOT &&
            (ob->hitcount += TARGHITCOUNT) <= TARGHITCOUNT * (g->gamenum + 1))
            return;

        ob->state = ST_FINISHED;
        sw_init_explosion(g, ob, false);
        sw_score(g, ob, ob->orient == TARGET_FUEL ? 200 : 100);
        if (ob->clr >= 1 && ob->clr <= 2 && !--g->numtarg[ob->clr - 1])
            sw_end_game(g, ob->clr);
        return;

    case OBJ_PLANE: {
        objstate_t state = ob->state;
        if (state == ST_CRASHED)
            return;
        if (g->endsts[ob->index] == END_WINNER)
            return;
        /* Flares are harmless, and birds cannot hurt a parked aircraft. */
        if (ttype == OBJ_STARBURST || (ttype == OBJ_BIRD && ob->athome))
            return;

        if (!agent) {                     /* flew into the scenery         */
            if (state == ST_FALLING) {
                sw_sound_stop(g, ob);
                sw_init_explosion(g, ob, true);
                crater(g, ob);
            } else if (state != ST_FINISHED) {
                sw_score(g, ob, 50);
                sw_init_explosion(g, ob, true);
                crater(g, ob);
            }
            sw_plane_crash(g, ob);
            return;
        }

        if (state == ST_FINISHED)
            return;

        if (state == ST_FALLING) {
            if (ob->index == g->player) {
                if (ttype == OBJ_SHOT)
                    g->shothole++;
                else if (ttype == OBJ_BIRD || ttype == OBJ_FLOCK)
                    g->splatbird++;
            }
            return;
        }

        if (ttype == OBJ_SHOT || ttype == OBJ_BIRD ||
            ttype == OBJ_OX || ttype == OBJ_FLOCK) {
            if (ob->index == g->player) {
                if (ttype == OBJ_SHOT)     g->shothole++;
                else if (ttype == OBJ_OX)  g->splatox++;
                else                       g->splatbird++;
            }
            /* Small stuff wounds rather than kills. */
            if (state == ST_FLYING)  { ob->state = ST_WOUNDED;    return; }
            if (state == ST_STALLED) { ob->state = ST_WOUNDSTALL; return; }
        } else {
            sw_init_explosion(g, ob, true);
            if (ttype == OBJ_PLANE) {
                /* Mid-air collision: the wrecks tumble away together. */
                ob->dx = ((ob->dx + agent->dx) >> 1) + ((g->countmove & 1) ? 2 : -2);
                ob->dy = ((ob->dy + agent->dy) >> 1) + ((g->countmove & 1) ? 1 : -1);
            }
        }

        sw_plane_hit(ob);
        sw_score(g, ob, 50);
        return;
    }

    case OBJ_BIRD:
        ob->life = score_penalty(g, ttype, agent, 25) ? -1 : -2;
        return;

    case OBJ_FLOCK:
        if (ttype != OBJ_FLOCK && ttype != OBJ_BIRD &&
            ob->state == ST_FLYING) {
            for (int i = 0; i < 8; i++)
                sw_init_bird(g, ob, i);
            ob->life = -1;
            ob->state = ST_FINISHED;
        }
        return;

    case OBJ_OX:
        if (ob->state != ST_STANDING)
            return;
        if (ttype == OBJ_EXPLOSION || ttype == OBJ_STARBURST)
            return;
        score_penalty(g, ttype, agent, 200);
        ob->state = ST_FINISHED;
        return;

    default:
        return;
    }
}

void sw_collide(game_t *g)
{
    kill_t kills[MAX_OBJS * 2];
    int nkill = 0;

    for (object_t *ob = g->xhead.xnext; ob != &g->xtail; ob = ob->xnext) {
        int xmax = ob->x + ob->symw - 1;
        int ymax = ob->y;
        int ymin = ymax - ob->symh + 1;

        for (object_t *obp = ob->xnext;
             obp != &g->xtail && obp->x <= xmax;
             obp = obp->xnext) {
            if (obp->y < ymin || (obp->y - obp->symh + 1) > ymax)
                continue;

            /* Aircraft that have left the game, and explosion debris
             * meeting other debris, are not worth testing. */
            if ((ob->type == OBJ_PLANE && ob->state == ST_FINISHED) ||
                (obp->type == OBJ_PLANE && obp->state == ST_FINISHED) ||
                (ob->type == OBJ_EXPLOSION && obp->type == OBJ_EXPLOSION))
                continue;

            if (!sprites_overlap(ob, obp))
                continue;

            if (nkill + 2 <= (int)(sizeof(kills) / sizeof(kills[0]))) {
                kills[nkill].victim = ob;  kills[nkill++].agent = obp;
                kills[nkill].victim = obp; kills[nkill++].agent = ob;
            }
        }

        /* Only bother with terrain when the object is low enough. */
        bool low =
            (ob->type == OBJ_PLANE && ob->state != ST_FINISHED &&
             ob->state != ST_WAITING &&
             ob->y < game_ground(g, ob->x + 8) + 24) ||
            ((ob->type == OBJ_BOMB || ob->type == OBJ_MISSILE) &&
             ob->y < game_ground(g, ob->x + 4) + 12);

        if (low && hits_ground(g, ob) &&
            nkill < (int)(sizeof(kills) / sizeof(kills[0]))) {
            kills[nkill].victim = ob;
            kills[nkill++].agent = NULL;
        }
    }

    for (int i = 0; i < nkill; i++)
        if (kills[i].victim->alive)
            kill_object(g, kills[i].victim, kills[i].agent);
}
