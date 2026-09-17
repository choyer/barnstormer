/*
 * flytest.c -- fly a map before anyone else has to.
 *
 * A map can load, obey every rule in doc/MAP_FORMAT.md and still be
 * unflyable: a building in the take-off path, a hillside that starts too soon
 * after the strip, a field facing the edge of the world.  Nothing catches
 * that except flying it.
 *
 * So this starts a real game against the computer, flies the player off the
 * deck with a simple scripted pilot -- the aircraft at the other fields are
 * flown by the game's own autopilot, which is the only fair judge of whether
 * a field can be operated from -- and reports, for each aeroplane, whether it
 * got away from its own aerodrome.
 *
 *     flytest MAP...      exit 0 if every field can be flown out of
 *
 * The bar is deliberately low: away means airborne by more than a building's
 * height and 200 columns from where it started.  A map that cannot manage
 * that is broken rather than hard.
 */
#include <stdio.h>
#include <stdlib.h>

#include "game.h"

#define AWAY_HEIGHT   30       /* clear of the scenery, not just off the mat */
#define AWAY_RANGE   200       /* far enough that it did not just hop        */
#define FLIGHT_TICKS 1500

/* Full throttle; rotate at flying speed; climb hard until there is room, then
 * keep the nose a margin above the ground ahead.
 *
 * Everything here is in the aeroplane's own terms rather than in compass
 * angles, because a map may put the player's field at either end of the
 * world, or in the middle, facing either way.  `base` is the heading it
 * started on -- 0 taking off east, ANGLES/2 west -- and `pitch` is how many
 * steps its nose is above that.  The flaps work the same way round
 * (move.c: `nangle += orient ? -flaps : flaps`), so K_FLAPU always raises
 * the nose whichever way the aeroplane points. */
static uint16_t scripted(game_t *g, object_t *p, int base)
{
    uint16_t k = K_ACCEL;
    int gh = game_ground(g, p->x);
    int want = -1;

    if (p->y <= gh + 14) {
        if (p->speed >= g->gmaxspeed)
            want = 1;
    } else {
        int dir = (p->angle > ANGLES / 4 && p->angle < 3 * ANGLES / 4) ? -1 : 1;
        int a = game_ground(g, p->x + 60 * dir);
        int b = game_ground(g, p->x + 120 * dir);
        int ahead = a > b ? a : b;
        int agl = p->y - ahead;
        want = (p->y - gh < AWAY_HEIGHT) ? 2
             : (agl < 35 ? 2 : (agl < 55 ? 1 : 0));
        if (p->speed <= g->gminspeed + 1)
            want = 0;
    }

    if (want >= 0) {
        int pitch = base ? base - p->angle : p->angle - base;
        pitch = (pitch + ANGLES) % ANGLES;
        if (pitch > ANGLES / 2)
            pitch -= ANGLES;            /* nose below the horizon           */
        if (pitch < want)
            k |= K_FLAPU;
        else if (pitch > want)
            k |= K_FLAPD;
    }
    return k;
}

static bool fly(const map_t *lv, const char *path)
{
    game_t *g = calloc(1, sizeof(*g));
    /* Against the computer, so the other fields have aircraft on them and the
     * autopilot is the thing being asked whether it can use them. */
    game_start(g, lv, PLAY_COMPUTER, 0);
    object_t *me = game_player(g);

    /* Which way the player's field points: a map may put it at either end,
     * or in the middle, so the scripted pilot is told where its nose began
     * rather than assuming east. */
    int base = (me->angle > ANGLES / 4 && me->angle < 3 * ANGLES / 4)
                   ? ANGLES / 2 : 0;

    struct flight { object_t *ob; int from, climbed, ranged; };
    struct flight plane[MAX_PLYR];
    int n = 0;
    for (object_t *ob = g->top; ob && n < MAX_PLYR; ob = ob->next)
        if (ob->type == OBJ_PLANE) {
            plane[n].ob = ob;
            plane[n].from = ob->x;
            plane[n].climbed = plane[n].ranged = 0;
            n++;
        }

    for (int t = 0; t < FLIGHT_TICKS && !g->over; t++) {
        uint16_t keys[MAX_PLYR] = { 0 };
        keys[g->player] = scripted(g, me, base);
        game_tick(g, keys);

        for (int i = 0; i < n; i++) {
            object_t *ob = plane[i].ob;
            int up = ob->y - game_ground(g, ob->x);
            int out = abs(ob->x - plane[i].from);
            if (up > plane[i].climbed) plane[i].climbed = up;
            if (out > plane[i].ranged) plane[i].ranged = out;
        }
    }

    printf("%s: %s\n", path, lv->name);
    bool ok = true;
    for (int i = 0; i < n; i++) {
        bool scripted_pilot = plane[i].ob == me;
        bool away = plane[i].climbed >= AWAY_HEIGHT &&
                    plane[i].ranged >= AWAY_RANGE;
        /* The reserves sit on their strips until the game wants them, so a
         * plane that never moved was never asked to fly and is not evidence
         * of anything.  One that started and stopped short is. */
        bool tried = scripted_pilot || plane[i].ranged > 20;

        printf("  field at %4d  %-9s  climbed %3d, ranged %4d  %s\n",
               plane[i].from, scripted_pilot ? "scripted" : "autopilot",
               plane[i].climbed, plane[i].ranged,
               !tried ? "waiting" : (away ? "away" : "STUCK"));
        if (tried && !away)
            ok = false;
    }
    free(g);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: flytest MAP...\n");
        return 2;
    }

    int bad = 0;
    for (int i = 1; i < argc; i++) {
        map_t *lv = NULL;
        if (map_load(argv[i], &lv) < 0) {
            fprintf(stderr, "%s: %s\n", argv[i], map_error());
            bad++;
            continue;
        }
        if (!fly(lv, argv[i]))
            bad++;
        map_free(lv);
    }
    return bad ? 1 : 0;
}
