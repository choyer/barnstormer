/*
 * sprites.h -- packed sprite artwork.
 *
 * Every symbol is stored one byte per pixel, values 0..3.  Colour 0 is
 * transparent; 1..3 are palette slots resolved at draw time by the renderer
 * (render.h), which lets the same bitmap serve either team.
 *
 * The bitmaps come from the original CGA artwork via tools/extract_sprites.py.
 * Nothing outside this module needs to know that; an enhanced-graphics pack
 * (see doc/ROADMAP.md) can supply a different sprite_set_t table with larger
 * frames and the renderer will scale accordingly.
 */
#ifndef SPRITES_H
#define SPRITES_H

#include <stddef.h>
#include <stdint.h>

#include "sprites_gen.h"

typedef struct {
    const char *name;
    int w, h;
    int frames;
    const uint8_t *px;      /* frames * w * h bytes                       */
} sprite_set_t;

extern const sprite_set_t sw_sprite_sets[SPRITE_SET_COUNT];

/* Pixel accessor.  Out-of-range frames wrap, which keeps callers that derive
 * a frame from an angle simple. */
static inline const uint8_t *sprite_frame(int set, int frame)
{
    const sprite_set_t *s = &sw_sprite_sets[set];
    if (frame < 0)
        frame = 0;
    return s->px + (size_t)(frame % s->frames) * s->w * s->h;
}

/* Fill the hollow interiors of every sprite so aircraft and buildings read as
 * solid shapes rather than outlines.  Builds a parallel table of frames; the
 * result is what breakout mode draws.  Idempotent. */
void sprites_build_solid(void);

/* Solid counterpart of sprite_frame(); falls back to the outline artwork if
 * sprites_build_solid() has not been called. */
const uint8_t *sprite_frame_solid(int set, int frame);

#endif /* SPRITES_H */
