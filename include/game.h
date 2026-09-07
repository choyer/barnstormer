/*
 * game.h -- the simulation.
 *
 * The game is a pure state machine: it owns no windows, files or audio
 * devices.  Each tick it is handed one control word per player and it
 * produces a new world state plus a list of sound requests.  That separation
 * is what lets the same core drive the windowed and overlay front ends, and
 * is the seam a future netplay layer plugs into (see net.h).
 */
#ifndef GAME_H
#define GAME_H

#include "sopwith.h"
#include "level.h"

/* Simulation rate.  The original advanced the world every 15 units of a
 * counter that the 18.2 Hz PC timer bumped by 10, i.e. 12.14 moves a second.
 * Everything in the physics is tuned around that number. */
#define GAME_TICK_HZ 12.1377

typedef struct {
    int   type;        /* S_* priority, S_NONE when silent               */
    int   tone;        /* PIT divisor, as the original wrote to port 42h */
    bool  restart;     /* true when a new note/effect begins             */
} sound_req_t;

struct game {
    const level_t *level;
    playmode_t mode;

    object_t pool[MAX_OBJS];
    object_t *free_list;
    object_t *top, *bot;       /* master list, in allocation order        */
    object_t xhead, xtail;     /* sentinels for the x-sorted list         */

    object_t *targets[MAX_TARG + MAX_OXEN];
    object_t *nearest[MAX_PLYR];   /* enemy the autopilot has spotted     */
    object_t start[MAX_PLYR];      /* pristine copy of each plane's base  */

    uint8_t ground[MAX_X];         /* mutable: bombs leave craters        */

    int n_players;                 /* human-controlled planes             */
    int player;                    /* index of the local player           */
    int numtarg[2];                /* buildings still standing, per team  */
    endstat_t endsts[MAX_PLYR];
    int endcount;
    bool goingsun;                 /* winner's victory climb              */

    int gamenum;                   /* difficulty level, 0..MAX_GAME       */
    int gminspeed, gmaxspeed;
    int targrnge;                  /* ack-ack engagement range, squared   */
    int maxcrash;

    uint32_t countmove;            /* ticks since the game began          */
    uint32_t explseed;             /* deterministic explosion scatter     */
    uint32_t randseed;

    int displx, disprx, dispdx;    /* viewport left/right and this tick's */
                                   /* scroll delta, in world pixels       */

    bool terrain_dirty;            /* a crater changed the height field   */
    unsigned screen_gen;           /* bumped whenever the view is reset,   */
                                   /* which clears windscreen damage       */
    int shothole;                  /* windscreen damage effects pending    */
    int splatbird;
    int splatox;
    bool oxsplatted;

    bool sound_on;
    sound_req_t sound;             /* loudest request this tick           */
    int  sound_type, sound_parm;
    object_t *sound_obj, *sound_last;

    /* Transient per-object context for the current move.  The original
     * kept these in globals that movepln() and interpret() consulted. */
    int  cur_endstat;
    bool plyrplane, compplane;
    int  currobx;

    bool restarted;                /* the world was rebuilt mid-tick      */
    bool quit;                     /* player pressed the bail-out key     */
    bool over;                     /* run is finished, show the summary   */
    const char *over_msg;
};

/* Build a fresh run.  `level` must outlive the game. */
void game_start(game_t *g, const level_t *level, playmode_t mode, int gamenum);

/* Advance one tick.  `keys[i]` is the K_* bitmask for player i. */
void game_tick(game_t *g, const uint16_t *keys);

/* Restart after a win (advances difficulty) or a loss (back to game 0). */
void game_restart(game_t *g);

/* Give up: ends the run immediately and leaves the final score standing.
 * The same thing happens more slowly if a control word carries K_QUIT, which
 * is the path the original's bail-out key took. */
void game_abandon(game_t *g);

/* Convenience accessors for the renderer and HUD. */
object_t *game_player(game_t *g);
static inline int game_ground(const game_t *g, int x)
{
    if (x < 0) x = 0;
    if (x >= MAX_X) x = MAX_X - 1;
    return g->ground[x];
}

/* ---- internals shared between the game/ translation units -------------- */

object_t *obj_alloc(game_t *g);
void      obj_free(game_t *g, object_t *ob);
void      obj_xinsert(game_t *g, object_t *ob);
void      obj_xremove(game_t *g, object_t *ob);
void      obj_move_xy(object_t *ob, int *x, int *y);
void      obj_set_dxdy(object_t *ob, int xval, int yval);

void      sw_move_all(game_t *g, const uint16_t *keys);
void      sw_collide(game_t *g);
void      sw_autopilot(game_t *g, object_t *ob);
void      sw_aim(game_t *g, object_t *ob, int ax, int ay, object_t *targ, bool longway);
int       sw_range(int x, int y, int ax, int ay);
void      sw_go_home(game_t *g, object_t *ob);

void      sw_init_player(game_t *g, object_t *reuse, int slot);
void      sw_init_comp(game_t *g, object_t *reuse, int slot);
void      sw_init_shot(game_t *g, object_t *ob, object_t *lead);
void      sw_init_bomb(game_t *g, object_t *ob);
void      sw_init_missile(game_t *g, object_t *ob);
void      sw_init_burst(game_t *g, object_t *ob);
void      sw_init_explosion(game_t *g, object_t *ob, bool small);
void      sw_init_smoke(game_t *g, object_t *ob);
void      sw_init_bird(game_t *g, object_t *flock, int i);

void      sw_plane_hit(object_t *ob);
void      sw_plane_stall(game_t *g, object_t *ob);
void      sw_plane_crash(game_t *g, object_t *ob);
void      sw_score(game_t *g, object_t *ob, int points);
void      sw_end_game(game_t *g, int targclr);
void      sw_winner(game_t *g, object_t *ob);
void      sw_loser(game_t *g, object_t *ob);

void      sw_sound_request(game_t *g, int type, int parm, object_t *ob);
void      sw_sound_start(game_t *g, object_t *ob, int type);
void      sw_sound_stop(game_t *g, object_t *ob);
void      sw_sound_resolve(game_t *g);
void      sw_sound_reset(game_t *g);
/* Called at the original's 18.2 Hz timer rate to advance the explosion
 * tune and the machine-gun warble. */
void      sw_sound_adj(game_t *g);
#define SOUND_ADJ_HZ 18.2065

#endif /* GAME_H */
