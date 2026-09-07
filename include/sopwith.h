/*
 * sopwith.h -- shared constants and object model.
 *
 * A re-implementation of Sopwith (David L. Clark, 1984).  The gameplay
 * constants, object states and physics in this file are transcribed from the
 * original DOS sources in ../origsrc (SW.H, SWMOVE.C, SWINIT.C, SWCOLLSN.C)
 * so that flight, gunnery and collision behaviour match the 1984 game.
 *
 * Original Sopwith is Copyright (C) 1984-2000 David L. Clark; see
 * LICENSE.origsopwith.txt.  This is a modified, independently written
 * implementation (2026); no original code is reused verbatim, but the
 * artwork, terrain and tuning values are derived from it.
 */
#ifndef SOPWITH_H
#define SOPWITH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- world geometry (SW.H) ------------------------------------------- */

#define MAX_X        3000   /* world width in pixels                      */
#define MAX_Y        200    /* ceiling; above this the plane stalls       */
#define SCR_WDTH     320    /* viewport width  (one CGA screen)           */
#define SCR_HGHT     200    /* viewport height                            */
#define SCR_CENTR    152    /* column the player's plane is locked to     */
#define SCR_LIMIT    180    /* scrolling stops this far from either edge  */

/* ---- flight model ----------------------------------------------------- */

#define MIN_SPEED     4
#define MAX_SPEED     8
#define MAX_THROTTLE  4
#define ANGLES       16     /* heading is quantised to pi/8 steps         */
#define ORIENTS       2     /* 0 = right-side up, 1 = inverted            */

#define MAXCRCOUNT   10     /* turns spent sitting as a wreck             */
#define FALLCOUNT    10     /* moves between adjustments while falling    */
#define STALLCOUNT    6     /* moves between adjustments while stalled    */
#define TARGHITCOUNT 10     /* bullet damage needed to level a building   */

/* ---- ordnance --------------------------------------------------------- */

#define MAXROUNDS   200
#define MAXBOMBS      5
#define MAXMISSILES   5
#define MAXBURSTS     5
#define MAXFUEL     (3 * MAX_X)
#define MAXCRASH      5

#define BULSPEED     10
#define BULLIFE      10
#define BOMBLIFE      5
#define MISSLIFE     50
#define BURSTLIFE    20
#define EXPLLIFE      3
#define SMOKELIFE    10
#define BIRDLIFE      4
#define FLOCKLIFE     5

/* ---- population limits ------------------------------------------------ */

#define MAX_PLYR      4
#define MAX_TARG     20
#define MAX_OBJS    100
#define MAX_FLCK      4
#define MAX_BIRD      1
#define MAX_OXEN      2
#define MAX_GAME      7

/* ---- autopilot tuning (SWAUTO.C) -------------------------------------- */

#define AI_NEAR       (150 * 150)
#define AI_CLOSE      32
#define AI_HOME       16
#define AI_SAFERESET  32

#define QUIT_LIFE   (-5000) /* fuel value that means "player gave up"      */

/* bird flock patrol limits */
#define MINFLCKX  (0     + SCR_WDTH + 50)
#define MAXFLCKX  (MAX_X - SCR_WDTH - 50)

/* ---- enumerations ----------------------------------------------------- */

typedef enum {
    PLAY_NOVICE = 0,   /* infinite ammo, no stalls, no wildlife           */
    PLAY_SINGLE,       /* one human, no enemy aircraft                    */
    PLAY_COMPUTER,     /* one human against three computer pilots         */
    PLAY_NET,          /* reserved: networked play (see net.h)            */
} playmode_t;

typedef enum {
    OBJ_GROUND = 0,
    OBJ_PLANE,
    OBJ_BOMB,
    OBJ_SHOT,
    OBJ_TARGET,
    OBJ_EXPLOSION,
    OBJ_SMOKE,
    OBJ_FLOCK,
    OBJ_BIRD,
    OBJ_OX,
    OBJ_MISSILE,
    OBJ_STARBURST,
    OBJ_NONE = 99,
} objtype_t;

typedef enum {
    ST_WAITING = 0,
    ST_FLYING = 1,
    ST_HIT = 2,
    ST_CRASHED = 4,
    ST_FALLING = 5,
    ST_STANDING = 6,
    ST_STALLED = 7,
    ST_REBUILDING = 8,
    ST_WOUNDED = 9,
    ST_WOUNDSTALL = 10,
    ST_FINISHED = 91,
} objstate_t;

typedef enum { END_PLAYING = 0, END_WINNER = 1, END_LOSER = 2 } endstat_t;

/* ---- control input ---------------------------------------------------- */

enum {
    K_ACCEL     = 0x0001,
    K_DEACC     = 0x0002,
    K_FLAPU     = 0x0004,
    K_FLAPD     = 0x0008,
    K_FLIP      = 0x0010,
    K_SHOT      = 0x0020,
    K_BOMB      = 0x0100,
    K_HOME      = 0x0200,
    K_SOUND     = 0x0400,
    K_QUIT      = 0x0800,
    K_MISSILE   = 0x1000,
    K_STARBURST = 0x2000,
};

/* ---- sound priorities (lower value wins) ------------------------------ */

enum {
    S_TITLE     = 5,
    S_EXPLOSION = 10,
    S_BOMB      = 20,
    S_SHOT      = 30,
    S_FALLING   = 40,
    S_HIT       = 50,
    S_PLANE     = 60,
    S_NONE      = 32767,
};

/* ---- objects ----------------------------------------------------------
 *
 * Position and velocity are 16.16 fixed point, exactly as the original's
 * movexy/setdxdy pair in SWUTIL.ASM: the integer half is the pixel the
 * object occupies and the fractional half accumulates sub-pixel motion.
 * x/y are the sprite's top-left corner in world space, where y counts
 * *upwards* from the bottom of the world.
 */

typedef struct object object_t;
typedef struct game game_t;

struct object {
    objtype_t  type;
    objstate_t state;

    int32_t x, y;            /* integer world position (sprite top-left)  */
    uint16_t lx, ly;         /* fractional position, 1/65536 pixel        */
    int32_t dx, dy;          /* integer velocity                          */
    uint16_t ldx, ldy;       /* fractional velocity                       */

    int angle;               /* 0..ANGLES-1, counter-clockwise from east  */
    int orient;              /* 0 upright, 1 inverted                     */
    int speed;
    int accel;               /* throttle 0..MAX_THROTTLE                  */
    int flaps;               /* -1, 0 or +1 stick input for this tick     */

    int clr;                 /* team colour: 1 = blue side, 2 = red side  */
    int score;
    int rounds, bombs, missiles, bursts;
    int life;                /* fuel for planes, countdown for munitions  */
    int hitcount;
    int crashcnt;

    int symw, symh;          /* collision/blit extent in pixels           */
    int sprite_set;          /* sprites.h SPRITE_* id, -1 for point sprites */
    int sprite_frame;

    bool firing;             /* trigger held this tick                    */
    bool bombing;
    bool bfiring;            /* starburst flare                           */
    object_t *mfiring;       /* missile lock target, NULL if not firing   */
    object_t *target;        /* missile: object being chased              */
    object_t *owner;
    object_t *shot_at;       /* target guns: who they are aiming at       */

    int bdelay, mdelay, bsdelay;  /* per-weapon cooldowns                 */
    bool athome;             /* parked on own runway                      */
    bool home;               /* autoland requested                        */
    bool ai;                 /* flown by the autopilot                    */

    int index;               /* slot in game->pool, stable for a lifetime */
    bool alive;

    int sound_tone;          /* continuous-tone state for bombs/dives     */
    int sound_chng;
    bool has_sound;

    object_t *next, *prev;   /* master list                               */
    object_t *xnext, *xprev; /* list sorted by x, for collision sweeping   */
};

/* ---- helpers ---------------------------------------------------------- */

extern const int sw_sintab[ANGLES];

#define SW_SIN(a) (sw_sintab[(a) % ANGLES])
#define SW_COS(a) (sw_sintab[((a) + (ANGLES / 4)) % ANGLES])

static inline int sw_abs(int v) { return v < 0 ? -v : v; }

#endif /* SOPWITH_H */
