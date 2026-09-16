/*
 * simtest.c -- headless simulation soak test.
 *
 * Runs the game with scripted input and checks the invariants that matter:
 * objects stay inside the world, the pool never leaks, and the free list and
 * the two object lists stay consistent.  Build with `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "net.h"
#include "sprites.h"

static int failures;

static void check(bool ok, const char *what, unsigned tick)
{
    if (!ok) {
        printf("  FAIL at tick %u: %s\n", tick, what);
        failures++;
    }
}

static void verify(game_t *g, unsigned tick)
{
    int live = 0;
    for (object_t *ob = g->top; ob; ob = ob->next) {
        live++;
        check(live <= MAX_OBJS, "master list longer than the pool", tick);
        if (live > MAX_OBJS)
            return;
        check(ob->alive, "dead object still on the master list", tick);
        check(ob->x >= -64 && ob->x <= MAX_X + 64, "object x out of range", tick);
        check(ob->y >= -400 && ob->y <= 4 * MAX_Y, "object y out of range", tick);
        check(ob->sprite_set < SPRITE_SET_COUNT, "bad sprite id", tick);
    }

    int freen = 0;
    for (object_t *ob = g->free_list; ob; ob = ob->next) {
        freen++;
        check(freen <= MAX_OBJS, "free list longer than the pool", tick);
        if (freen > MAX_OBJS)
            return;
        check(!ob->alive, "live object on the free list", tick);
    }
    check(live + freen == MAX_OBJS, "objects lost from the pool", tick);

    int xn = 0;
    for (object_t *ob = g->xhead.xnext; ob != &g->xtail; ob = ob->xnext) {
        xn++;
        check(xn <= MAX_OBJS, "x-list longer than the pool", tick);
        if (xn > MAX_OBJS)
            return;
        check(ob->xprev->xnext == ob, "x-list links inconsistent", tick);
        check(ob->xprev->x <= ob->x, "x-list not sorted", tick);
    }

    for (int i = 0; i < MAX_X; i++)
        check(g->ground[i] > 0 && g->ground[i] < MAX_Y, "terrain out of range", tick);
}

/* A crude pilot: climb, shoot, bomb, and occasionally do something silly. */
static uint16_t scripted_keys(unsigned tick)
{
    uint16_t k = 0;
    unsigned phase = (tick / 37) % 8;
    switch (phase) {
    case 0: k = K_ACCEL | K_FLAPU; break;
    case 1: k = K_ACCEL | K_SHOT; break;
    case 2: k = K_FLAPD | K_BOMB; break;
    case 3: k = K_FLIP; break;
    case 4: k = K_ACCEL | K_MISSILE; break;
    case 5: k = K_STARBURST | K_FLAPU; break;
    case 6: k = K_DEACC; break;
    default: k = K_HOME; break;
    }
    return k;
}

static int soak(const char *name, playmode_t mode, int gamenum,
                unsigned ticks, bool scripted)
{
    game_t *g = calloc(1, sizeof(*g));
    int before = failures;

    printf("%-28s ", name);
    fflush(stdout);
    game_start(g, &level_classic, mode, gamenum);

    for (unsigned t = 0; t < ticks; t++) {
        uint16_t keys[MAX_PLYR] = { 0 };
        if (scripted)
            keys[g->player] = scripted_keys(t);
        game_tick(g, keys);
        if (g->over)
            game_start(g, &level_classic, mode, gamenum);
        if ((t & 15) == 0)
            verify(g, t);
    }
    verify(g, ticks);

    printf("%s  (score %d, game %d)\n",
           failures == before ? "ok" : "FAILED", g->pool[0].score, g->gamenum);
    free(g);
    return failures - before;
}

/* Two runs fed the same inputs must agree tick for tick; that is the premise
 * the planned lockstep netplay rests on (doc/ROADMAP.md), and net_state_hash()
 * is the same checksum a session would compare between peers. */
static uint32_t run_hashed(playmode_t mode, int gamenum, unsigned ticks)
{
    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &level_classic, mode, gamenum);
    uint32_t h = 0;
    for (unsigned t = 0; t < ticks; t++) {
        uint16_t keys[MAX_PLYR] = { 0 };
        keys[g->player] = scripted_keys(t);
        game_tick(g, keys);
        h ^= net_state_hash(g) + t;
    }
    free(g);
    return h;
}

static int determinism(void)
{
    printf("%-28s ", "deterministic replay");
    fflush(stdout);
    uint32_t a = run_hashed(PLAY_COMPUTER, 0, 3000);
    uint32_t b = run_hashed(PLAY_COMPUTER, 0, 3000);
    bool ok = (a == b);
    printf("%s  (%08x)\n", ok ? "ok" : "FAILED", a);
    if (!ok)
        failures++;
    return ok ? 0 : 1;
}

/* What is destroyed stops being in the way.  A flattened building is not
 * drawn, and for a while it was still collidable: flying through the gap it
 * left crashed the aircraft into nothing. */
static object_t *standing(game_t *g, objtype_t type)
{
    for (object_t *ob = g->top; ob; ob = ob->next) {
        if (ob->type != type || ob->state != ST_STANDING)
            continue;
        if (type == OBJ_TARGET && ob->clr != 2)
            continue;              /* an enemy building, so it can be hit */
        if (game_ground(g, ob->x + 8) < 110)
            return ob;             /* low, flat ground: no hillside in it */
    }
    return NULL;
}

/* Put the player in the air at (x, y) as an ordinary flying aircraft. */
static void fly_at(game_t *g, int x, int y)
{
    object_t *p = game_player(g);
    p->state = ST_FLYING;
    p->athome = false;
    p->crashcnt = 0;
    p->hitcount = 0;
    p->life = 100;
    p->sprite_set = SPRITE_PLANE;
    p->sprite_frame = 0;
    p->symw = p->symh = 16;
    p->x = x;
    p->y = y;
    obj_xremove(g, p);
    obj_xinsert(g, p);
}

static int wreckage(void)
{
    printf("%-28s ", "wreckage is not solid");
    fflush(stdout);
    int before = failures;

    game_t *g = calloc(1, sizeof(*g));
    uint16_t keys[MAX_PLYR] = { 0 };

    for (int pass = 0; pass < 2; pass++) {
        bool ox = pass == 1;
        game_start(g, &level_classic, PLAY_COMPUTER, 0);
        object_t *victim = standing(g, ox ? OBJ_OX : OBJ_TARGET);
        check(victim != NULL, "no scenery to fly into", 0);
        if (!victim)
            break;
        int vx = victim->x, vy = victim->y;

        /* Flying into it while it stands takes both down: that is the game
         * working, and it is also how the wreck gets made. */
        fly_at(g, vx, vy);
        game_tick(g, keys);
        check(game_player(g)->state != ST_FLYING,
              ox ? "running an ox down left the aircraft flying"
                 : "a standing building did not stop the aircraft", 0);
        check(victim->state == ST_FINISHED,
              ox ? "the ox survived being run down"
                 : "the building survived being flown into", 0);

        /* Let the explosion burn out -- it is a real thing in the air and is
         * supposed to be lethal while it lasts. */
        for (int i = 0; i < 60; i++)
            game_tick(g, keys);

        /* The same square of sky, now empty. */
        fly_at(g, vx, vy);
        game_tick(g, keys);
        check(game_player(g)->state == ST_FLYING,
              ox ? "a dead ox still brings an aircraft down"
                 : "a flattened building still brings an aircraft down", 0);
        check(game_player(g)->crashcnt == 0,
              "flying through empty sky cost a life", 0);
        verify(g, 0);
    }
    free(g);

    printf("%s\n", failures == before ? "ok" : "FAILED");
    return failures - before;
}

/* The clear-the-map counter has to come from the level, not from MAX_TARG:
 * an authored level (doc/LEVEL_FORMAT.md) may carry fewer buildings, and a
 * counter that starts above the number standing can never reach zero. */
static int authored_levels(void)
{
    printf("%-28s ", "authored levels");
    fflush(stdout);
    int before = failures;

    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &level_classic, PLAY_COMPUTER, 0);
    check(g->numtarg[1] == level_classic.n_targets - 3,
          "the classic level does not start with 17 enemy buildings", 0);
    check(g->numtarg[0] == 0, "the player's own buildings are counted", 0);

    /* The same level, truncated: twelve buildings, of which the three either
     * side of centre are the player's. */
    level_t small = level_classic;
    small.n_targets = 12;
    game_start(g, &small, PLAY_COMPUTER, 0);
    check(g->numtarg[1] == 9, "a shorter level starts with the wrong count", 0);

    /* A dogfight arena with no buildings at all must still start. */
    level_t bare = level_classic;
    bare.n_targets = 0;
    game_start(g, &bare, PLAY_COMPUTER, 0);
    check(g->numtarg[1] == 0, "an empty level starts with buildings to clear",
          0);
    for (unsigned t = 0; t < 200; t++) {
        uint16_t keys[MAX_PLYR] = { 0 };
        game_tick(g, keys);
    }
    verify(g, 200);

    /* The boards are three columns of scores made on the classic map, so a
     * run on anything else must not reach them however it ended. */
    g->end_reason = RUN_RETIRED;
    check(!game_ranked(g), "an authored level ranks on the boards", 0);
    game_start(g, &level_classic, PLAY_COMPUTER, 0);
    g->end_reason = RUN_RETIRED;
    check(game_ranked(g), "the classic level stopped ranking", 0);
    free(g);

    printf("%s\n", failures == before ? "ok" : "FAILED");
    return failures - before;
}

int main(void)
{
    printf("barnstormer simulation soak test\n");
    soak("idle, novice",            PLAY_NOVICE,   0, 4000, false);
    soak("idle, single",            PLAY_SINGLE,   0, 4000, false);
    soak("idle, vs computer",       PLAY_COMPUTER, 0, 8000, false);
    soak("flying, vs computer",     PLAY_COMPUTER, 0, 8000, true);
    soak("flying, vs computer g3",  PLAY_COMPUTER, 3, 8000, true);
    soak("flying, novice",          PLAY_NOVICE,   0, 8000, true);
    soak("flying, single g7",       PLAY_SINGLE,   7, 8000, true);
    soak("long run, vs computer",   PLAY_COMPUTER, 0, 60000, true);
    authored_levels();
    wreckage();
    determinism();

    printf("%s (%d failures)\n", failures ? "FAILURES" : "all ok", failures);
    return failures ? 1 : 0;
}
