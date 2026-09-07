/*
 * game.c -- run setup, per-tick driver, spawning and scoring.
 */
#include <string.h>

#include "game.h"
#include "sprites.h"

/* Runway slot tables from the original SWINIT.C (inits/initc/initm). */
static const int slot_single[MAX_PLYR]   = { 0, 7, 0, 0 };
static const int slot_computer[MAX_PLYR] = { 0, 7, 1, 6 };
static const int slot_net[MAX_PLYR]      = { 0, 7, 3, 4 };

int level_runway_slot(playmode_t mode, int i)
{
    if (i < 0 || i >= MAX_PLYR)
        i = 0;
    switch (mode) {
    case PLAY_COMPUTER: return slot_computer[i];
    case PLAY_NET:      return slot_net[i];
    default:            return slot_single[i];
    }
}

/* ---- difficulty -------------------------------------------------------- */

static void set_difficulty(game_t *g)
{
    g->gmaxspeed = MAX_SPEED + g->gamenum;
    g->gminspeed = MIN_SPEED + g->gamenum;

    int r = 150;
    if (g->gamenum < 6)
        r -= 15 * (6 - g->gamenum);
    g->targrnge = r * r;
}

/* ---- plane construction ------------------------------------------------ */

static object_t *init_plane(game_t *g, object_t *reuse, int slot)
{
    object_t *ob = reuse ? reuse : obj_alloc(g);
    if (!ob)
        return NULL;

    const level_runway_t *rw = &g->level->runways[slot % g->level->n_runways];

    ob->type = OBJ_PLANE;
    ob->x = rw->x;

    /* Sit on the highest ground under the aircraft's footprint. */
    int height = 0;
    for (int x = ob->x; x <= ob->x + 20; x++)
        if (game_ground(g, x) > height)
            height = game_ground(g, x);
    ob->y = height + 13;

    ob->lx = ob->ly = 0;
    ob->speed = ob->flaps = ob->accel = ob->hitcount = 0;
    ob->bdelay = ob->mdelay = ob->bsdelay = 0;
    obj_set_dxdy(ob, 0, 0);

    ob->orient = rw->orient;
    ob->angle = ob->orient ? (ANGLES / 2) : 0;
    ob->target = ob->mfiring = ob->shot_at = NULL;
    ob->firing = ob->bombing = ob->bfiring = ob->home = false;
    ob->symw = ob->symh = 16;
    ob->sprite_set = SPRITE_PLANE;
    ob->athome = true;

    if (!reuse || ob->state == ST_CRASHED) {
        ob->rounds   = MAXROUNDS;
        ob->bombs    = MAXBOMBS;
        ob->missiles = MAXMISSILES;
        ob->bursts   = MAXBURSTS;
        ob->life     = MAXFUEL;
    }

    if (!reuse) {
        ob->score = ob->crashcnt = 0;
        g->endsts[ob->index] = END_PLAYING;
        g->nearest[ob->index] = NULL;
        obj_xinsert(g, ob);
    } else {
        obj_xremove(g, ob);
        obj_xinsert(g, ob);
    }

    ob->state = ST_FLYING;
    return ob;
}

void sw_init_player(game_t *g, object_t *reuse, int slot)
{
    object_t *ob = init_plane(g, reuse, slot);
    if (!ob)
        return;
    ob->ai = false;
    if (!reuse) {
        ob->clr = ob->index % 2 + 1;
        ob->owner = ob;
        memcpy(&g->start[ob->index], ob, sizeof(*ob));
        g->goingsun = false;
        g->endcount = 0;
    }
    if (ob->index == g->player) {
        g->displx = ob->x - SCR_CENTR;
        g->disprx = g->displx + SCR_WDTH - 1;
        /* A fresh aeroplane gets a fresh windscreen. */
        g->screen_gen++;
        g->shothole = g->splatbird = g->splatox = 0;
        g->oxsplatted = false;
    }
}

void sw_init_comp(game_t *g, object_t *reuse, int slot)
{
    object_t *ob = init_plane(g, reuse, slot);
    if (!ob)
        return;
    ob->ai = true;
    if (!reuse) {
        ob->clr = 2;
        /* Every computer pilot answers to the first of their number, which
         * is what decides team colour when they are drawn. */
        ob->owner = &g->pool[1];
        memcpy(&g->start[ob->index], ob, sizeof(*ob));
    }
    if (g->mode == PLAY_SINGLE || g->mode == PLAY_NOVICE) {
        ob->state = ST_FINISHED;
        obj_xremove(g, ob);
    }
}

/* ---- static scenery ---------------------------------------------------- */

static void init_targets(game_t *g)
{
    const level_t *lv = g->level;

    /* Single player: the three buildings either side of centre are the
     * player's own; the rest belong to the enemy and must all be levelled. */
    g->numtarg[0] = 0;
    g->numtarg[1] = MAX_TARG - 3;

    for (int i = 0; i < lv->n_targets && i < MAX_TARG; i++) {
        object_t *ob = obj_alloc(g);
        if (!ob)
            return;
        g->targets[i] = ob;

        int minx = lv->targets[i].x;
        int maxx = minx + 15;
        int minh = 999, maxh = 0;
        for (int x = minx; x <= maxx; x++) {
            int h = game_ground(g, x);
            if (h > maxh) maxh = h;
            if (h < minh) minh = h;
        }
        int aveh = (minh + maxh) >> 1;
        while (aveh + 16 >= MAX_Y)
            aveh--;

        ob->x = minx;
        ob->y = aveh + 16;

        /* Buildings stand on a levelled pad. */
        for (int x = minx; x <= maxx && x < MAX_X; x++)
            g->ground[x] = (uint8_t)aveh;

        ob->type = OBJ_TARGET;
        ob->state = ST_STANDING;
        ob->orient = lv->targets[i].kind;
        ob->life = i;
        ob->owner = (i < MAX_TARG / 2 && i > MAX_TARG / 2 - 4)
                        ? &g->pool[0] : &g->pool[1];
        ob->clr = ob->owner->clr;
        ob->symw = ob->symh = 16;
        ob->sprite_set = SPRITE_TARGET;
        obj_xinsert(g, ob);
    }
}

static void init_oxen(game_t *g)
{
    if (g->mode == PLAY_NOVICE) {
        for (int i = 0; i < MAX_OXEN; i++)
            g->targets[MAX_TARG + i] = NULL;
        return;
    }
    for (int i = 0; i < g->level->n_oxen && i < MAX_OXEN; i++) {
        object_t *ob = obj_alloc(g);
        if (!ob)
            return;
        g->targets[MAX_TARG + i] = ob;
        ob->type = OBJ_OX;
        ob->state = ST_STANDING;
        ob->x = g->level->oxen[i].x;
        ob->y = g->level->oxen[i].y;
        ob->owner = ob;
        ob->symw = ob->symh = 16;
        ob->sprite_set = SPRITE_OX;
        ob->clr = 1;
        obj_xinsert(g, ob);
    }
}

void sw_init_bird(game_t *g, object_t *flock, int i)
{
    static const int ibx[]  = {  8,  3,  0,  6,  7, 14, 10, 12 };
    static const int iby[]  = { 16,  1,  8,  3, 12, 10,  7, 14 };
    static const int ibdx[] = { -2,  2, -3,  3, -1,  1,  0,  0 };
    static const int ibdy[] = { -1, -2, -1, -2, -1, -2, -1, -2 };

    object_t *ob = obj_alloc(g);
    if (!ob)
        return;
    i &= 7;
    ob->type = OBJ_BIRD;
    ob->x = flock->x + ibx[i];
    ob->y = flock->y - iby[i];
    ob->dx = ibdx[i];
    ob->dy = ibdy[i];
    ob->life = BIRDLIFE;
    ob->owner = flock;
    ob->symw = 4;
    ob->symh = 2;
    ob->sprite_set = SPRITE_BIRD;
    ob->clr = flock->clr;
    obj_xinsert(g, ob);
}

static void init_flocks(game_t *g)
{
    static const int ifx[]  = { MINFLCKX, MINFLCKX + 1000,
                                MAXFLCKX - 1000, MAXFLCKX };
    static const int ifdx[] = { 2, 2, -2, -2 };

    if (g->mode == PLAY_NOVICE)
        return;

    for (int i = 0; i < MAX_FLCK; i++) {
        object_t *ob = obj_alloc(g);
        if (!ob)
            return;
        ob->type = OBJ_FLOCK;
        ob->state = ST_FLYING;
        ob->x = ifx[i];
        ob->y = MAX_Y - 1;
        ob->dx = ifdx[i];
        ob->life = FLOCKLIFE;
        ob->owner = ob;
        ob->symw = ob->symh = 16;
        ob->sprite_set = SPRITE_FLOCK;
        ob->clr = 9;
        obj_xinsert(g, ob);
        for (int j = 0; j < MAX_BIRD; j++)
            sw_init_bird(g, ob, 1);
    }
}

/* ---- ordnance ---------------------------------------------------------- */

/* Cheap integer range used only to lead a shot; negative means "too far". */
static int shot_range(int x, int y, int ax, int ay)
{
    int dy = sw_abs(y - ay);
    dy += dy >> 1;
    int dx = sw_abs(x - ax);
    if (dx > 100 || dy > 100)
        return -1;
    if (dx < dy) { int t = dx; dx = dy; dy = t; }
    return ((7 * dx) + (dy << 2)) >> 3;
}

void sw_init_shot(game_t *g, object_t *shooter, object_t *lead)
{
    /* Only a human pilot can run dry; the computer and the ground batteries
     * have as much ammunition as they need, exactly as in the original. */
    if (!lead && !shooter->ai && shooter->rounds <= 0)
        return;

    object_t *ob = obj_alloc(g);
    if (!ob)
        return;

    if (g->mode != PLAY_NOVICE && shooter->rounds > 0)
        shooter->rounds--;

    int bspeed = BULSPEED + g->gamenum;

    if (lead) {
        /* Ground guns fire at where the aircraft is going to be. */
        int x = lead->x + (lead->dx << 2);
        int y = lead->y + (lead->dy << 2);
        int r = shot_range(x, y, shooter->x, shooter->y);
        if (r < 1) {
            obj_free(g, ob);
            return;
        }
        ob->dx = ((x - shooter->x) * bspeed) / r;
        ob->dy = ((y - shooter->y) * bspeed) / r;
        ob->ldx = ob->ldy = 0;
    } else {
        int nspeed = shooter->speed + bspeed;
        obj_set_dxdy(ob, nspeed * SW_COS(shooter->angle),
                         nspeed * SW_SIN(shooter->angle));
    }

    ob->type = OBJ_SHOT;
    ob->x = shooter->x + 16 / 2;
    ob->y = shooter->y - 16 / 2;
    ob->lx = shooter->lx;
    ob->ly = shooter->ly;
    ob->life = BULLIFE;
    ob->owner = shooter;
    ob->clr = shooter->clr;
    ob->symw = ob->symh = 1;
    ob->sprite_set = -1;
    ob->speed = 0;
    obj_xinsert(g, ob);
}

void sw_init_bomb(game_t *g, object_t *from)
{
    if ((from->type == OBJ_PLANE && from->bombs <= 0) || from->bdelay)
        return;
    object_t *ob = obj_alloc(g);
    if (!ob)
        return;

    if (g->mode != PLAY_NOVICE && from->bombs > 0)
        from->bombs--;
    from->bdelay = 10;

    ob->type = OBJ_BOMB;
    ob->state = ST_FALLING;
    ob->dx = from->dx;
    ob->dy = from->dy;

    /* Released perpendicular to the wings, so an inverted plane drops up. */
    int angle = from->orient ? (from->angle + ANGLES / 4) % ANGLES
                             : (from->angle + 3 * ANGLES / 4) % ANGLES;
    ob->x = from->x + ((SW_COS(angle) * 10) >> 8) + 4;
    ob->y = from->y + ((SW_SIN(angle) * 10) >> 8) - 4;

    ob->life = BOMBLIFE;
    ob->owner = from;
    ob->clr = from->clr;
    ob->symw = ob->symh = 8;
    ob->sprite_set = SPRITE_BOMB;
    obj_xinsert(g, ob);
}

void sw_init_missile(game_t *g, object_t *from)
{
    if (from->mdelay || from->missiles <= 0)
        return;
    object_t *ob = obj_alloc(g);
    if (!ob)
        return;

    if (g->mode != PLAY_NOVICE)
        from->missiles--;
    from->mdelay = 5;

    ob->type = OBJ_MISSILE;
    ob->state = ST_FLYING;
    int angle = ob->angle = from->angle;
    ob->x = from->x + (SW_COS(angle) >> 4) + 4;
    ob->y = from->y + (SW_SIN(angle) >> 4) - 4;
    int nspeed = ob->speed = g->gmaxspeed + (g->gmaxspeed >> 1);
    obj_set_dxdy(ob, nspeed * SW_COS(angle), nspeed * SW_SIN(angle));

    ob->life = MISSLIFE;
    ob->owner = from;
    ob->clr = from->clr;
    ob->symw = ob->symh = 8;
    ob->sprite_set = SPRITE_MISSILE;
    ob->target = from->mfiring;
    obj_xinsert(g, ob);
}

void sw_init_burst(game_t *g, object_t *from)
{
    if (from->bsdelay || from->bursts <= 0)
        return;
    object_t *ob = obj_alloc(g);
    if (!ob)
        return;

    from->bsdelay = 5;
    if (g->mode != PLAY_NOVICE)
        from->bursts--;

    ob->type = OBJ_STARBURST;
    ob->state = ST_FALLING;

    int angle = from->orient ? (from->angle + 3 * ANGLES / 8) % ANGLES
                             : (from->angle + 5 * ANGLES / 8) % ANGLES;
    obj_set_dxdy(ob, g->gminspeed * SW_COS(angle),
                     g->gminspeed * SW_SIN(angle));
    ob->dx += from->dx;
    ob->dy += from->dy;

    ob->x = from->x + ((SW_COS(angle) * 10) >> 10) + 4;
    ob->y = from->y + ((SW_SIN(angle) * 10) >> 10) - 4;

    ob->life = BURSTLIFE;
    ob->owner = from;
    ob->clr = from->clr;
    ob->symw = ob->symh = 8;
    ob->sprite_set = SPRITE_STARBURST;
    obj_xinsert(g, ob);
}

void sw_init_explosion(game_t *g, object_t *from, bool small)
{
    int obox  = from->x + (from->symw >> 1);
    int oboy  = from->y + (from->symh >> 1);
    int obodx = from->dx >> 2;
    int obody = from->dy >> 2;
    int oboclr = from->clr;

    int ic, speed;
    if (from->type == OBJ_TARGET && from->orient == TARGET_FUEL) {
        ic = 1;                       /* fuel dumps go up spectacularly    */
        speed = g->gminspeed;
    } else {
        ic = small ? 6 : 2;
        speed = g->gminspeed >> ((g->explseed & 7) != 7);
    }

    /* A pilot bails out of an aircraft that was still under control. */
    bool mansym = from->type == OBJ_PLANE &&
                  (from->state == ST_FLYING || from->state == ST_WOUNDED);

    for (int i = 1; i <= 15; i += ic) {
        object_t *ob = obj_alloc(g);
        if (!ob)
            return;

        ob->type = OBJ_EXPLOSION;
        obj_set_dxdy(ob, SW_COS(i) * speed, SW_SIN(i) * speed);
        ob->dx += obodx;
        ob->dy += obody;

        ob->x = obox + ob->dx;
        ob->y = oboy + ob->dy;
        g->explseed = (uint32_t)ob->x * (uint32_t)ob->y * g->explseed + 7491;
        if (!g->explseed)
            g->explseed = 74917777u;

        ob->life = EXPLLIFE;
        int orient = ob->orient = (g->explseed & 0x01C0) >> 6;
        if (mansym && (orient == 0 || orient == 7)) {
            mansym = false;
            orient = ob->orient = 0;
            ob->dx = obodx;
            ob->dy = -g->gminspeed;
        }

        ob->owner = from;
        ob->clr = oboclr;
        ob->symw = ob->symh = 8;
        ob->sprite_set = SPRITE_EXPLOSION;
        if (orient)
            sw_sound_start(g, ob, S_EXPLOSION);
        obj_xinsert(g, ob);
    }
}

void sw_init_smoke(game_t *g, object_t *from)
{
    object_t *ob = obj_alloc(g);
    if (!ob)
        return;
    ob->type = OBJ_SMOKE;
    ob->x = from->x + 8;
    ob->y = from->y - 8;
    ob->dx = from->dx;
    ob->dy = from->dy;
    ob->life = SMOKELIFE;
    ob->owner = from;
    ob->symw = ob->symh = 1;
    ob->sprite_set = -1;
    ob->clr = from->clr;
    /* Smoke is not in the x-list: it never collides with anything. */
}

/* ---- damage and scoring ------------------------------------------------ */

void sw_plane_hit(object_t *ob)
{
    ob->ldx = ob->ldy = 0;
    ob->hitcount = FALLCOUNT;
    ob->state = ST_FALLING;
    ob->athome = false;
}

void sw_plane_stall(game_t *g, object_t *ob)
{
    (void)g;
    ob->ldx = ob->ldy = 0;
    ob->orient = 0;
    ob->dx = 0;
    ob->dy = 0;
    ob->angle = 7 * ANGLES / 8;
    ob->speed = 0;
    ob->hitcount = STALLCOUNT;
    ob->state = (ob->state == ST_WOUNDED) ? ST_WOUNDSTALL : ST_STALLED;
    ob->athome = false;
}

void sw_plane_crash(game_t *g, object_t *ob)
{
    if (ob->dx < 0)
        ob->angle = (ob->angle + 2) % ANGLES;
    else
        ob->angle = (ob->angle + ANGLES - 2) % ANGLES;

    ob->state = ST_CRASHED;
    ob->athome = false;
    ob->dx = ob->dy = ob->speed = 0;
    ob->ldx = ob->ldy = 0;

    /* Crashing on your own doorstep costs you twice as long on the ground. */
    const object_t *base = &g->start[ob->index];
    ob->hitcount = (sw_abs(base->x - ob->x) < AI_SAFERESET &&
                    sw_abs(base->y - ob->y) < AI_SAFERESET)
                       ? (MAXCRCOUNT << 1) : MAXCRCOUNT;
}

void sw_score(game_t *g, object_t *ob, int points)
{
    /* Shooting up your own side subtracts; the enemy's adds. */
    if (ob->clr == 1)
        g->pool[0].score -= points;
    else
        g->pool[0].score += points;
}

void sw_winner(game_t *g, object_t *ob)
{
    g->endsts[ob->index] = END_WINNER;
    if (ob->index == g->player) {
        g->endcount = 72;
        g->goingsun = true;
        ob->dx = ob->dy = 0;
        ob->ldx = ob->ldy = 0;
        ob->state = ST_FLYING;
        ob->life = MAXFUEL;
        ob->speed = MIN_SPEED;
    }
}

void sw_loser(game_t *g, object_t *ob)
{
    g->endsts[ob->index] = END_LOSER;
    if (ob->index == g->player) {
        g->endcount = 20;
        g->over_msg = "THE END";
    }
}

void sw_end_game(game_t *g, int targclr)
{
    int winclr = 1;
    for (object_t *ob = g->top; ob && ob->type == OBJ_PLANE; ob = ob->next) {
        if (g->endsts[ob->index] != END_PLAYING)
            continue;
        bool alive = ob->state == ST_FLYING || ob->state == ST_STALLED ||
                     ob->state == ST_WOUNDED || ob->state == ST_WOUNDSTALL;
        if (ob->clr == winclr &&
            (ob->crashcnt < MAXCRASH - 1 || (ob->crashcnt < MAXCRASH && alive)))
            sw_winner(g, ob);
        else
            sw_loser(g, ob);
    }
    (void)targclr;
}

/* ---- lifecycle --------------------------------------------------------- */

static void reset_pool(game_t *g)
{
    memset(g->pool, 0, sizeof(g->pool));
    for (int i = 0; i < MAX_OBJS; i++) {
        g->pool[i].index = i;
        g->pool[i].next = (i + 1 < MAX_OBJS) ? &g->pool[i + 1] : NULL;
    }
    g->free_list = &g->pool[0];
    g->top = g->bot = NULL;

    memset(&g->xhead, 0, sizeof(g->xhead));
    memset(&g->xtail, 0, sizeof(g->xtail));
    g->xhead.x = -32767;
    g->xtail.x = 32767;
    g->xhead.xnext = &g->xtail;
    g->xtail.xprev = &g->xhead;

    memset(g->targets, 0, sizeof(g->targets));
    memset(g->nearest, 0, sizeof(g->nearest));
}

static void build_world(game_t *g)
{
    memcpy(g->ground, g->level->ground, MAX_X);
    reset_pool(g);

    sw_init_player(g, NULL, level_runway_slot(g->mode, 0));
    for (int i = 1; i < MAX_PLYR; i++)
        sw_init_comp(g, NULL, level_runway_slot(g->mode, i));

    init_targets(g);
    init_oxen(g);
    init_flocks(g);
    set_difficulty(g);

    g->countmove = 0;
    g->endcount = 0;
    g->goingsun = false;
    g->quit = false;
    g->over = false;
    g->over_msg = NULL;
    g->shothole = g->splatbird = g->splatox = 0;
    g->oxsplatted = false;
    g->terrain_dirty = true;
    memset(g->endsts, 0, sizeof(g->endsts));
    sw_sound_reset(g);
    g->restarted = true;
}

void game_start(game_t *g, const level_t *level, playmode_t mode, int gamenum)
{
    bool sound_was_on = g->sound_on;

    memset(g, 0, sizeof(*g));
    g->level = level;
    g->mode = mode;
    g->gamenum = gamenum;
    g->player = 0;
    g->n_players = 1;
    g->maxcrash = MAXCRASH;
    g->explseed = level->rand_seed ? level->rand_seed : 7491;
    g->randseed = 74917777u;
    g->sound_on = sound_was_on;
    g->sound_type = g->sound_parm = S_NONE;

    build_world(g);
}

void game_restart(game_t *g)
{
    int score = g->pool[0].score;

    if (g->endsts[g->player] == END_WINNER) {
        /* Surviving lives are worth a rising bonus, as in the original. */
        object_t *ob = &g->pool[g->player];
        int inc = 0;
        while (ob->crashcnt++ < g->maxcrash)
            score += (inc += 25);
        g->gamenum++;
        if (g->gamenum > MAX_GAME)
            g->gamenum = MAX_GAME;
    } else {
        g->gamenum = 0;
        score = 0;
    }

    build_world(g);
    g->pool[0].score = score;
}

void game_abandon(game_t *g)
{
    object_t *p = &g->pool[g->player];
    p->life = QUIT_LIFE;
    p->home = false;
    g->quit = true;
    g->over = true;
    g->endsts[g->player] = END_LOSER;
    g->over_msg = "GAME ABANDONED";
    sw_sound_reset(g);
}

object_t *game_player(game_t *g)
{
    return &g->pool[g->player];
}

void game_tick(game_t *g, const uint16_t *keys)
{
    if (g->over)
        return;

    g->restarted = false;
    sw_move_all(g, keys);
    if (!g->restarted && !g->over)
        sw_collide(g);
    sw_sound_resolve(g);
    g->countmove++;
}
