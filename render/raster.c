/*
 * raster.c -- framebuffer primitives.
 *
 * Everything is drawn by hand into a 32-bit ARGB buffer that goes straight to
 * a Wayland shared-memory surface.  Premultiplied alpha is what the
 * compositor expects, so the sky in breakout mode is simply 0x00000000 and
 * the desktop shows through untouched.
 */
#include <string.h>

#include "render.h"

extern const uint8_t sw_font5x7[95][5];

uint32_t sw_palette[PAL_COUNT] = {
    [PAL_SKY]         = 0xFF16243A,
    [PAL_SKY_TOP]     = 0xFF060A14,
    [PAL_TEAM1]       = 0xFF63E6F0,   /* the player: CGA cyan, softened    */
    [PAL_TEAM1_ACC]   = 0xFF2A93A8,
    [PAL_TEAM2]       = 0xFFF25FBE,   /* the enemy: CGA magenta            */
    [PAL_TEAM2_ACC]   = 0xFFA13279,
    [PAL_NEUTRAL]     = 0xFFF2F2F2,
    [PAL_GROUND]      = 0xFF6B4A2F,
    [PAL_GROUND_EDGE] = 0xFF4E8F3A,
    [PAL_WILDLIFE]    = 0xFFD8C08A,
    [PAL_HUD]         = 0xFFF2F2F2,
    [PAL_HUD_DIM]     = 0xFF7A8290,
    [PAL_HUD_BG]      = 0xC0101822,
    [PAL_TITLE2]      = 0xFFD8C08A,   /* the title's second word: doped linen */
};

void fb_clear(framebuf_t *fb, uint32_t argb)
{
    if (argb == 0) {
        memset(fb->px, 0, (size_t)fb->stride * fb->h * sizeof(uint32_t));
        return;
    }
    for (int y = 0; y < fb->h; y++) {
        uint32_t *row = fb->px + (size_t)y * fb->stride;
        for (int x = 0; x < fb->w; x++)
            row[x] = argb;
    }
}

void fb_rect(framebuf_t *fb, int x, int y, int w, int h, uint32_t argb)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    if (w <= 0 || h <= 0)
        return;

    for (int r = 0; r < h; r++) {
        uint32_t *row = fb->px + (size_t)(y + r) * fb->stride + x;
        for (int c = 0; c < w; c++)
            row[c] = argb;
    }
}

/* Source-over with premultiplied source. */
static inline uint32_t blend(uint32_t dst, uint32_t src)
{
    unsigned a = src >> 24;
    if (a == 255)
        return src;
    if (a == 0)
        return dst;
    unsigned ia = 255 - a;
    unsigned rb = ((((dst >> 16) & 0xFF) * ia) / 255) << 16 |
                  ((((dst >>  0) & 0xFF) * ia) / 255);
    unsigned g  = (((dst >> 8) & 0xFF) * ia) / 255;
    unsigned da = (((dst >> 24) & 0xFF) * ia) / 255;
    /* src is stored straight, so scale it here. */
    unsigned sr = (((src >> 16) & 0xFF) * a) / 255;
    unsigned sg = (((src >>  8) & 0xFF) * a) / 255;
    unsigned sb = (((src >>  0) & 0xFF) * a) / 255;
    return ((da + a) << 24) | (((rb >> 16) + sr) << 16) |
           ((g + sg) << 8) | (((rb & 0xFF) + sb));
}

void fb_blend_rect(framebuf_t *fb, int x, int y, int w, int h, uint32_t argb)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    if (w <= 0 || h <= 0)
        return;

    for (int r = 0; r < h; r++) {
        uint32_t *row = fb->px + (size_t)(y + r) * fb->stride + x;
        for (int c = 0; c < w; c++)
            row[c] = blend(row[c], argb);
    }
}

int fb_text_width(int scale, const char *s)
{
    return (int)strlen(s) * 6 * scale;
}

void fb_text(framebuf_t *fb, int x, int y, int scale, uint32_t argb,
             const char *s)
{
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) {
            x += 6 * scale;
            continue;
        }
        const uint8_t *glyph = sw_font5x7[ch - 32];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++)
                if (bits & (1u << row))
                    fb_rect(fb, x + col * scale, y + row * scale,
                            scale, scale, argb);
        }
        x += 6 * scale;
    }
}
