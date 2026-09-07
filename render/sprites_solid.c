/*
 * sprites_solid.c -- turn outline artwork into filled shapes.
 *
 * The CGA originals are drawn as outlines with hollow middles, which reads
 * well against a black sky but disappears over a desktop wallpaper.  For
 * breakout mode we fill every enclosed region: flood the transparent pixels
 * inward from the frame's border, and whatever the flood cannot reach is
 * interior.  Each interior region takes the colour that dominates its own
 * boundary, so a fuselage outlined in one colour fills with that colour and a
 * building with a two-tone roof keeps the distinction.
 */
#include <stdlib.h>
#include <string.h>

#include "sprites.h"

static uint8_t *solid_px[SPRITE_SET_COUNT];

static void fill_frame(const uint8_t *src, uint8_t *dst, int w, int h)
{
    memcpy(dst, src, (size_t)w * h);

    /* 0 = untouched, 1 = reachable from outside, 2 = enclosed. */
    uint8_t *mark = calloc((size_t)w * h, 1);
    int *queue = malloc(sizeof(int) * (size_t)w * h);
    if (!mark || !queue) {
        free(mark);
        free(queue);
        return;
    }

    int qn = 0;
    for (int x = 0; x < w; x++) {
        if (!src[x])                        mark[x] = 1, queue[qn++] = x;
        int b = (h - 1) * w + x;
        if (!src[b] && !mark[b])            mark[b] = 1, queue[qn++] = b;
    }
    for (int y = 0; y < h; y++) {
        int l = y * w, r = y * w + w - 1;
        if (!src[l] && !mark[l])            mark[l] = 1, queue[qn++] = l;
        if (!src[r] && !mark[r])            mark[r] = 1, queue[qn++] = r;
    }

    for (int qi = 0; qi < qn; qi++) {
        int p = queue[qi];
        int x = p % w, y = p / w;
        const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = x + dxs[k], ny = y + dys[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            int np = ny * w + nx;
            if (src[np] || mark[np])
                continue;
            mark[np] = 1;
            queue[qn++] = np;
        }
    }

    /* Every remaining transparent pixel is enclosed.  Walk each region and
     * count the colours around its edge to decide what to fill it with. */
    for (int start = 0; start < w * h; start++) {
        if (src[start] || mark[start])
            continue;

        int votes[4] = { 0, 0, 0, 0 };
        int rn = 0;
        queue[rn++] = start;
        mark[start] = 2;

        for (int qi = 0; qi < rn; qi++) {
            int p = queue[qi];
            int x = p % w, y = p / w;
            const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = x + dxs[k], ny = y + dys[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                int np = ny * w + nx;
                if (src[np]) {
                    votes[src[np] & 3]++;
                } else if (!mark[np]) {
                    mark[np] = 2;
                    queue[rn++] = np;
                }
            }
        }

        int best = 1;
        for (int c = 2; c <= 3; c++)
            if (votes[c] > votes[best])
                best = c;
        for (int qi = 0; qi < rn; qi++)
            dst[queue[qi]] = (uint8_t)best;
    }

    free(mark);
    free(queue);
}

void sprites_build_solid(void)
{
    if (solid_px[0])
        return;

    for (int s = 0; s < SPRITE_SET_COUNT; s++) {
        const sprite_set_t *set = &sw_sprite_sets[s];
        size_t n = (size_t)set->frames * set->w * set->h;
        uint8_t *buf = malloc(n);
        if (!buf)
            return;
        for (int f = 0; f < set->frames; f++) {
            size_t off = (size_t)f * set->w * set->h;
            fill_frame(set->px + off, buf + off, set->w, set->h);
        }
        solid_px[s] = buf;
    }
}

const uint8_t *sprite_frame_solid(int set, int frame)
{
    if (set < 0 || set >= SPRITE_SET_COUNT || !solid_px[set])
        return sprite_frame(set, frame);
    const sprite_set_t *s = &sw_sprite_sets[set];
    if (frame < 0)
        frame = 0;
    return solid_px[set] + (size_t)(frame % s->frames) * s->w * s->h;
}
