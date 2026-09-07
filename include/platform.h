/*
 * platform.h -- Wayland front end.
 *
 * One backend covers both presentation modes.  Classic mode opens an
 * xdg-shell toplevel; breakout mode opens a wlr-layer-shell surface on the
 * overlay layer with an empty input region outside the game's solids, so the
 * game floats above the desktop.  Only libwayland-client and libxkbcommon are
 * required -- there is no toolkit, no GL and no SDL.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "render.h"

typedef struct platform platform_t;

typedef struct {
    render_style_t style;
    const char *title;
    int width, height;      /* requested size; ignored in breakout mode   */
    bool keyboard_exclusive; /* breakout: grab the keyboard while running  */
} platform_opts_t;

platform_t *platform_open(const platform_opts_t *opts);
void        platform_close(platform_t *p);

/* Pump the compositor and the keyboard.  Returns false when the user has
 * closed the window or pressed the quit key. */
bool platform_poll(platform_t *p);

/* Current control word (K_* bitmask) built from held keys. */
uint16_t platform_keys(const platform_t *p);

/* One-shot keys, cleared by reading.  Values are the SWKEY_* codes. */
enum {
    SWKEY_NONE = 0,
    SWKEY_QUIT,
    SWKEY_PAUSE,
    SWKEY_SOUND,
    SWKEY_STYLE,   /* toggle classic <-> breakout                         */
    SWKEY_ENTER,
    SWKEY_UP,
    SWKEY_DOWN,
    SWKEY_RESTART,
};
int platform_take_event(platform_t *p);

/* Acquire the next framebuffer.  Returns NULL if no buffer is free yet. */
framebuf_t *platform_begin_frame(platform_t *p);
void        platform_end_frame(platform_t *p);

bool platform_ready(const platform_t *p);

/* Switch presentation style at run time; tears down and rebuilds the
 * surface. Returns false if the compositor cannot provide the new style
 * (e.g. no layer-shell support). */
bool platform_set_style(platform_t *p, render_style_t style);
render_style_t platform_style(const platform_t *p);

/* Block until the compositor has traffic or `timeout_ms` elapses, so the
 * game loop idles instead of spinning. */
int platform_wait(platform_t *p, int timeout_ms);

/* Whether the compositor advertised wlr-layer-shell, i.e. whether breakout
 * mode is available at all. */
bool platform_has_overlay(const platform_t *p);

/* True when the overlay held the keyboard and something took it away -- the
 * Omarchy menu, the screenshot picker, anything else that asks for the
 * keyboard exclusively.  The backend reclaims it by itself as soon as the
 * compositor lets go, but the game is deaf until then and should pause. */
bool platform_input_lost(const platform_t *p);

/* Ask for the keyboard back now.  Called for the user on SIGUSR1, as a way
 * out if a compositor never returns the pointer either. */
void platform_regrab(platform_t *p);

#endif /* PLATFORM_H */
