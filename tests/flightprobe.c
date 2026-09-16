/*
 * flightprobe.c -- what the aeroplane can actually do, in numbers.
 *
 * A map can obey every rule in doc/MAP_FORMAT.md and still be unflyable:
 * a slope no aircraft can climb, a runway with a mountain at the end of it, a
 * valley too narrow to turn around in.  Those limits are not written down
 * anywhere -- they fall out of the flight model -- so this measures them, and
 * the numbers go into the map-design skill where a generator can use them.
 *
 * Not a test: nothing here passes or fails.  `make probe` prints the table.
 * Re-run it if the flight model ever changes, because everything built on
 * these numbers will be wrong the moment it does.
 *
 * The pilot is deliberately a good one -- full throttle, pitch held exactly,
 * no hesitation -- so the numbers are what is possible, not what is easy.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define GROUND_H   60          /* the flat plain everything starts from */
#define RUNWAY_X   400

static uint8_t ground[MAX_X];
static map_runway_t runways[2] = { { RUNWAY_X, 0 }, { 2400, 1 } };
static map_t probe_map = {
    .name = "flight probe", .format = MAP_FORMAT_VERSION,
    .width = MAX_X, .height = MAX_Y, .rand_seed = 7491,
    .ground = ground, .runways = runways, .n_runways = 2,
};

/* The stick turns one sixteenth of a circle per tick while it is held, so a
 * held pull-up is a loop.  Flying an angle means pressing towards it and then
 * letting go, which is what a person does without thinking about it. */
static uint16_t fly_at(const object_t *p, int want_angle, bool throttle)
{
    uint16_t k = throttle ? K_ACCEL : 0;
    if (p->angle != want_angle) {
        int up = (want_angle - p->angle + ANGLES) % ANGLES;
        k |= (up <= ANGLES / 2) ? K_FLAPU : K_FLAPD;
    }
    return k;
}

static void tick(game_t *g, uint16_t k)
{
    uint16_t keys[MAX_PLYR] = { 0 };
    keys[g->player] = k;
    game_tick(g, keys);
}

/* Wounded counts: single player has birds in it, and a bird strike damages an
 * aeroplane without ending its flight.  Leaving it out made every long probe
 * look like it had hit the scenery. */
static bool flying(const object_t *p)
{
    return p->state == ST_FLYING || p->state == ST_STALLED ||
           p->state == ST_WOUNDED || p->state == ST_WOUNDSTALL;
}

/* ---- 1. how much ground a take-off needs ------------------------------- */

static void probe_takeoff(void)
{
    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &probe_map, PLAY_SINGLE, 0);
    object_t *p = game_player(g);

    int start = p->x;
    int rolled = -1, cleared16 = -1, cleared50 = -1;

    for (int t = 0; t < 1200 && flying(p); t++) {
        /* Full throttle, then hold the shallowest climb there is. */
        uint16_t k = K_ACCEL;
        if (p->speed >= g->gmaxspeed)
            k = fly_at(p, 1, true);
        tick(g, k);

        int agl = p->y - GROUND_H;
        /* Parked height is ground + 13, so it is flying once it is above
         * that; athome clears the moment the throttle opens, which is not
         * the same thing at all. */
        if (rolled < 0 && agl > 14)
            rolled = p->x - start;
        if (cleared16 < 0 && agl >= 16 + 13)
            cleared16 = p->x - start;
        if (cleared50 < 0 && agl >= 50 + 13) {
            cleared50 = p->x - start;
            break;
        }
    }

    printf("take-off\n");
    printf("  leaves the ground after          %4d columns\n", rolled);
    printf("  clears a building (16 high) in   %4d columns\n", cleared16);
    printf("  reaches 50 above the field in    %4d columns\n", cleared50);
    free(g);
}

/* ---- 2. the steepest climb it can hold --------------------------------- */

/* Fly to altitude, settle, then hold `angle` and see what happens. */
static void probe_climb(int angle)
{
    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &probe_map, PLAY_SINGLE, 0);
    object_t *p = game_player(g);

    /* Up to a working height, level off, and let the speed come back. */
    for (int t = 0; t < 600 && flying(p); t++) {
        uint16_t k = (p->speed >= g->gmaxspeed) ? fly_at(p, 1, true) : K_ACCEL;
        if (p->y > 110)
            break;
        tick(g, k);
    }
    for (int t = 0; t < 120 && flying(p); t++)
        tick(g, fly_at(p, 0, true));

    int x0 = p->x, y0 = p->y, minspeed = p->speed;
    bool stalled = false;
    int ticks = 0;

    for (; ticks < 240 && flying(p); ticks++) {
        tick(g, fly_at(p, angle, true));
        if (p->speed < minspeed)
            minspeed = p->speed;
        if (p->state == ST_STALLED)
            stalled = true;
        if (p->y > MAX_Y - 40)
            break;                    /* out of sky, not out of ability */
    }

    int dx = p->x - x0, dy = p->y - y0;
    printf("  %2d  (%4.1f deg) %+5d %+5d  ", angle, angle * 360.0 / ANGLES,
           dx, dy);
    if (dy > 0 && dx > 0)
        printf("1 in %-5.1f", (double)dx / dy);
    else if (dy > 0)
        printf("vertical  ");
    else
        printf("  --      ");
    printf("  min speed %d%s\n", minspeed, stalled ? "   STALLED" : "");
    free(g);
}

/* ---- 3. how much room a turn takes ------------------------------------- */

static void probe_loop(void)
{
    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &probe_map, PLAY_SINGLE, 0);
    object_t *p = game_player(g);

    for (int t = 0; t < 900 && flying(p); t++) {
        uint16_t k = (p->speed >= g->gmaxspeed) ? fly_at(p, 1, true) : K_ACCEL;
        if (p->y > 120)
            break;
        tick(g, k);
    }
    for (int t = 0; t < 120 && flying(p); t++)
        tick(g, fly_at(p, 0, true));

    int minx = p->x, maxx = p->x, miny = p->y, maxy = p->y;
    int start_orient = p->orient;

    /* Stick hard back and hold it: that is a loop. */
    for (int t = 0; t < 400 && flying(p); t++) {
        tick(g, K_ACCEL | K_FLAPU);
        if (p->x < minx) minx = p->x;
        if (p->x > maxx) maxx = p->x;
        if (p->y < miny) miny = p->y;
        if (p->y > maxy) maxy = p->y;
        if (t > 8 && p->angle == 0 && p->orient == start_orient)
            break;                    /* round again, facing the same way */
    }

    printf("turning\n");
    printf("  a loop is                        %4d columns wide\n",
           maxx - minx);
    printf("  and                              %4d high\n", maxy - miny);
    free(g);
}

/* ---- 4. the slope it can follow up a hill ------------------------------ */

/* The question a generator actually asks: the ground rises 1 in N -- can the
 * aeroplane stay above it?  The summit is capped well below the ceiling,
 * because otherwise this measures the height of the sky rather than the
 * ability of the aircraft.  Flown by a pilot who keeps the nose just above
 * the slope, the way a person hedge-hops. */
static bool probe_slope(int run_per_rise, int summit)
{
    memset(ground, GROUND_H, sizeof(ground));
    for (int x = 900; x < MAX_X; x++) {
        int h = GROUND_H + (x - 900) / run_per_rise;
        ground[x] = (uint8_t)(h > summit ? summit : h);
    }

    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &probe_map, PLAY_SINGLE, 0);
    object_t *p = game_player(g);

    bool made_it = false;
    for (int t = 0; t < 2000 && flying(p); t++) {
        uint16_t k;
        if (p->y <= GROUND_H + 14 && p->speed < g->gmaxspeed) {
            k = K_ACCEL;                        /* still on the roll */
        } else {
            /* Looks a long way ahead and keeps a comfortable margin: a
             * pilot flying with his wheels in the heather would measure his
             * own nerve rather than what the aeroplane can do. */
            int ahead = game_ground(g, p->x + 60);
            if (game_ground(g, p->x + 120) > ahead)
                ahead = game_ground(g, p->x + 120);
            int agl = p->y - ahead;
            int want = agl < 35 ? 2 : (agl < 55 ? 1 : 0);
            if (p->speed <= g->gminspeed + 1)
                want = 0;                       /* speed first, height after */
            k = fly_at(p, want, true);
        }
        tick(g, k);

        /* Over the top and still flying, with the summit behind it. */
        if (p->x > 900 + (summit - GROUND_H) * run_per_rise + 300) {
            made_it = true;
            break;
        }
    }

    free(g);
    memset(ground, GROUND_H, sizeof(ground));
    return made_it;
}

/* ---- 5. how high the ground can be and still be flown over ------------- */

/* Holding the stick back turns the aeroplane all the way round; count the
 * heading through a whole revolution rather than waiting for it to come back
 * to a particular number, which it can skip past in a fast roll. */
static bool loop_from_here(game_t *g, object_t *p, int *width, int *height)
{
    int prev = p->angle, turned = 0;
    int minx = p->x, maxx = p->x, miny = p->y, maxy = p->y;

    for (int t = 0; t < 400 && flying(p); t++) {
        tick(g, K_ACCEL | K_FLAPU);

        int d = ((p->angle - prev + ANGLES + ANGLES / 2) % ANGLES)
                - ANGLES / 2;
        prev = p->angle;
        turned += d;

        if (p->x < minx) minx = p->x;
        if (p->x > maxx) maxx = p->x;
        if (p->y < miny) miny = p->y;
        if (p->y > maxy) maxy = p->y;

        if (abs(turned) >= ANGLES) {
            if (width)  *width = maxx - minx;
            if (height) *height = maxy - miny;
            return true;
        }
    }
    return false;
}

/* The world is 200 tall and the aeroplane stalls at the top of it, so ground
 * height is not just scenery: it is a lid on the air above it.  This finds
 * where the lid gets too low to fly under. */
static void probe_headroom(int h)
{
    memset(ground, (uint8_t)h, sizeof(ground));

    game_t *g = calloc(1, sizeof(*g));
    game_start(g, &probe_map, PLAY_SINGLE, 0);
    object_t *p = game_player(g);

    int air = MAX_Y - h;
    int want = h + (air / 2 < 60 ? air / 2 : 60);   /* a working height */
    bool airborne = false;

    for (int t = 0; t < 900 && flying(p); t++) {
        uint16_t k = (p->speed >= g->gmaxspeed) ? fly_at(p, 1, true) : K_ACCEL;
        tick(g, k);
        if (p->y >= want) {
            airborne = true;
            break;
        }
    }

    /* Level off and let the speed come back before asking for a turn -- a
     * loop entered slow is a stall, and that would measure the pilot. */
    for (int t = 0; t < 80 && flying(p); t++)
        tick(g, fly_at(p, 0, true));

    int width = 0, height = 0;
    bool looped = airborne && flying(p) &&
                  loop_from_here(g, p, &width, &height);

    printf("  ground at %3d  (%3d of air)  %-12s  ", h, air,
           airborne ? "climbs out" : "cannot climb");
    if (looped)
        printf("turns round in %d x %d\n", width, height);
    else
        printf("cannot turn round\n");
    free(g);
    memset(ground, GROUND_H, sizeof(ground));
}

int main(void)
{
    memset(ground, GROUND_H, sizeof(ground));
    printf("barnstormer flight envelope  (single player, game 0,"
           " flat ground at %d)\n\n", GROUND_H);

    probe_takeoff();

    printf("\nclimbing -- 240 ticks at each pitch\n");
    printf("  angle           dx    dy   gradient\n");
    for (int a = 1; a <= 4; a++)
        probe_climb(a);

    printf("\n");
    probe_loop();

    printf("\nfollowing a slope that rises 1 in N to a summit of 120\n");
    for (int n = 1; n <= 6; n++)
        printf("  1 in %-2d  %s\n", n,
               probe_slope(n, 120) ? "flown" : "hit the hill");

    printf("\nhow high the ground can be and still be flown over\n");
    for (int h = 60; h <= 180; h += 10)
        probe_headroom(h);
    return 0;
}
