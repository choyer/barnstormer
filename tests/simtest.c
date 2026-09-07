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
    determinism();

    printf("%s (%d failures)\n", failures ? "FAILURES" : "all ok", failures);
    return failures ? 1 : 0;
}
