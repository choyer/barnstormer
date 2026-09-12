/*
 * scene.c -- composing a frame.
 *
 * One scene walk serves both presentation styles.  The only differences are
 * what the sky is filled with (an opaque colour, or nothing at all so the
 * compositor shows the desktop) and whether sprites are drawn from the
 * outline artwork or its filled counterpart.
 *
 * World space has y increasing upwards from the bottom of the map; screen
 * space does the opposite, so every draw goes through world_row().
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render.h"

/* ---- viewport ---------------------------------------------------------- */

void render_layout(render_ctx_t *c, render_style_t style, int fb_w, int fb_h)
{
    c->style = style;

    int s = fb_h / SCR_HGHT;
    int sx = fb_w / SCR_WDTH;
    if (sx < s) s = sx;
    if (s < 1) s = 1;

    c->scale  = s;
    c->view_w = fb_w / s;
    c->view_h = fb_h / s;
    c->ox = (fb_w - c->view_w * s) / 2;
    c->oy = (fb_h - c->view_h * s) / 2;

    c->solid = (style == RENDER_BREAKOUT);
    c->lead_ticks = 0.0;        /* exactly as simulated, until asked */
    c->trail_ticks = 0.0;
    c->dials = false;
}

/* Where something is `ticks` of a tick from its simulated position, given its
 * per-tick delta.  Reconstructing from velocity rather than from a stored
 * previous position keeps this entirely inside the renderer: nothing has to be
 * remembered between ticks, and objects that spawn or teleport need no special
 * case.
 *
 * Which way to offset matters at this tick rate.  Trailing the simulation, the
 * usual way to interpolate, shows a state up to a whole tick old -- 82ms here,
 * enough to feel sluggish against the original, which drew each tick exactly
 * as simulated.  Leading removes that but draws up to a tick of future that
 * has not been tested against anything.  Centring splits the difference: half
 * a tick either side, averaging exactly the original's timing, with half the
 * overshoot. */
static inline double lead(double cur, double delta, double ticks)
{
    return cur + ticks * delta;
}

/* An object's movement per tick, in world pixels.  The simulation keeps a
 * 16-bit fraction it carries between ticks, so the true speed is the integer
 * step plus that fraction -- which is exactly the jitter worth smoothing. */
/* Whether it is safe to draw this object ahead of the simulation.
 *
 * Anything that dies the instant it touches something is not: the simulation
 * tests a shot against the terrain at its own position, so drawing it a tick
 * further on puts it inside walls and under the ground for a frame before the
 * tick that removes it.  A bullet covers ten world pixels a tick, which on a
 * full-screen overlay is eighty pixels of visible penetration.
 *
 * They are drawn trailing instead of simply pinned to the simulated position:
 * pinning them while the camera moves smoothly is worse than either, because
 * the shot then slides backwards with the camera all tick and jumps forward
 * when the tick fires.  Trailing keeps them continuous and keeps them behind
 * the collision test. */
static inline bool leads(const object_t *ob)
{
    return ob->type != OBJ_SHOT && ob->type != OBJ_BOMB &&
           ob->type != OBJ_MISSILE;
}

static inline double vel_x(const object_t *ob)
{
    return ob->dx + ob->ldx / 65536.0;
}
static inline double vel_y(const object_t *ob)
{
    return ob->dy + ob->ldy / 65536.0;
}

/* Left-hand world column of the view, centred on wherever the game has put
 * its 320-wide window. */
static double view_left(const render_ctx_t *c, const game_t *g)
{
    double left = lead(g->displx, g->dispdx, c->lead_ticks)
                  + SCR_WDTH / 2 - c->view_w / 2;
    double max = MAX_X - c->view_w;
    if (max < 0) max = 0;
    if (left < 0) left = 0;
    if (left > max) left = max;
    return left;
}

static inline int screen_x(const render_ctx_t *c, double left, double wx)
{
    return c->ox + (int)lround((wx - left) * c->scale);
}

static inline int screen_y(const render_ctx_t *c, double wy)
{
    return c->oy + (int)lround((c->view_h - 1 - wy) * c->scale);
}

/* ---- colours ----------------------------------------------------------- */

static uint32_t obj_color(const object_t *ob, int v)
{
    if (ob->clr == 9)
        return sw_palette[PAL_WILDLIFE];
    if (v >= 3)
        return sw_palette[PAL_NEUTRAL];
    bool enemy = (ob->clr == 2);
    if (v == 2)
        return sw_palette[enemy ? PAL_TEAM2_ACC : PAL_TEAM1_ACC];
    return sw_palette[enemy ? PAL_TEAM2 : PAL_TEAM1];
}

/* A vertical wash from near-black at the top to a lighter horizon.  It costs
 * no more than a flat fill and stops a tall window looking like a mistake. */
static void draw_sky(framebuf_t *fb, const render_ctx_t *c)
{
    if (c->style == RENDER_BREAKOUT) {
        fb_clear(fb, 0u);
        return;
    }

    uint32_t top = sw_palette[PAL_SKY_TOP], bot = sw_palette[PAL_SKY];
    for (int y = 0; y < fb->h; y++) {
        int f = fb->h > 1 ? y * 255 / (fb->h - 1) : 255;
        uint32_t col = 0xFF000000u;
        for (int sh = 16; sh >= 0; sh -= 8) {
            int a = (int)((top >> sh) & 0xFF);
            int b = (int)((bot >> sh) & 0xFF);
            col |= (uint32_t)(a + (b - a) * f / 255) << sh;
        }
        fb_rect(fb, 0, y, fb->w, 1, col);
    }
}

/* ---- terrain ----------------------------------------------------------- */

static void draw_ground(framebuf_t *fb, const render_ctx_t *c, game_t *g,
                        double left)
{
    int s = c->scale;
    uint32_t body = sw_palette[PAL_GROUND];
    uint32_t edge = sw_palette[PAL_GROUND_EDGE];

    /* One column past the right edge: a camera part way between world pixels
     * shifts everything left, and without it the last column leaves a gap. */
    int first = (int)floor(left);
    for (int wx = first; wx <= first + c->view_w; wx++) {
        if (wx < 0 || wx >= MAX_X)
            continue;
        int h = g->ground[wx];
        if (h > c->view_h - 1)
            h = c->view_h - 1;

        int x = screen_x(c, left, wx);
        int top = screen_y(c, h);
        int bottom = c->oy + c->view_h * s;

        /* Two world rows of turf, then earth all the way down. */
        fb_rect(fb, x, top, s, 2 * s, edge);
        if (bottom - (top + 2 * s) > 0)
            fb_rect(fb, x, top + 2 * s, s, bottom - (top + 2 * s), body);
    }
}

/* ---- objects ----------------------------------------------------------- */

static void draw_object(framebuf_t *fb, const render_ctx_t *c,
                        const object_t *ob, double left)
{
    int s = c->scale;

    /* Both the object and the camera are wound back by the same fraction of
     * a tick, so anything the camera is pinned to (the player) stays put on
     * screen while the world scrolls under it. */
    double a = leads(ob) ? c->lead_ticks : c->trail_ticks;
    double wx = lead(ob->x, vel_x(ob), a);
    double wy = lead(ob->y, vel_y(ob), a);

    if (ob->sprite_set < 0) {
        /* Bullets and smoke are single pixels. */
        if (ob->x < left - 1 || ob->x >= left + c->view_w + 1)
            return;
        if (ob->y < -1 || ob->y >= c->view_h + 1)
            return;
        uint32_t col = ob->type == OBJ_SMOKE ? sw_palette[PAL_HUD_DIM]
                                             : sw_palette[PAL_NEUTRAL];
        fb_rect(fb, screen_x(c, left, wx), screen_y(c, wy), s, s, col);
        return;
    }

    const sprite_set_t *set = &sw_sprite_sets[ob->sprite_set];
    const uint8_t *px = c->solid ? sprite_frame_solid(ob->sprite_set,
                                                      ob->sprite_frame)
                                 : sprite_frame(ob->sprite_set,
                                                ob->sprite_frame);

    int bx = screen_x(c, left, wx);
    int by = screen_y(c, wy);

    for (int row = 0; row < set->h; row++) {
        int cull = ob->y - row;
        if (cull < -1 || cull >= c->view_h + 1)
            continue;
        int y = by + row * s;
        for (int col = 0; col < set->w; col++) {
            uint8_t v = px[row * set->w + col];
            if (!v)
                continue;
            int cx = ob->x + col;
            if (cx < left - 1 || cx >= left + c->view_w + 1)
                continue;
            fb_rect(fb, bx + col * s, y, s, s, obj_color(ob, v));
        }
    }
}

/* ---- head-up display --------------------------------------------------- */

static void gauge(framebuf_t *fb, int x, int y, int w, int h, int ts,
                  const char *label, int cur, int max, uint32_t col)
{
    fb_text(fb, x, y, ts, sw_palette[PAL_HUD_DIM], label);
    int bx = x + 5 * 6 * ts;
    fb_blend_rect(fb, bx, y, w, h, 0x40FFFFFF);   /* empty-gauge trough    */
    if (max <= 0)
        return;
    int filled = cur <= 0 ? 0 : (cur >= max ? w : cur * w / max);
    if (filled > 0)
        fb_rect(fb, bx, y, filled, h, col);
}

/* A compressed silhouette of the whole map with a blip per object, the
 * descendant of the original's "world display" strip. */
static void draw_radar(framebuf_t *fb, const render_ctx_t *c, game_t *g,
                       int x, int y, int w, int h, int left)
{
    fb_blend_rect(fb, x, y, w, h, 0x60000000);

    for (int col = 0; col < w; col++) {
        int wx0 = (int)((int64_t)col * MAX_X / w);
        int wx1 = (int)((int64_t)(col + 1) * MAX_X / w);
        int maxh = 0;
        for (int wx = wx0; wx < wx1 && wx < MAX_X; wx++)
            if (g->ground[wx] > maxh)
                maxh = g->ground[wx];
        int gh = maxh * h / MAX_Y;
        if (gh > h) gh = h;
        fb_rect(fb, x + col, y + h - gh, 1, gh, sw_palette[PAL_GROUND]);
    }

    for (object_t *ob = g->top; ob; ob = ob->next) {
        uint32_t col;
        switch (ob->type) {
        case OBJ_PLANE:
            if (ob->state == ST_FINISHED)
                continue;
            col = obj_color(ob, 1);
            break;
        case OBJ_TARGET:
            if (ob->state != ST_STANDING)
                continue;
            col = obj_color(ob, 1);
            break;
        default:
            continue;
        }
        int bx = x + (int)((int64_t)ob->x * w / MAX_X);
        int by = y + h - 1 - (int)((int64_t)ob->y * h / MAX_Y);
        int size = ob->type == OBJ_PLANE ? 3 : 2;
        fb_rect(fb, bx, by - size / 2, size, size, col);
    }

    /* Where the camera is looking. */
    int vx = x + (int)((int64_t)left * w / MAX_X);
    int vw = (int)((int64_t)c->view_w * w / MAX_X);
    if (vw < 2) vw = 2;
    fb_rect(fb, vx, y, vw, 1, sw_palette[PAL_HUD]);
    fb_rect(fb, vx, y + h - 1, vw, 1, sw_palette[PAL_HUD]);
    fb_rect(fb, vx, y, 1, h, sw_palette[PAL_HUD]);
    fb_rect(fb, vx + vw - 1, y, 1, h, sw_palette[PAL_HUD]);
}

/*
 * The throttle and airspeed strip, sitting flush on top of the panel.
 *
 * Throttle is five discrete detents; airspeed is a continuous thing that
 * chases a target the throttle only partly sets, one step every fourth tick.
 * Showing both is what makes the flight model legible: the bug marks what the
 * lever asked for, the fill shows what the aircraft has actually got, and the
 * gap between them is the lag and the pitch working against each other.
 *
 * The shaded region is below the stall floor -- fall in there and the aircraft
 * departs.  It is hidden in novice mode, which cannot stall at all.
 */
static void draw_dials(framebuf_t *fb, const render_ctx_t *c, game_t *g,
                       int ts, int pad, int y0)
{
    const object_t *p = game_player(g);
    int barh = 5 * ts;
    int bw = 60 * ts;
    int lx = pad;                       /* label column, as the gauges use */
    int bx = lx + 5 * 6 * ts;           /* bar column, ditto               */
    int spd_y = y0 - 6 * ts - barh;     /* floating just above the panel   */
    int thr_y = spd_y - 9 * ts;

    uint32_t mine = sw_palette[PAL_TEAM1];
    uint32_t warn = sw_palette[PAL_TEAM2];

    /* ---- throttle: one pip per unit of thrust above the minimum ----
     *
     * The lever has five positions, 0 to MAX_THROTTLE, but that is five
     * *values* and only four units: idle is no pips lit, full throttle is all
     * of them.  Drawing five cells leaves the last one permanently dark. */
    fb_text(fb, lx, thr_y, ts, sw_palette[PAL_HUD_DIM], "THR");
    int gap = 2 * ts;
    int pipw = (bw - (MAX_THROTTLE - 1) * gap) / MAX_THROTTLE;
    for (int i = 0; i < MAX_THROTTLE; i++) {
        int px = bx + i * (pipw + gap);
        if (i < p->accel)
            fb_rect(fb, px, thr_y, pipw, barh, mine);
        else
            fb_blend_rect(fb, px, thr_y, pipw, barh, 0x40FFFFFF);
    }

    /* ---- airspeed: where it is, where it was asked to be, where it dies ---- */
    fb_text(fb, lx, spd_y, ts, sw_palette[PAL_HUD_DIM], "SPD");

    int span = g->gminspeed + 8;        /* a full-throttle dive, near enough */
    if (span < 1)
        span = 1;
    fb_blend_rect(fb, bx, spd_y, bw, barh, 0x40FFFFFF);

    int spd = p->speed;
    if (spd < 0) spd = 0;
    if (spd > span) spd = span;
    int fill = spd * bw / span;
    if (fill > 0)
        fb_rect(fb, bx, spd_y, fill, barh,
                (g->mode != PLAY_NOVICE && p->speed < g->gminspeed) ? warn : mine);

    /* The stall floor goes on top of the fill, not under it.  Underneath, it
     * disappears the moment you are flying fast enough to cover it -- which
     * is exactly when it is worth seeing, because the whole point is watching
     * the airspeed come down towards it. */
    if (g->mode != PLAY_NOVICE) {
        int stall = g->gminspeed * bw / span;
        if (stall > 0)
            fb_blend_rect(fb, bx, spd_y, stall, barh,
                          0x66000000u | (warn & 0x00FFFFFFu));
    }

    /* The bug marks what the lever asked for, not where pitch is dragging
     * the aircraft: that part is the fill moving towards or away from it. */
    int want = g->gminspeed + p->accel;
    if (want > span)
        want = span;
    int wx = bx + want * bw / span;
    int tickw = ts < 1 ? 1 : ts;
    fb_rect(fb, wx - tickw / 2, spd_y - 2 * ts, tickw, barh + 4 * ts,
            sw_palette[PAL_HUD]);
}

static void draw_hud(framebuf_t *fb, const render_ctx_t *c, game_t *g,
                     int left)
{
    object_t *p = game_player(g);

    /* Size the panel against the framebuffer, not the world scale: a tall
     * narrow window has a small world scale but plenty of room for text. */
    int ts = fb->w / 480;
    if (ts < 1) ts = 1;
    if (ts > 4) ts = 4;

    int pad = 4 * ts;
    int rowh = 9 * ts;
    int band = 2 * pad + 3 * rowh;
    while (band > fb->h / 3 && ts > 1) {
        ts--;
        pad = 4 * ts;
        rowh = 9 * ts;
        band = 2 * pad + 3 * rowh;
    }
    int y0 = fb->h - band;

    /* Over a desktop the earth alone is not enough contrast for text. */
    if (c->style == RENDER_BREAKOUT)
        fb_blend_rect(fb, 0, y0, fb->w, band, sw_palette[PAL_HUD_BG]);
    else
        fb_blend_rect(fb, 0, y0, fb->w, band, 0x90000000);
    fb_rect(fb, 0, y0, fb->w, 1, sw_palette[PAL_HUD_DIM]);

    if (c->dials)
        draw_dials(fb, c, g, ts, pad, y0);

    int barw = 30 * ts;
    int barh = 5 * ts;
    int gx = pad;
    int gy = y0 + pad;

    uint32_t mine = sw_palette[PAL_TEAM1];
    gauge(fb, gx, gy,            barw, barh, ts, "FUEL", p->life, MAXFUEL,
          p->life < MAXFUEL / 8 ? sw_palette[PAL_TEAM2] : mine);
    gauge(fb, gx, gy + rowh,     barw, barh, ts, "AMMO", p->rounds,
          MAXROUNDS, mine);
    gauge(fb, gx, gy + 2 * rowh, barw, barh, ts, "LIFE",
          g->maxcrash - p->crashcnt, g->maxcrash, mine);

    gx += 5 * 6 * ts + barw + 8 * ts;
    gauge(fb, gx, gy,            barw, barh, ts, "BOMB", p->bombs,
          MAXBOMBS, mine);
    gauge(fb, gx, gy + rowh,     barw, barh, ts, "MSSL", p->missiles,
          MAXMISSILES, mine);
    gauge(fb, gx, gy + 2 * rowh, barw, barh, ts, "FLAR", p->bursts,
          MAXBURSTS, mine);

    gx += 5 * 6 * ts + barw + 10 * ts;

    char buf[64];
    snprintf(buf, sizeof(buf), "SCORE %6d", p->score);
    fb_text(fb, gx, gy, ts, sw_palette[PAL_HUD], buf);
    snprintf(buf, sizeof(buf), "GAME  %6d", g->gamenum);
    fb_text(fb, gx, gy + rowh, ts, sw_palette[PAL_HUD_DIM], buf);
    snprintf(buf, sizeof(buf), "TARGETS%5d", g->numtarg[1] > 0 ? g->numtarg[1] : 0);
    fb_text(fb, gx, gy + 2 * rowh, ts, sw_palette[PAL_HUD_DIM], buf);

    int rx = gx + 13 * 6 * ts + 8 * ts;
    int rw = fb->w - rx - pad;
    if (rw > 40)
        draw_radar(fb, c, g, rx, y0 + pad, rw, band - 2 * pad, left);
}

/* ---- damage overlays --------------------------------------------------- */

/* The original splattered the windscreen with bullet holes and unlucky
 * birds.  We keep the effect, drawn straight onto the framebuffer. */
static void draw_windscreen(framebuf_t *fb, const render_ctx_t *c, game_t *g)
{
    enum { MAX_MARKS = 24 };
    static uint32_t seed = 74917777u;
    static struct { int x, y, kind; } marks[MAX_MARKS];
    static int nmarks;
    static unsigned gen;

    if (gen != g->screen_gen) {
        gen = g->screen_gen;
        nmarks = 0;
    }

    int span_x = fb->w - 32 * c->scale;
    int span_y = fb->h / 2;
    while ((g->shothole > 0 || g->splatbird > 0) && nmarks < MAX_MARKS &&
           span_x > 1 && span_y > 1) {
        seed = seed * 1103515245u + 12345u;
        marks[nmarks].x = (int)((seed >> 8) % (unsigned)span_x);
        marks[nmarks].y = (int)((seed >> 3) % (unsigned)span_y);
        marks[nmarks].kind = g->shothole > 0 ? SPRITE_SHOTHOLE : SPRITE_SPLAT;
        if (g->shothole > 0) g->shothole--; else g->splatbird--;
        nmarks++;
    }
    g->shothole = g->splatbird = 0;

    /* Hitting one of the oxen wrecks the whole view.  The original shipped
     * the routine for this but never called it; it is too good a joke to
     * leave out, so it fires here and clears when you take off again. */
    if (g->splatox > 0) {
        g->splatox = 0;
        g->oxsplatted = true;
    }
    if (g->oxsplatted) {
        int s = c->scale;
        for (int y = 0; y < fb->h; y += s * 2)
            for (int x = 0; x < fb->w; x += s * 2)
                fb_blend_rect(fb, x, y, s, s, 0xB0B4483C);
    }

    for (int i = 0; i < nmarks; i++) {
        const sprite_set_t *set = &sw_sprite_sets[marks[i].kind];
        const uint8_t *px = sprite_frame(marks[i].kind, 0);
        uint32_t col = marks[i].kind == SPRITE_SHOTHOLE
                           ? 0xD0DCE4F0 : 0xC0D0405C;
        for (int r = 0; r < set->h; r++)
            for (int cc = 0; cc < set->w; cc++)
                if (px[r * set->w + cc])
                    fb_blend_rect(fb, marks[i].x + cc * c->scale,
                                  marks[i].y + r * c->scale,
                                  c->scale, c->scale, col);
    }
}

/* ---- frame ------------------------------------------------------------- */

void render_frame(framebuf_t *fb, const render_ctx_t *c, game_t *g)
{
    draw_sky(fb, c);

    double left = view_left(c, g);

    draw_ground(fb, c, g, left);

    /* Static scenery first so aircraft pass in front of it. */
    for (object_t *ob = g->top; ob; ob = ob->next)
        if ((ob->type == OBJ_TARGET || ob->type == OBJ_OX) &&
            ob->state != ST_FINISHED)
            draw_object(fb, c, ob, left);

    for (object_t *ob = g->top; ob; ob = ob->next) {
        if (ob->type == OBJ_TARGET || ob->type == OBJ_OX)
            continue;
        if (ob->type == OBJ_PLANE && ob->state == ST_FINISHED)
            continue;
        if (ob->sprite_set < 0 && ob->type != OBJ_SHOT &&
            ob->type != OBJ_SMOKE)
            continue;
        draw_object(fb, c, ob, left);
    }

    draw_windscreen(fb, c, g);
    draw_hud(fb, c, g, (int)lround(left));

    /* The loser's flash while the countdown runs; once the run is over the
     * summary screen says it instead. */
    if (!g->over && g->endsts[g->player] == END_LOSER && g->over_msg) {
        int ts = c->scale;
        int w = fb_text_width(ts * 2, g->over_msg);
        fb_text(fb, (fb->w - w) / 2, fb->h / 3, ts * 2,
                sw_palette[PAL_TEAM2], g->over_msg);
    }
}

/* ---- title and summary ------------------------------------------------- */

static const char *const title_menu[] = {
    "NOVICE PILOT",
    "SINGLE PLAYER",
    "AGAINST THE COMPUTER",
};
const int sw_title_menu_len = 3;

/* Lay the title out as a centred stack so it sits correctly whatever shape
 * of window (or whole screen) it lands in. */
/* A key and what it does, for the title screen's control block.  They are
 * kept as pairs rather than as one string so the key can be drawn bright and
 * the action dim: on a list this long, the keys are what the eye is hunting
 * for, and giving them the contrast makes the block scannable. */
typedef struct {
    const char *key;
    const char *act;
} control_t;

#define CTL_COLS   3
#define CTL_ROWS   5
#define CTL_GAP    3        /* blank characters between columns             */

typedef struct {
    const char *text;   /* NULL when this row is CTL_COLS control pairs    */
    const control_t *ctl;
    int scale;          /* multiples of the base text size                */
    int pal;
    int gap;            /* extra space below, in base text units          */
} title_line_t;

/* Column widths are measured from the keys and actions actually in each
 * column rather than fixed, so a one-character key is not left stranded
 * five spaces from what it does.  Returns the block width in characters. */
static int ctl_layout(int keyw[CTL_COLS], int xoff[CTL_COLS]);

static const control_t sw_controls[] = {
    { ",",     "PULL UP"  }, { "/",  "DIVE"    }, { ".",   "FLIP"    },
    { "X",     "FASTER"   }, { "Z",  "SLOWER"  }, { "SPACE", "GUNS"  },
    { "B",     "BOMB"     }, { "V",  "MISSILE" }, { "C",   "FLARE"   },
    { "H",     "HOME"     }, { "S",  "SOUND"   }, { "P",   "PAUSE"   },
    { "D",     "DIALS"    }, { "F2", "VIEW"    }, { "ESC", "END RUN" },
};

static int ctl_layout(int keyw[CTL_COLS], int xoff[CTL_COLS])
{
    int x = 0;
    for (int c = 0; c < CTL_COLS; c++) {
        size_t k = 0, a = 0;
        for (int r = 0; r < CTL_ROWS; r++) {
            const control_t *e = &sw_controls[r * CTL_COLS + c];
            if (strlen(e->key) > k) k = strlen(e->key);
            if (strlen(e->act) > a) a = strlen(e->act);
        }
        keyw[c] = (int)k + 1;                  /* one space after the key */
        xoff[c] = x;
        x += keyw[c] + (int)a + (c < CTL_COLS - 1 ? CTL_GAP : 0);
    }
    return x;
}

void render_title(framebuf_t *fb, const render_ctx_t *c, unsigned t,
                  int menu_sel)
{
    draw_sky(fb, c);

    int ts = c->scale;
    if (ts < 1) ts = 1;
    if (ts > 4) ts = 4;

    char menu[3][40];
    for (int i = 0; i < sw_title_menu_len; i++)
        snprintf(menu[i], sizeof(menu[i]), "%s%s",
                 i == menu_sel ? "> " : "  ", title_menu[i]);

    title_line_t lines[] = {
        { "SOPWITH",                          NULL, 4, PAL_TEAM1,   0 },
        { "BARNSTORMER",                      NULL, 3, PAL_TITLE2,  2 },
        { "(C) COPYRIGHT 1984-2000 DAVID L. CLARK", NULL,
                                                    1, PAL_HUD_DIM, 0 },
        { "WAYLAND IMPLEMENTATION 2026 CARL HOYER", NULL,
                                                    1, PAL_HUD_DIM, 3 },
        { menu[0], NULL, 2, menu_sel == 0 ? PAL_HUD : PAL_HUD_DIM, 0 },
        { menu[1], NULL, 2, menu_sel == 1 ? PAL_HUD : PAL_HUD_DIM, 0 },
        { menu[2], NULL, 2, menu_sel == 2 ? PAL_HUD : PAL_HUD_DIM, 3 },
        { NULL, &sw_controls[0],  1, PAL_HUD_DIM, 0 },
        { NULL, &sw_controls[3],  1, PAL_HUD_DIM, 0 },
        { NULL, &sw_controls[6],  1, PAL_HUD_DIM, 0 },
        { NULL, &sw_controls[9],  1, PAL_HUD_DIM, 0 },
        { NULL, &sw_controls[12], 1, PAL_HUD_DIM, 3 },
        { "PRESS ENTER TO FLY",               NULL, 2, PAL_TEAM1,   0 },
    };
    const int n = (int)(sizeof(lines) / sizeof(lines[0]));
    const int nblink = 1;      /* trailing lines that blink               */

    /* Shrink until the whole stack fits with room to breathe, so the title
     * reads the same in a small window and on a 4K overlay. */
    int total = 0, widest = 0;
    for (;;) {
        total = 0;
        widest = 0;
        for (int i = 0; i < n; i++) {
            total += (7 + 3 + lines[i].gap * 3) * ts * lines[i].scale;
            int sc = ts * lines[i].scale;
            int kw[CTL_COLS], xo[CTL_COLS];
            int w = lines[i].ctl ? ctl_layout(kw, xo) * 6 * sc
                                 : fb_text_width(sc, lines[i].text);
            if (w > widest)
                widest = w;
        }
        if (ts <= 1)
            break;
        if (total + 60 * ts <= fb->h && widest + 16 * ts <= fb->w)
            break;
        ts--;
    }

    int y = (fb->h - total) / 2;
    if (y < 30 * ts)
        y = 30 * ts;
    int cx = fb->w / 2;

    /* Over a desktop the text needs something to sit on. */
    if (c->style == RENDER_BREAKOUT)
        fb_blend_rect(fb, cx - widest / 2 - 8 * ts, y - 8 * ts,
                      widest + 16 * ts, total + 16 * ts,
                      sw_palette[PAL_HUD_BG]);

    /* A lone aeroplane drifting across, because an empty title is a dull
     * one.  It flies the same sixteen-heading artwork as the game. */
    {
        const sprite_set_t *set = &sw_sprite_sets[SPRITE_PLANE];
        const uint8_t *px = c->solid ? sprite_frame_solid(SPRITE_PLANE, 0)
                                     : sprite_frame(SPRITE_PLANE, 0);
        int ps = ts * 2;
        int span = fb->w + set->w * ps * 2;
        int px0 = (int)((t * ts) / 2) % span - set->w * ps;
        int py0 = y - 26 * ts + (int)(3 * ts * ((t / 20) % 3));
        for (int r = 0; r < set->h; r++)
            for (int col = 0; col < set->w; col++) {
                uint8_t v = px[r * set->w + col];
                if (!v)
                    continue;
                fb_rect(fb, px0 + col * ps, py0 + r * ps, ps, ps,
                        sw_palette[v == 2 ? PAL_TEAM1_ACC
                                          : (v >= 3 ? PAL_NEUTRAL : PAL_TEAM1)]);
            }
    }

    for (int i = 0; i < n; i++) {
        int sc = ts * lines[i].scale;
        bool blink = (i >= n - nblink);
        if (lines[i].ctl) {
            /* Key bright, action dim: the keys are what the eye hunts for. */
            int kw[CTL_COLS], xo[CTL_COLS];
            int x = cx - ctl_layout(kw, xo) * 6 * sc / 2;
            for (int col = 0; col < CTL_COLS; col++) {
                const control_t *k = &lines[i].ctl[col];
                int kx = x + xo[col] * 6 * sc;
                /* Keys sit right against their action: the field is as wide
                 * as the longest key in the column, so short ones are pushed
                 * to its right rather than left stranded beside it. */
                int pad_k = (kw[col] - 1 - (int)strlen(k->key)) * 6 * sc;
                fb_text(fb, kx + pad_k, y, sc, sw_palette[PAL_HUD], k->key);
                fb_text(fb, kx + kw[col] * 6 * sc, y, sc,
                        sw_palette[PAL_HUD_DIM], k->act);
            }
        } else if (!blink || ((t / 24) & 1)) {
            fb_text(fb, cx - fb_text_width(sc, lines[i].text) / 2, y, sc,
                    sw_palette[lines[i].pal], lines[i].text);
        }
        y += (7 + 3 + lines[i].gap * 3) * sc;
    }
}

void render_gameover(framebuf_t *fb, const render_ctx_t *c, game_t *g)
{
    int ts = c->scale;
    if (ts < 1) ts = 1;
    int cx = fb->w / 2;

    fb_blend_rect(fb, 0, 0, fb->w, fb->h, 0xB0000000);

    const char *msg = g->over_msg ? g->over_msg : "GAME OVER";
    fb_text(fb, cx - fb_text_width(ts * 3, msg) / 2, fb->h / 3, ts * 3,
            sw_palette[PAL_TEAM2], msg);

    char buf[64];
    snprintf(buf, sizeof(buf), "FINAL SCORE %d", game_player(g)->score);
    fb_text(fb, cx - fb_text_width(ts * 2, buf) / 2, fb->h / 3 + 30 * ts,
            ts * 2, sw_palette[PAL_HUD], buf);

    const char *again = "ENTER TO PLAY AGAIN    ESC FOR MENU";
    fb_text(fb, cx - fb_text_width(ts, again) / 2, fb->h / 3 + 60 * ts,
            ts, sw_palette[PAL_HUD_DIM], again);
}

/* ---- the high score board ---------------------------------------------- */

/*
 * The end-of-run screen.  Laid out as a centred stack that shrinks until it
 * fits, the same trick the title screen uses, so it reads the same in a small
 * window and on a 4K overlay.  The 5x7 font advances a fixed six units a
 * character, so the columns are aligned by padding the row strings rather
 * than by measuring them.
 */
void render_scores(framebuf_t *fb, const render_ctx_t *c, const scoreboard_t *v)
{
    int ts = c->scale;
    if (ts < 1) ts = 1;
    if (ts > 4) ts = 4;

    char scoreline[48], heading[48];
    char rows[SCORE_ROWS][32];

    snprintf(scoreline, sizeof(scoreline), "FINAL SCORE %d", v->final_score);
    snprintf(heading, sizeof(heading), "%s HIGH SCORES", v->board_name);
    for (int i = 0; i < SCORE_ROWS; i++)
        snprintf(rows[i], sizeof(rows[i]), "%2d  %s %8d",
                 i + 1, v->table->e[i].name, v->table->e[i].score);

    /* Rank, name, score: the name starts at this character offset, which is
     * what puts the editing caret under the right initial. */
    const int name_col = 4;

    title_line_t lines[SCORE_ROWS + 6];
    int n = 0;
    /* Named fields throughout: this struct is shared with the title screen
     * and gained a member once already. */
    lines[n++] = (title_line_t){ .text = v->headline, .scale = 3,
                                 .pal = PAL_TEAM2, .gap = 1 };
    lines[n++] = (title_line_t){ .text = scoreline, .scale = 2,
                                 .pal = PAL_HUD, .gap = 0 };
    if (!v->ranked)
        lines[n++] = (title_line_t){ .text = "NOT RANKED", .scale = 1,
                                     .pal = PAL_HUD_DIM, .gap = 0 };
    lines[n++] = (title_line_t){ .text = heading, .scale = 1,
                                 .pal = PAL_TITLE2, .gap = 2 };

    int first_row = n;
    for (int i = 0; i < SCORE_ROWS; i++)
        lines[n++] = (title_line_t){
            .text = rows[i], .scale = 2,
            .pal = i == v->highlight ? PAL_TEAM1 : PAL_HUD_DIM,
            .gap = i == SCORE_ROWS - 1 ? 2 : 0,
        };

    if (v->edit_cell >= 0)
        lines[n++] = (title_line_t){
            .text = "UP DOWN LETTER PICKS    ENTER WHEN DONE",
            .scale = 1, .pal = PAL_HUD, .gap = 0 };
    else
        lines[n++] = (title_line_t){
            .text = "ENTER TO PLAY AGAIN    ESC FOR MENU",
            .scale = 1, .pal = PAL_HUD_DIM, .gap = 0 };
    if (!v->saved)
        lines[n++] = (title_line_t){ .text = "SCORES NOT SAVED", .scale = 1,
                                     .pal = PAL_TEAM2, .gap = 0 };

    int total = 0, widest = 0;
    for (;;) {
        total = 0;
        widest = 0;
        for (int i = 0; i < n; i++) {
            total += (7 + 3 + lines[i].gap * 3) * ts * lines[i].scale;
            int sc = ts * lines[i].scale;
            int kw[CTL_COLS], xo[CTL_COLS];
            int w = lines[i].ctl ? ctl_layout(kw, xo) * 6 * sc
                                 : fb_text_width(sc, lines[i].text);
            if (w > widest)
                widest = w;
        }
        if (ts <= 1)
            break;
        if (total + 40 * ts <= fb->h && widest + 16 * ts <= fb->w)
            break;
        ts--;
    }

    int y = (fb->h - total) / 2;
    if (y < 10 * ts)
        y = 10 * ts;
    int cx = fb->w / 2;

    fb_blend_rect(fb, 0, 0, fb->w, fb->h, 0xB0000000);
    if (c->style == RENDER_BREAKOUT)
        fb_blend_rect(fb, cx - widest / 2 - 8 * ts, y - 8 * ts,
                      widest + 16 * ts, total + 16 * ts,
                      sw_palette[PAL_HUD_BG]);

    for (int i = 0; i < n; i++) {
        int sc = ts * lines[i].scale;
        int x = cx - fb_text_width(sc, lines[i].text) / 2;
        fb_text(fb, x, y, sc, sw_palette[lines[i].pal], lines[i].text);

        /* The caret sits under the initial being edited, and blinks so it is
         * obvious the board is waiting for you rather than finished. */
        if (v->edit_cell >= 0 && v->highlight >= 0 &&
            i == first_row + v->highlight && ((v->t / 8) & 1)) {
            int bar = sc / 4 < 1 ? 1 : sc / 4;
            fb_rect(fb, x + (name_col + v->edit_cell) * 6 * sc, y + 8 * sc,
                    5 * sc, bar, sw_palette[PAL_TEAM1]);
        }
        y += (7 + 3 + lines[i].gap * 3) * sc;
    }
}
