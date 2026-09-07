/*
 * autopilot.c -- the computer pilots, and the missile seeker head.
 *
 * The strategy is the original's and is charmingly direct: each tick the pilot
 * simulates the three moves it could make (stick back, centred, forward),
 * scores each by how much closer it gets to its objective, discards any that
 * would fly it into the scenery, and takes the best.  Gunnery is a separate
 * short forward simulation of the bullet and the target together.
 */
#include <string.h>

#include "game.h"

/* Squared distance while the target is near, and a negative cheap estimate
 * once it is far away.  Callers rely on the sign to tell the two apart. */
int sw_range(int x, int y, int ax, int ay)
{
    int dy = sw_abs(y - ay);
    dy += dy >> 1;                    /* vertical separation counts more   */
    int dx = sw_abs(x - ax);

    if (dx < 125 && dy < 125)
        return dx * dx + dy * dy;

    if (dx < dy) { int t = dx; dx = dy; dy = t; }
    return -(((7 * dx) + (dy << 2)) >> 3);
}

/* Per-pilot scratch shared by aim() and shoot(), as in the original. */
static object_t sim;
static int courseadj;
static int targ_lo, targ_hi;

static void clear_targs(void) { targ_lo = -2; }

/* Which static targets are close enough to x to be worth avoiding. */
static void test_targs(game_t *g, int x)
{
    int xl = x - 32 - g->gmaxspeed;
    int xr = x + 32 + g->gmaxspeed;

    targ_lo = -1;
    targ_hi = 0;
    int i;
    for (i = 0; i < MAX_TARG + MAX_OXEN; i++)
        if (g->targets[i] && g->targets[i]->x >= xl) {
            targ_lo = i;
            break;
        }
    if (targ_lo == -1)
        return;
    for (; i < MAX_TARG + MAX_OXEN && g->targets[i] &&
           g->targets[i]->x < xr; i++)
        ;
    targ_hi = i - 1;
}

/* Would moving to (x, y) at altitude `alt` put us into terrain or a
 * building?  Anything above fifty pixels of clearance is safe. */
static bool would_crash(game_t *g, object_t *ob, int x, int y, int alt)
{
    if (alt > 50)
        return false;
    if (alt < 22)
        return true;

    if (targ_lo == -2)
        test_targs(g, ob->x);

    int xl = x - 32, xr = x + 32;
    for (int i = targ_lo; i >= 0 && i <= targ_hi; i++) {
        object_t *t = g->targets[i];
        if (!t)
            continue;
        if (t->x < xl)
            continue;
        if (t->x > xr)
            return false;
        int yt = t->y + (t->state == ST_STANDING ? 16 : 8);
        if (y <= yt)
            return true;
    }
    return false;
}

/* Fly the bullet and the target forward together.  Returns 0 for no shot,
 * 1 for a snapshot and 2 when there is time for a missile instead. */
static int shoot(game_t *g, object_t *targ)
{
    object_t bullet = sim;
    object_t mark = *targ;

    int nspeed = bullet.speed + BULSPEED;
    obj_set_dxdy(&bullet, nspeed * SW_COS(bullet.angle),
                          nspeed * SW_SIN(bullet.angle));
    bullet.x += 16 / 2;
    bullet.y -= 16 / 2;

    int nangle = mark.angle;
    nspeed = mark.speed;
    int rprev = AI_NEAR;

    for (int i = 0; i < BULLIFE; i++) {
        int bx, by, tx, ty;
        obj_move_xy(&bullet, &bx, &by);

        /* Assume the target holds whatever stick input it is using now. */
        if ((mark.state == ST_FLYING || mark.state == ST_WOUNDED) &&
            mark.flaps) {
            nangle += mark.orient ? -mark.flaps : mark.flaps;
            nangle = (nangle + ANGLES) % ANGLES;
            obj_set_dxdy(&mark, nspeed * SW_COS(nangle),
                                nspeed * SW_SIN(nangle));
        }
        obj_move_xy(&mark, &tx, &ty);

        int r = sw_range(bx, by, tx, ty);
        if (r < 0 || r > rprev)
            return 0;                /* opening the range: do not waste it */
        if (bx >= tx && bx <= tx + 16 - 1 && by <= ty && by >= ty - 16 + 1)
            return 1 + (i > BULLIFE / 3);
        (void)g;
    }
    return 0;
}

void sw_aim(game_t *g, object_t *ob, int ax, int ay, object_t *targ,
            bool longway)
{
    static const int cflaps[3] = { 0, -1, 1 };
    int crange[3], calt[3];
    bool ccrash[3];

    /* A stalled aircraft has exactly one job: get the nose down. */
    if ((ob->state == ST_STALLED || ob->state == ST_WOUNDSTALL) &&
        ob->angle != (3 * ANGLES / 4)) {
        ob->flaps = -1;
        ob->accel = MAX_THROTTLE;
        return;
    }

    int x = ob->x, y = ob->y;
    int dx = x - ax;

    if (sw_abs(dx) > 160) {
        /* Too far to steer at directly.  Either we are already closing, in
         * which case just hold an altitude, or we turn back via a waypoint. */
        if (ob->dx && ((dx < 0) == (ob->dx < 0))) {
            if (!ob->hitcount)
                ob->hitcount = 1 + (y > MAX_Y - 50);
            sw_aim(g, ob, x, (ob->hitcount == 1) ? y + 25 : y - 25, NULL, true);
            return;
        }
        ob->hitcount = 0;
        int ny = y + 100;
        if (ny > MAX_Y - 50 - courseadj)
            ny = MAX_Y - 50 - courseadj;
        sw_aim(g, ob, x + ((dx < 0) ? 150 : -150), ny, NULL, true);
        return;
    }
    if (!longway)
        ob->hitcount = 0;

    /* Within a few pixels the sixteen headings are too coarse, so nudge the
     * aircraft sideways to line up exactly. */
    if (ob->speed) {
        int dy = y - ay;
        if (dy && sw_abs(dy) < 6) {
            ob->y = (dy < 0) ? ++y : --y;
        } else if (dx && sw_abs(dx) < 6) {
            ob->x = (dx < 0) ? ++x : --x;
        }
    }

    sim = *ob;
    int nspeed = sim.speed + 1;
    if (nspeed > g->gmaxspeed && sim.type == OBJ_PLANE)
        nspeed = g->gmaxspeed;
    else if (nspeed < g->gminspeed)
        nspeed = g->gminspeed;

    clear_targs();
    for (int i = 0; i < 3; i++) {
        int nangle = (sim.angle + (sim.orient ? -cflaps[i] : cflaps[i])
                      + ANGLES) % ANGLES;
        int nx, ny;
        obj_set_dxdy(&sim, nspeed * SW_COS(nangle), nspeed * SW_SIN(nangle));
        obj_move_xy(&sim, &nx, &ny);
        crange[i] = sw_range(nx, ny, ax, ay);
        int gx = nx + 8;
        if (gx < 0) gx = 0;
        if (gx >= MAX_X) gx = MAX_X - 1;
        calt[i] = ny - g->level->ground[gx];
        ccrash[i] = would_crash(g, ob, nx, ny, calt[i]);
        sim = *ob;
    }

    if (targ) {
        int q = shoot(g, targ);
        if (q) {
            if (ob->missiles && q == 2)
                ob->mfiring = targ->athome ? ob : targ;
            else
                ob->firing = true;
        }
    }

    /* Prefer the move that closes the range; fall back to the least bad. */
    int n = 0, rmin = 32767;
    for (int i = 0; i < 3; i++)
        if (crange[i] >= 0 && crange[i] < rmin && !ccrash[i]) {
            rmin = crange[i];
            n = i;
        }
    if (rmin == 32767) {
        rmin = -32767;
        for (int i = 0; i < 3; i++)
            if (crange[i] < 0 && crange[i] > rmin && !ccrash[i]) {
                rmin = crange[i];
                n = i;
            }
    }

    if (ob->speed < g->gminspeed)
        ob->accel = MAX_THROTTLE;

    if (rmin != -32767) {
        if (ob->accel < MAX_THROTTLE)
            ob->accel++;
    } else {
        /* Nothing is safe: throttle back and climb away from the ground. */
        if (ob->accel)
            ob->accel--;
        n = 0;
        int best = calt[0];
        if (calt[1] > best) { best = calt[1]; n = 1; }
        if (calt[2] > best) { n = 2; }
    }

    ob->flaps = cflaps[n];
    if (ob->type == OBJ_PLANE && !ob->flaps && ob->speed)
        ob->orient = (ob->dx < 0);
}

void sw_go_home(game_t *g, object_t *ob)
{
    if (ob->athome)
        return;

    const object_t *base = &g->start[ob->index];
    courseadj = ((g->countmove & 0x1F) < 16) << 4;

    if (sw_abs(ob->x - base->x) < AI_HOME &&
        sw_abs(ob->y - base->y) < AI_HOME) {
        int slot = level_runway_slot(g->mode, ob->index);
        if (ob->ai)
            sw_init_comp(g, ob, slot);
        else
            sw_init_player(g, ob, slot);
        return;
    }
    sw_aim(g, ob, base->x, base->y, NULL, false);
}

static void attack(game_t *g, object_t *ob, object_t *targ)
{
    courseadj = ((g->countmove & 0x1F) < 16) << 4;
    if (targ->speed)
        /* Aim for a point behind the target so we arrive on its tail. */
        sw_aim(g, ob,
               targ->x - ((AI_CLOSE * SW_COS(targ->angle)) >> 8),
               targ->y - ((AI_CLOSE * SW_SIN(targ->angle)) >> 8),
               targ, false);
    else
        sw_aim(g, ob, targ->x, targ->y + 4, targ, false);
}

static void cruise(game_t *g, object_t *ob)
{
    courseadj = ((g->countmove & 0x1F) < 16) << 4;
    int orgx = g->start[ob->index].x;
    int ax = orgx;
    if (ax < MAX_X / 3)             ax = MAX_X / 3;
    else if (ax > 2 * MAX_X / 3)    ax = 2 * MAX_X / 3;
    sw_aim(g, ob, ax + courseadj, MAX_Y - 50 - (courseadj >> 1), NULL, false);
}

void sw_autopilot(game_t *g, object_t *ob)
{
    object_t *enemy = g->nearest[ob->index];
    if (enemy)
        attack(g, ob, enemy);
    else if (!ob->athome)
        cruise(g, ob);
    g->nearest[ob->index] = NULL;
}
