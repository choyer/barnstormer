/*
 * objects.c -- object pool, the x-sorted list, and fixed-point motion.
 *
 * The original kept a free list threaded through a fixed array of 100 objects
 * plus a second list sorted by x that the collision sweep walks.  Both are
 * reproduced here; the bounded pool is a feature, not a limitation, because it
 * caps how much debris a single explosion can spawn.
 */
#include <string.h>

#include "game.h"

const int sw_sintab[ANGLES] = {
       0,   98,  181,  237,
     256,  237,  181,   98,
       0,  -98, -181, -237,
    -256, -237, -181,  -98,
};

object_t *obj_alloc(game_t *g)
{
    object_t *ob = g->free_list;
    if (!ob)
        return NULL;
    g->free_list = ob->next;

    int index = ob->index;
    memset(ob, 0, sizeof(*ob));
    ob->index = index;
    ob->alive = true;
    ob->sprite_set = -1;

    ob->next = NULL;
    ob->prev = g->bot;
    if (g->bot)
        g->bot->next = ob;
    else
        g->top = ob;
    g->bot = ob;
    return ob;
}

void obj_free(game_t *g, object_t *ob)
{
    if (!ob->alive)
        return;
    ob->alive = false;
    obj_xremove(g, ob);

    if (ob->prev)
        ob->prev->next = ob->next;
    else
        g->top = ob->next;

    if (ob->next)
        ob->next->prev = ob->prev;
    else
        g->bot = ob->prev;

    /* Anything still pointing at this object must forget it, or the
     * autopilot and missile seekers will chase freed memory. */
    for (int i = 0; i < MAX_PLYR; i++)
        if (g->nearest[i] == ob)
            g->nearest[i] = NULL;
    for (object_t *o = g->top; o; o = o->next) {
        if (o->target == ob) o->target = NULL;
        if (o->mfiring == ob) o->mfiring = NULL;
        if (o->shot_at == ob) o->shot_at = NULL;
        if (o->owner == ob) o->owner = o;
    }

    ob->next = g->free_list;
    ob->prev = NULL;
    g->free_list = ob;
}

/* ---- x-sorted list ----------------------------------------------------- */

void obj_xinsert(game_t *g, object_t *ob)
{
    object_t *s = g->xhead.xnext;
    while (s != &g->xtail && s->x <= ob->x)
        s = s->xnext;
    /* insert before s */
    ob->xnext = s;
    ob->xprev = s->xprev;
    s->xprev->xnext = ob;
    s->xprev = ob;
}

void obj_xremove(game_t *g, object_t *ob)
{
    (void)g;
    if (!ob->xnext && !ob->xprev)
        return;
    ob->xnext->xprev = ob->xprev;
    ob->xprev->xnext = ob->xnext;
    ob->xnext = ob->xprev = NULL;
}

/* ---- fixed-point motion ------------------------------------------------
 *
 * setdxdy() takes a velocity scaled by 256 (speed * sine-table entry) and
 * splits it into an integer pixel step and a 16-bit fraction, exactly as the
 * original's assembly did with CH/CL.  move_xy() then adds velocity to
 * position with carry, so sub-pixel speeds accumulate instead of being lost.
 */

void obj_set_dxdy(object_t *ob, int xval, int yval)
{
    ob->dx  = (int8_t)((xval >> 8) & 0xFF);
    ob->ldx = (uint16_t)((xval & 0xFF) << 8);
    ob->dy  = (int8_t)((yval >> 8) & 0xFF);
    ob->ldy = (uint16_t)((yval & 0xFF) << 8);
}

void obj_move_xy(object_t *ob, int *x, int *y)
{
    uint32_t acc = (uint32_t)ob->lx + (uint32_t)ob->ldx;
    ob->lx = (uint16_t)acc;
    ob->x += ob->dx + (int)(acc >> 16);

    acc = (uint32_t)ob->ly + (uint32_t)ob->ldy;
    ob->ly = (uint16_t)acc;
    ob->y += ob->dy + (int)(acc >> 16);

    if (x) *x = ob->x;
    if (y) *y = ob->y;
}
