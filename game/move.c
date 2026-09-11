/*
 * move.c -- per-object update, transcribed from the original SWMOVE.C.
 *
 * The flight model is deliberately faithful: heading is one of sixteen
 * directions, airspeed chases a target derived from throttle plus a gravity
 * term indexed by heading, and losing airspeed below the stall floor flips the
 * aircraft onto its back until it has fallen far enough to fly again.
 */
#include <string.h>

#include "game.h"
#include "sprites.h"

/* Speed the aircraft is pulled towards, by heading.  Nose-down gains, and
 * climbing costs, exactly four units at the extremes. */
static const int gravity[ANGLES] = {
    0, -1, -2, -3, -4, -3, -2, -1,
    0,  1,  2,  3,  4,  3,  2,  1,
};

static bool move_plane(game_t *g, object_t *ob);

/* Eight-way heading of a ballistic object, used to pick a bomb sprite. */
static int sym_angle(const object_t *ob)
{
    int dx = ob->dx, dy = ob->dy;
    if (dx == 0)
        return dy > 0 ? 2 : 6;
    if (dx > 0)
        return dy < 0 ? 7 : (dy > 0 ? 1 : 0);
    return dy < 0 ? 5 : (dy > 0 ? 3 : 4);
}

/* Munitions arc over: every `life` ticks the horizontal component decays by
 * one and the vertical gains one, up to terminal velocity. */
static void adjust_fall(object_t *ob, int reset_life)
{
    if (--ob->life)
        return;
    if (ob->dy < 0) {
        if (ob->dx < 0) ob->dx++;
        else if (ob->dx > 0) ob->dx--;
    }
    if (ob->dy > -10)
        ob->dy--;
    ob->life = reset_life;
}

/* ---- control ----------------------------------------------------------- */

static void interpret(game_t *g, object_t *ob, uint16_t key)
{
    ob->flaps = 0;
    ob->bombing = ob->bfiring = false;
    ob->mfiring = NULL;
    ob->firing = false;

    objstate_t state = ob->state;
    if (state != ST_FLYING && state != ST_STALLED && state != ST_FALLING &&
        state != ST_WOUNDED && state != ST_WOUNDSTALL)
        return;

    if (state != ST_FALLING) {
        if (g->cur_endstat) {
            if (g->cur_endstat == END_LOSER && g->plyrplane)
                sw_go_home(g, ob);
            return;
        }

        if (key & K_QUIT) {
            ob->life = QUIT_LIFE;
            ob->home = false;
            if (ob->athome) {
                ob->state = state = ST_CRASHED;
                ob->hitcount = 0;
            }
            if (g->plyrplane)
                g->quit = true;
        }

        if ((key & K_HOME) && (state == ST_FLYING || state == ST_WOUNDED))
            ob->home = true;
    }

    /* A wounded aircraft answers the controls only every other tick. */
    if ((g->countmove & 1) ||
        (state != ST_WOUNDED && state != ST_WOUNDSTALL)) {
        if (key & K_FLAPU) { ob->flaps++; ob->home = false; }
        if (key & K_FLAPD) { ob->flaps--; ob->home = false; }
        if (key & K_FLIP)  { ob->orient = !ob->orient; ob->home = false; }
        if (key & K_DEACC) { if (ob->accel) ob->accel--; ob->home = false; }
        if (key & K_ACCEL) {
            if (ob->accel < MAX_THROTTLE) ob->accel++;
            ob->home = false;
        }
    }

    if (key & K_SHOT)       ob->firing = true;
    if (key & K_MISSILE)    ob->mfiring = ob;
    if (key & K_BOMB)       ob->bombing = true;
    if (key & K_STARBURST)  ob->bfiring = true;

    if (ob->home)
        sw_go_home(g, ob);
}

/* Tell each computer pilot which enemy aircraft is closest to it. */
static void near_plane(game_t *g, object_t *ob)
{
    int obx = ob->x;
    int obclr = ob->owner->clr;

    for (int i = 1; i < MAX_PLYR; i++) {
        object_t *obt = &g->pool[i];
        if (obt->type != OBJ_PLANE || !obt->ai)
            continue;
        if (obclr == obt->owner->clr)
            continue;

        /* In the three-opponent game each pilot patrols a stretch of map. */
        if (g->mode == PLAY_COMPUTER) {
            static const int lo[MAX_PLYR] = { 0, 1155, 0,    2089 };
            static const int hi[MAX_PLYR] = { 0, 2088, 1154, 10000 };
            if (obx < lo[i] || obx > hi[i])
                continue;
        }
        object_t *cur = g->nearest[i];
        if (!cur || sw_abs(obx - obt->x) < sw_abs(cur->x - obt->x))
            g->nearest[i] = ob;
    }
}

static bool topup(game_t *g, int *counter, int max)
{
    if (*counter >= max)
        return false;
    bool changed = false;
    if (max < 20) {
        if (!(g->countmove % 20)) { (*counter)++; changed = true; }
    } else {
        *counter += max / 100;
        changed = true;
    }
    if (*counter > max)
        *counter = max;
    return changed;
}

static void refuel(game_t *g, object_t *ob)
{
    topup(g, &ob->life, MAXFUEL);
    topup(g, &ob->rounds, MAXROUNDS);
    topup(g, &ob->bombs, MAXBOMBS);
    topup(g, &ob->missiles, MAXMISSILES);
    topup(g, &ob->bursts, MAXBURSTS);
}

/* ---- the aircraft ------------------------------------------------------ */

static bool move_plane(game_t *g, object_t *ob)
{
    int x, y;
    bool stalled = false;

    switch (ob->state) {
    case ST_FINISHED:
    case ST_WAITING:
        return false;

    case ST_CRASHED:
        ob->hitcount--;
        break;

    case ST_FALLING:
        /* The stick still has a little authority on the way down. */
        ob->hitcount -= 2;
        if (ob->dy < 0 && ob->dx) {
            if (ob->orient ^ (ob->dx < 0))
                ob->hitcount -= ob->flaps;
            else
                ob->hitcount += ob->flaps;
        }
        if (ob->hitcount <= 0) {
            if (ob->dy < 0) {
                if (ob->dx < 0) ob->dx++;
                else if (ob->dx > 0) ob->dx--;
                else ob->orient = !ob->orient;
            }
            if (ob->dy > -10)
                ob->dy--;
            ob->hitcount = FALLCOUNT;
        }
        ob->angle = sym_angle(ob) << 1;
        if (ob->dy <= 0)
            sw_sound_start(g, ob, S_FALLING);
        break;

    case ST_STALLED:
    case ST_WOUNDSTALL: {
        objstate_t recovered =
            (ob->state == ST_WOUNDSTALL) ? ST_WOUNDED : ST_FLYING;
        stalled = (ob->angle != (3 * ANGLES / 4)) || (ob->speed < g->gminspeed);
        if (!stalled)
            ob->state = recovered;
        goto controlled;
    }

    case ST_FLYING:
    case ST_WOUNDED:
        stalled = (ob->y >= MAX_Y);
        if (stalled) {
            if (g->mode == PLAY_NOVICE) {
                ob->angle = 3 * ANGLES / 4;   /* novices just level off    */
                stalled = false;
            } else {
                sw_plane_stall(g, ob);
            }
        }
        /* fall through */
    controlled: {
        if (g->goingsun && g->plyrplane)
            break;

        objstate_t st = ob->state;
        bool flyable = st == ST_FLYING || st == ST_STALLED ||
                       st == ST_WOUNDED || st == ST_WOUNDSTALL;

        if (ob->life <= 0 && !ob->athome && flyable) {
            sw_plane_hit(ob);
            sw_score(g, ob, 50);
            return move_plane(g, ob);
        }

        if (ob->firing)   sw_init_shot(g, ob, NULL);
        if (ob->bombing)  sw_init_bomb(g, ob);
        if (ob->mfiring)  sw_init_missile(g, ob);
        if (ob->bfiring)  sw_init_burst(g, ob);

        int nangle = ob->angle;
        int nspeed = ob->speed;
        bool update = false;

        if (ob->flaps) {
            nangle += ob->orient ? -ob->flaps : ob->flaps;
            nangle = (nangle + ANGLES) % ANGLES;
            update = true;
        }

        /* Airspeed is re-evaluated every fourth tick. */
        if (!(g->countmove & 3)) {
            if (!stalled && nspeed < g->gminspeed && g->mode != PLAY_NOVICE) {
                nspeed--;
                update = true;
            } else {
                int limit = g->gminspeed + ob->accel + gravity[nangle];
                if (nspeed < limit)      { nspeed++; update = true; }
                else if (nspeed > limit) { nspeed--; update = true; }
            }
        }

        if (update) {
            if (ob->athome) {
                nspeed = (ob->accel || ob->flaps) ? g->gminspeed : 0;
            } else if (nspeed <= 0 && !stalled) {
                if (g->mode == PLAY_NOVICE) {
                    nspeed = 1;
                } else {
                    sw_plane_stall(g, ob);
                    return move_plane(g, ob);
                }
            }

            ob->speed = nspeed;
            ob->angle = nangle;

            if (stalled) {
                ob->dx = 0;
                ob->ldx = ob->ldy = 0;
                ob->dy = -nspeed;
            } else {
                obj_set_dxdy(ob, nspeed * SW_COS(nangle),
                                 nspeed * SW_SIN(nangle));
            }
        }

        if (stalled && !--ob->hitcount) {
            /* Tumble: swap which way up we are and mirror the heading. */
            ob->orient = !ob->orient;
            ob->angle = ((3 * ANGLES / 2) - ob->angle) % ANGLES;
            ob->hitcount = STALLCOUNT;
        }

        if (!g->compplane)
            ob->life -= ob->speed;

        if (ob->speed)
            ob->athome = false;
        break;
    }

    default:
        break;
    }

    /* Pick this tick's sprite. */
    if (g->cur_endstat == END_WINNER && g->plyrplane && g->goingsun) {
        ob->sprite_set = SPRITE_WINNER;
        int f = g->endcount / 18;
        ob->sprite_frame = f < 0 ? 0 : (f > 3 ? 3 : f);
    } else if (ob->state == ST_FINISHED) {
        ob->sprite_set = -1;
    } else if (ob->state == ST_FALLING && !ob->dx && ob->dy < 0) {
        ob->sprite_set = SPRITE_PLANE_HIT;   /* pilot-less spin            */
        ob->sprite_frame = ob->orient;
    } else {
        ob->sprite_set = SPRITE_PLANE;
        ob->sprite_frame = ob->orient * ANGLES + ob->angle;
    }

    obj_move_xy(ob, &x, &y);

    if (x < 0)
        x = ob->x = 0;
    else if (x >= MAX_X - 16)
        x = ob->x = MAX_X - 16;

    if (!g->compplane &&
        (ob->state == ST_FLYING || ob->state == ST_STALLED ||
         ob->state == ST_WOUNDED || ob->state == ST_WOUNDSTALL) &&
        !g->endsts[g->player])
        near_plane(g, ob);

    obj_xremove(g, ob);
    obj_xinsert(g, ob);

    if (ob->bdelay)  ob->bdelay--;
    if (ob->mdelay)  ob->mdelay--;
    if (ob->bsdelay) ob->bsdelay--;

    if (!g->compplane && ob->athome && ob->state == ST_FLYING)
        refuel(g, ob);

    if (y < MAX_Y && y >= 0) {
        if (ob->state == ST_FALLING || ob->state == ST_WOUNDED ||
            ob->state == ST_WOUNDSTALL)
            sw_init_smoke(g, ob);
        return true;
    }
    return false;
}

static bool move_player(game_t *g, object_t *ob, uint16_t key)
{
    g->compplane = false;
    g->plyrplane = true;
    g->currobx = ob->index;

    g->cur_endstat = g->endsts[g->player];
    if (g->cur_endstat && --g->endcount <= 0) {
        if (g->quit) {
            g->over = true;                 /* message set by game_abandon */
        } else if (g->cur_endstat == END_WINNER) {
            game_restart(g);                /* on to the next, score kept  */
        } else {
            /* Out of aircraft.  This used to rebuild the world with the
             * score reset to zero, which meant a run had no end and nothing
             * to record.  It ends here now, score intact. */
            g->over = true;
            g->end_reason = RUN_CRASHED;
            g->over_msg = "GAME OVER";
        }
        return true;
    }

    interpret(g, ob, key);

    if (ob->state == ST_CRASHED && ob->hitcount <= 0) {
        ob->crashcnt++;
        if (g->cur_endstat != END_WINNER &&
            (ob->life <= QUIT_LIFE || ob->crashcnt >= g->maxcrash)) {
            if (!g->cur_endstat)
                sw_loser(g, ob);
        } else {
            sw_init_player(g, ob, level_runway_slot(g->mode, ob->index));
            if (g->cur_endstat == END_WINNER)
                sw_winner(g, ob);
        }
    }

    int oldx = ob->x;
    bool rc = move_plane(g, ob);

    /* The viewport is pinned to the player until they near either edge. */
    if (oldx <= SCR_LIMIT || oldx >= MAX_X - SCR_LIMIT) {
        g->dispdx = 0;
    } else {
        g->dispdx = ob->x - oldx;
        g->displx += g->dispdx;
        g->disprx += g->dispdx;
    }
    return rc;
}

static bool move_comp(game_t *g, object_t *ob)
{
    g->compplane = true;
    g->plyrplane = false;
    g->currobx = ob->index;

    ob->flaps = 0;
    ob->bfiring = ob->bombing = false;
    ob->mfiring = NULL;

    g->cur_endstat = g->endsts[ob->index];
    ob->firing = false;
    ob->shot_at = NULL;

    switch (ob->state) {
    case ST_WOUNDED:
    case ST_WOUNDSTALL:
        if (g->countmove & 1)
            break;
        /* fall through */
    case ST_FLYING:
    case ST_STALLED:
        if (g->cur_endstat)
            sw_go_home(g, ob);
        else
            sw_autopilot(g, ob);
        break;

    case ST_CRASHED:
        if (ob->hitcount <= 0 && !g->cur_endstat)
            sw_init_comp(g, ob, level_runway_slot(g->mode, ob->index));
        break;

    default:
        break;
    }

    return move_plane(g, ob);
}

/* ---- everything else --------------------------------------------------- */

static bool move_shot(game_t *g, object_t *ob)
{
    int x, y;
    obj_xremove(g, ob);
    if (!--ob->life) {
        obj_free(g, ob);
        return false;
    }
    obj_move_xy(ob, &x, &y);
    if (y >= MAX_Y || x < 0 || x >= MAX_X || y <= game_ground(g, x)) {
        obj_free(g, ob);
        return false;
    }
    obj_xinsert(g, ob);
    ob->sprite_set = -1;
    ob->sprite_frame = 3;         /* a single white pixel                  */
    return true;
}

static bool move_bomb(game_t *g, object_t *ob)
{
    int x, y;
    obj_xremove(g, ob);

    if (ob->life < 0) {
        obj_free(g, ob);
        return false;
    }
    adjust_fall(ob, BOMBLIFE);
    if (ob->dy <= 0)
        sw_sound_start(g, ob, S_BOMB);

    obj_move_xy(ob, &x, &y);
    if (y < 0 || x < 0 || x >= MAX_X) {
        sw_sound_stop(g, ob);
        obj_free(g, ob);
        return false;
    }
    ob->sprite_set = SPRITE_BOMB;
    ob->sprite_frame = sym_angle(ob);
    obj_xinsert(g, ob);
    return y < MAX_Y;
}

static bool move_missile(game_t *g, object_t *ob)
{
    int x, y;
    obj_xremove(g, ob);

    if (ob->life < 0) {
        obj_free(g, ob);
        return false;
    }

    if (ob->state == ST_FLYING) {
        object_t *obt = ob->target;
        /* Track every other tick, and prefer a flare over the aircraft. */
        if (obt && obt != ob->owner && (ob->life & 1)) {
            if (obt->target)
                obt = obt->target;
            sw_aim(g, ob, obt->x, obt->y, NULL, false);
            int angle = ob->angle = (ob->angle + ob->flaps + ANGLES) % ANGLES;
            obj_set_dxdy(ob, ob->speed * SW_COS(angle),
                             ob->speed * SW_SIN(angle));
        }
        obj_move_xy(ob, &x, &y);
        if (!--ob->life || y >= (MAX_Y * 3) / 2) {
            ob->state = ST_FALLING;   /* out of fuel: it just tumbles       */
            ob->life++;
        }
    } else {
        adjust_fall(ob, BOMBLIFE);
        ob->angle = (ob->angle + 1) % ANGLES;
        obj_move_xy(ob, &x, &y);
    }

    if (y < 0 || x < 0 || x >= MAX_X) {
        obj_free(g, ob);
        return false;
    }
    ob->sprite_set = SPRITE_MISSILE;
    ob->sprite_frame = ob->angle;
    obj_xinsert(g, ob);
    return y < MAX_Y;
}

static bool move_burst(game_t *g, object_t *ob)
{
    int x, y;
    obj_xremove(g, ob);

    if (ob->life < 0) {
        if (ob->owner) ob->owner->target = NULL;
        obj_free(g, ob);
        return false;
    }
    adjust_fall(ob, BOMBLIFE);
    obj_move_xy(ob, &x, &y);

    if (x < 0 || x >= MAX_X || y <= game_ground(g, x)) {
        if (ob->owner) ob->owner->target = NULL;
        obj_free(g, ob);
        return false;
    }
    /* While it burns it is what heat-seekers see. */
    if (ob->owner) ob->owner->target = ob;
    ob->sprite_set = SPRITE_STARBURST;
    ob->sprite_frame = ob->life & 1;
    obj_xinsert(g, ob);
    return y < MAX_Y;
}

static bool move_target(game_t *g, object_t *ob)
{
    object_t *plyr = &g->pool[g->player];
    ob->shot_at = NULL;

    /* Anti-aircraft fire only exists from game 1 upwards, and only fires
     * every other tick until game 2. */
    if (g->gamenum && ob->state == ST_STANDING &&
        (plyr->state == ST_FLYING || plyr->state == ST_STALLED ||
         plyr->state == ST_WOUNDED || plyr->state == ST_WOUNDSTALL) &&
        ob->clr != plyr->clr &&
        (g->gamenum > 1 || (g->countmove & 1))) {
        int r = sw_range(ob->x, ob->y, plyr->x, plyr->y);
        if (r > 0 && r < g->targrnge) {
            ob->shot_at = plyr;
            sw_init_shot(g, ob, plyr);
        }
    }

    if (--ob->hitcount < 0)
        ob->hitcount = 0;

    if (ob->state == ST_STANDING) {
        ob->sprite_set = SPRITE_TARGET;
        ob->sprite_frame = ob->orient;
    } else {
        ob->sprite_set = SPRITE_TARGET_HIT;
        ob->sprite_frame = 0;
    }
    return true;
}

static bool move_explosion(game_t *g, object_t *ob)
{
    int x, y;
    int orient = ob->orient;
    obj_xremove(g, ob);

    if (ob->life < 0) {
        if (orient) sw_sound_stop(g, ob);
        obj_free(g, ob);
        return false;
    }

    if (!--ob->life) {
        if (ob->dy < 0) {
            if (ob->dx < 0) ob->dx++;
            else if (ob->dx > 0) ob->dx--;
        }
        int floor = orient ? -10 : -g->gminspeed;
        if (ob->dy > floor)
            ob->dy--;
        ob->life = EXPLLIFE;
    }

    obj_move_xy(ob, &x, &y);
    if (x < 0 || x >= MAX_X || y <= game_ground(g, x)) {
        if (orient) sw_sound_stop(g, ob);
        obj_free(g, ob);
        return false;
    }
    ob->hitcount++;
    obj_xinsert(g, ob);
    ob->sprite_set = SPRITE_EXPLOSION;
    ob->sprite_frame = ob->orient;
    return y < MAX_Y;
}

static bool move_smoke(game_t *g, object_t *ob)
{
    objstate_t st = ob->owner ? ob->owner->state : ST_FINISHED;
    if (!--ob->life ||
        (st != ST_FALLING && st != ST_WOUNDED &&
         st != ST_WOUNDSTALL && st != ST_CRASHED)) {
        obj_free(g, ob);
        return false;
    }
    ob->sprite_set = -1;
    ob->sprite_frame = ob->clr;
    return true;
}

static bool move_flock(game_t *g, object_t *ob)
{
    int x, y;
    obj_xremove(g, ob);

    if (ob->life == -1) {
        obj_free(g, ob);
        return false;
    }
    if (!--ob->life) {
        ob->orient = !ob->orient;
        ob->life = FLOCKLIFE;
    }
    if (ob->x < MINFLCKX || ob->x > MAXFLCKX)
        ob->dx = -ob->dx;

    obj_move_xy(ob, &x, &y);
    obj_xinsert(g, ob);
    ob->sprite_set = SPRITE_FLOCK;
    ob->sprite_frame = ob->orient;
    return true;
}

static bool move_bird(game_t *g, object_t *ob)
{
    int x, y;
    obj_xremove(g, ob);

    if (ob->life == -1) {
        obj_free(g, ob);
        return false;
    }
    if (ob->life == -2) {
        /* Bounced off the ground or the ceiling: turn around. */
        ob->dy = -ob->dy;
        ob->dx = (g->countmove & 7) - 4;
        ob->life = BIRDLIFE;
    } else if (!--ob->life) {
        ob->orient = !ob->orient;
        ob->life = BIRDLIFE;
    }

    obj_move_xy(ob, &x, &y);

    /* The original let a stray bird wander off the end of the map (and read
     * past the terrain array doing it); turn it back instead. */
    if (x < 0 || x >= MAX_X) {
        ob->x = x = (x < 0) ? 0 : MAX_X - 1;
        ob->lx = 0;
        ob->dx = -ob->dx;
    }

    obj_xinsert(g, ob);
    ob->sprite_set = SPRITE_BIRD;
    ob->sprite_frame = ob->orient;

    if (y >= MAX_Y || y <= game_ground(g, x)) {
        ob->y -= ob->dy;
        ob->life = -2;
        return false;
    }
    return true;
}

static bool move_ox(game_t *g, object_t *ob)
{
    (void)g;
    ob->sprite_set = SPRITE_OX;
    ob->sprite_frame = ob->state != ST_STANDING;
    return true;
}

/* ---- sound requests ---------------------------------------------------- */

static void plane_sound(game_t *g, object_t *ob)
{
    if (ob->firing) {
        sw_sound_request(g, S_SHOT, 0, ob);
        return;
    }
    switch (ob->state) {
    case ST_FALLING:
        if (ob->dy >= 0)
            sw_sound_request(g, S_HIT, 0, ob);
        else
            sw_sound_request(g, S_FALLING, ob->y, ob);
        break;
    case ST_FLYING:
        sw_sound_request(g, S_PLANE, -ob->speed, ob);
        break;
    case ST_STALLED:
    case ST_WOUNDED:
    case ST_WOUNDSTALL:
        sw_sound_request(g, S_HIT, 0, ob);
        break;
    default:
        break;
    }
}

static void object_sound(game_t *g, object_t *ob)
{
    switch (ob->type) {
    case OBJ_PLANE:
        plane_sound(g, ob);
        break;
    case OBJ_BOMB:
        if (ob->dy <= 0)
            sw_sound_request(g, S_BOMB, -ob->y, ob);
        break;
    case OBJ_EXPLOSION:
        if (ob->orient)
            sw_sound_request(g, S_EXPLOSION, ob->hitcount, ob);
        break;
    case OBJ_TARGET:
        if (ob->shot_at)
            sw_sound_request(g, S_SHOT, 0, ob);
        break;
    default:
        break;
    }
}

/* ---- the driver -------------------------------------------------------- */

void sw_move_all(game_t *g, const uint16_t *keys)
{
    object_t *ob = g->top;
    while (ob) {
        object_t *next = ob->next;
        switch (ob->type) {
        case OBJ_PLANE:
            if (ob->ai)
                move_comp(g, ob);
            else
                move_player(g, ob, keys ? keys[ob->index] : 0);
            break;
        case OBJ_SHOT:      move_shot(g, ob); break;
        case OBJ_BOMB:      move_bomb(g, ob); break;
        case OBJ_MISSILE:   move_missile(g, ob); break;
        case OBJ_STARBURST: move_burst(g, ob); break;
        case OBJ_TARGET:    move_target(g, ob); break;
        case OBJ_EXPLOSION: move_explosion(g, ob); break;
        case OBJ_SMOKE:     move_smoke(g, ob); break;
        case OBJ_FLOCK:     move_flock(g, ob); break;
        case OBJ_BIRD:      move_bird(g, ob); break;
        case OBJ_OX:        move_ox(g, ob); break;
        default: break;
        }
        if (g->over || g->restarted)
            return;
        ob = next;
    }

    for (ob = g->top; ob; ob = ob->next)
        object_sound(g, ob);
}
