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
}

/* Left-hand world column of the view, centred on wherever the game has put
 * its 320-wide window. */
static int view_left(const render_ctx_t *c, const game_t *g)
{
    int left = g->displx + SCR_WDTH / 2 - c->view_w / 2;
    int max = MAX_X - c->view_w;
    if (max < 0) max = 0;
    if (left < 0) left = 0;
    if (left > max) left = max;
    return left;
}

static inline int screen_x(const render_ctx_t *c, int left, int wx)
{
    return c->ox + (wx - left) * c->scale;
}

static inline int screen_y(const render_ctx_t *c, int wy)
{
    return c->oy + (c->view_h - 1 - wy) * c->scale;
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
                        int left)
{
    int s = c->scale;
    uint32_t body = sw_palette[PAL_GROUND];
    uint32_t edge = sw_palette[PAL_GROUND_EDGE];

    for (int col = 0; col < c->view_w; col++) {
        int wx = left + col;
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
                        const object_t *ob, int left)
{
    int s = c->scale;

    if (ob->sprite_set < 0) {
        /* Bullets and smoke are single pixels. */
        if (ob->x < left || ob->x >= left + c->view_w)
            return;
        if (ob->y < 0 || ob->y >= c->view_h)
            return;
        uint32_t col = ob->type == OBJ_SMOKE ? sw_palette[PAL_HUD_DIM]
                                             : sw_palette[PAL_NEUTRAL];
        fb_rect(fb, screen_x(c, left, ob->x), screen_y(c, ob->y), s, s, col);
        return;
    }

    const sprite_set_t *set = &sw_sprite_sets[ob->sprite_set];
    const uint8_t *px = c->solid ? sprite_frame_solid(ob->sprite_set,
                                                      ob->sprite_frame)
                                 : sprite_frame(ob->sprite_set,
                                                ob->sprite_frame);

    for (int row = 0; row < set->h; row++) {
        int wy = ob->y - row;
        if (wy < 0 || wy >= c->view_h)
            continue;
        int y = screen_y(c, wy);
        for (int col = 0; col < set->w; col++) {
            uint8_t v = px[row * set->w + col];
            if (!v)
                continue;
            int wx = ob->x + col;
            if (wx < left || wx >= left + c->view_w)
                continue;
            fb_rect(fb, screen_x(c, left, wx), y, s, s, obj_color(ob, v));
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

    int left = view_left(c, g);

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
    draw_hud(fb, c, g, left);

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
typedef struct {
    const char *text;
    int scale;          /* multiples of the base text size                */
    int pal;
    int gap;            /* extra space below, in base text units          */
} title_line_t;

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
        { "SOPWITH",                                4, PAL_TEAM1,   0 },
        { "BARNSTORMER",                            3, PAL_TITLE2,  2 },
        { "(C) COPYRIGHT 1984-2000 DAVID L. CLARK", 1, PAL_HUD_DIM, 0 },
        { "WAYLAND IMPLEMENTATION 2026 CARL HOYER", 1, PAL_HUD_DIM, 3 },
        { menu[0], 2, menu_sel == 0 ? PAL_HUD : PAL_HUD_DIM, 0 },
        { menu[1], 2, menu_sel == 1 ? PAL_HUD : PAL_HUD_DIM, 0 },
        { menu[2], 2, menu_sel == 2 ? PAL_HUD : PAL_HUD_DIM, 3 },
        { ",  PULL UP      /  DIVE        .  FLIP",  1, PAL_HUD_DIM, 0 },
        { "X  FASTER       Z  SLOWER   SPACE GUNS",  1, PAL_HUD_DIM, 0 },
        { "B  BOMB         V  MISSILE     C FLARE",  1, PAL_HUD_DIM, 0 },
        { "H  FLY HOME     S  SOUND       P PAUSE",  1, PAL_HUD_DIM, 0 },
        { "F2 WINDOW / OVERLAY       ESC  END RUN",  1, PAL_HUD_DIM, 0 },
        { "ESC FROM THIS SCREEN QUITS",              1, PAL_HUD_DIM, 3 },
        { "PRESS ENTER TO FLY",                      2, PAL_TEAM1,   0 },
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
            int w = fb_text_width(ts * lines[i].scale, lines[i].text);
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
        if (!blink || ((t / 24) & 1))
            fb_text(fb, cx - fb_text_width(sc, lines[i].text) / 2, y, sc,
                    sw_palette[lines[i].pal], lines[i].text);
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
