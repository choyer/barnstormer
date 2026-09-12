/*
 * render.h -- software rasteriser and scene composer.
 *
 * The game is drawn into a 32-bit ARGB framebuffer that the platform layer
 * hands straight to a Wayland shared-memory buffer.  Two presentation styles
 * share one scene walk:
 *
 *   RENDER_CLASSIC   opaque sky, the game inside a window
 *   RENDER_BREAKOUT  transparent sky, so the compositor shows the desktop
 *                    behind the aeroplanes; terrain and sprites are filled
 *                    solids rather than outlines
 */
#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "score.h"
#include "sprites.h"

typedef struct {
    uint32_t *px;
    int w, h;
    int stride;        /* in pixels                                       */
} framebuf_t;

typedef enum { RENDER_CLASSIC = 0, RENDER_BREAKOUT = 1 } render_style_t;

typedef struct {
    render_style_t style;
    /* How far from its simulated position to draw everything, measured in
     * simulation ticks and scaled by each object's own velocity.  The world
     * advances 12.14 times a second; offsetting by a fraction of a tick lets
     * the display show intermediate positions without the simulation knowing.
     *
     * Zero always means exactly the state the tick produced -- what the
     * original did, and what --no-smooth restores.  Keeping the units as
     * ticks rather than a 0..1 phase is deliberate: it means changing the
     * smoothing scheme cannot quietly change which value disables it. */
    double lead_ticks;

    /* The same, for things that die the instant they touch something.  They
     * are offset backwards instead, between their previous and current
     * simulated positions, so they are only ever drawn where the simulation
     * has already been -- never past the wall that is about to stop them.
     * Also zero-by-default, for the same reason as above. */
    double trail_ticks;

    bool dials;            /* draw the throttle/airspeed strip      */
    int scale;             /* integer world-pixel magnification            */
    int ox, oy;            /* framebuffer offset of world pixel (0,0)      */
    int view_w, view_h;    /* visible world pixels                         */
    bool solid;            /* fill sprite interiors                        */
} render_ctx_t;

/* Palette slots. */
enum {
    PAL_SKY = 0,      /* at the horizon                                   */
    PAL_SKY_TOP,      /* at the top of the frame                          */
    PAL_TEAM1,       /* the local player's aircraft and buildings         */
    PAL_TEAM1_ACC,   /* its struts, guns and markings                     */
    PAL_TEAM2,       /* the opposition                                    */
    PAL_TEAM2_ACC,
    PAL_NEUTRAL,     /* shared detail: bombs, bullets, explosions         */
    PAL_GROUND,
    PAL_GROUND_EDGE,
    PAL_WILDLIFE,
    PAL_HUD,
    PAL_HUD_DIM,
    PAL_HUD_BG,
    PAL_TITLE2,      /* the second word of the title                      */
    PAL_COUNT,
};

extern uint32_t sw_palette[PAL_COUNT];

/* Choose scale/offsets for a framebuffer of the given size. */
void render_layout(render_ctx_t *c, render_style_t style, int fb_w, int fb_h);

/* Draw a whole frame. */
void render_frame(framebuf_t *fb, const render_ctx_t *c, game_t *g);

/* Title/attract screen; `t` is a monotonically rising frame counter. */
void render_title(framebuf_t *fb, const render_ctx_t *c, unsigned t,
                  int menu_sel);

/* End-of-run summary. */
void render_gameover(framebuf_t *fb, const render_ctx_t *c, game_t *g);

/* The end-of-run screen: what you scored, then the board.  `highlight` is a
 * row to pick out (-1 for none) and `edit_cell` is the initial being typed
 * (0-2, or -1 when the score is not being entered). */
typedef struct {
    const char *headline;        /* GAME OVER / RETIRED / ABANDONED       */
    const char *board_name;
    int   final_score;
    bool  ranked;                /* false prints NOT RANKED               */
    const score_table_t *table;
    int   highlight;
    int   edit_cell;
    bool  saved;                 /* false prints SCORES NOT SAVED         */
    unsigned t;                  /* frame counter, for the caret blink    */
} scoreboard_t;

void render_scores(framebuf_t *fb, const render_ctx_t *c,
                   const scoreboard_t *v);

/* ---- primitives, also used by the HUD and title screens ---------------- */

void fb_clear(framebuf_t *fb, uint32_t argb);
void fb_rect(framebuf_t *fb, int x, int y, int w, int h, uint32_t argb);
void fb_blend_rect(framebuf_t *fb, int x, int y, int w, int h, uint32_t argb);
void fb_text(framebuf_t *fb, int x, int y, int scale, uint32_t argb,
             const char *s);
int  fb_text_width(int scale, const char *s);

#endif /* RENDER_H */
