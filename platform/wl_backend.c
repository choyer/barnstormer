/*
 * wl_backend.c -- Wayland front end for both presentation styles.
 *
 * Classic mode is an ordinary xdg-shell toplevel.  Breakout mode is a
 * wlr-layer-shell surface pinned to all four edges of the overlay layer with
 * an empty input region, so the game is painted above every window while the
 * pointer still reaches whatever is underneath.  Because the sky is written
 * as fully transparent pixels, the compositor shows the user's desktop
 * through it.
 *
 * Only libwayland-client and libxkbcommon are linked; drawing is the software
 * rasteriser in render/, handed over as a shared-memory buffer.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "platform.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"

#define NBUFS 2
#define MAX_EVENTS 16

typedef struct {
    struct wl_buffer *wl;
    uint32_t *data;
    size_t offset, size;
    bool busy;
    framebuf_t fb;
} buffer_t;

struct platform {
    struct wl_display    *dpy;
    struct wl_registry   *reg;
    struct wl_compositor *comp;
    struct wl_shm        *shm;
    struct wl_seat       *seat;
    struct wl_keyboard   *kb;
    struct wl_pointer    *ptr;
    uint32_t              seat_caps;
    struct xdg_wm_base   *wm_base;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *scale_mgr;

    struct wl_surface    *surf;
    struct xdg_surface   *xsurf;
    struct xdg_toplevel  *xtop;
    struct zwlr_layer_surface_v1 *lsurf;
    struct wp_viewport   *viewport;
    struct wp_fractional_scale_v1 *fscale;
    struct wl_callback   *frame_cb;

    struct xkb_context *xkb;
    struct xkb_keymap  *keymap;
    struct xkb_state   *xkbstate;

    /* `width`/`height` are logical (surface) pixels, which is what the
     * compositor configures and what the viewport is sized to.  `buf_w`/
     * `buf_h` are the real device pixels we rasterise into: on a fractionally
     * scaled output they are larger, which is what keeps the game crisp. */
    int  width, height;
    int  buf_w, buf_h;
    int  scale120;              /* fractional scale in 120ths, 120 = 1.0   */
    bool configured, closed, frame_pending;

    struct wl_shm_pool *pool;
    void   *pool_data;
    size_t  pool_size;
    int     pool_fd;
    buffer_t bufs[NBUFS];
    buffer_t *current;

    /* A compositor may still be scanning out of a buffer when we replace the
     * pool on resize, so the old mapping is retired rather than unmapped and
     * released only once a new buffer has been through a commit. */
    void   *retired_data;
    size_t  retired_size;
    int     retired_fd;

    uint16_t keys;
    int events[MAX_EVENTS];
    int nevents;

    /* Overlay focus recovery.  `had_focus` distinguishes "never focused
     * yet" from "focused once and then lost it", which is the only case
     * worth reclaiming.  `last_regrab` is a cooldown against a re-grab
     * storm if a compositor answered one with another pointer enter. */
    bool   kb_focus, had_focus;
    double last_regrab;

    platform_opts_t opts;
    render_style_t  style;
};

static void push_event(platform_t *p, int ev)
{
    if (p->nevents < MAX_EVENTS)
        p->events[p->nevents++] = ev;
}

static double monotonic(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Whether the overlay is supposed to be holding the keyboard right now. */
static bool grabbing(const platform_t *p)
{
    return p->style == RENDER_BREAKOUT && p->opts.keyboard_exclusive &&
           p->lsurf != NULL;
}

/*
 * Take the keyboard back.
 *
 * A layer surface loses keyboard focus whenever another surface asks for it
 * exclusively -- the Omarchy menu and the screenshot picker both do -- and
 * the compositor has no reliable way to hand it back afterwards: it looks
 * for the surface under the pointer, and ours has an empty input region, so
 * it is never found.  Focus therefore stays where the dismissed surface left
 * it and the game goes deaf.
 *
 * Asking for exclusivity again is only honoured as a change, so the request
 * is dropped and re-made across two commits.  That is a transition the
 * compositor does act on, and focus comes back on the second commit.
 */
static void regrab(platform_t *p)
{
    /* Nothing to reclaim when the keys are already ours, and asking anyway
     * would cycle focus away and back, dropping whatever is held down. */
    if (!grabbing(p) || p->kb_focus)
        return;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        p->lsurf, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(p->surf);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        p->lsurf, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    wl_surface_commit(p->surf);
    wl_display_flush(p->dpy);
    p->last_regrab = monotonic();
}

/* ---- shared memory ------------------------------------------------------ */

static int anon_shm(size_t size)
{
    int fd = memfd_create("barnstormer-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
        return -1;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl)
{
    (void)wl;
    ((buffer_t *)data)->busy = false;
}
static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void release_retired(platform_t *p)
{
    if (p->retired_data) {
        munmap(p->retired_data, p->retired_size);
        p->retired_data = NULL;
    }
    if (p->retired_fd >= 0) {
        close(p->retired_fd);
        p->retired_fd = -1;
    }
}

static void pool_destroy(platform_t *p)
{
    for (int i = 0; i < NBUFS; i++) {
        if (p->bufs[i].wl)
            wl_buffer_destroy(p->bufs[i].wl);
        memset(&p->bufs[i], 0, sizeof(p->bufs[i]));
    }
    if (p->pool) {
        wl_shm_pool_destroy(p->pool);
        p->pool = NULL;
    }

    release_retired(p);
    p->retired_data = p->pool_data;
    p->retired_size = p->pool_size;
    p->retired_fd = p->pool_fd;
    p->pool_data = NULL;
    p->pool_fd = -1;
    p->current = NULL;
}

/* Device-pixel size for the current logical size and fractional scale. */
static void compute_buffer_size(platform_t *p)
{
    int s = p->scale120 > 0 ? p->scale120 : 120;
    p->buf_w = (p->width * s + 119) / 120;
    p->buf_h = (p->height * s + 119) / 120;
    if (p->buf_w < 1) p->buf_w = 1;
    if (p->buf_h < 1) p->buf_h = 1;
}

static bool pool_create(platform_t *p)
{
    pool_destroy(p);
    compute_buffer_size(p);

    size_t stride = (size_t)p->buf_w * 4;
    size_t one = stride * (size_t)p->buf_h;
    p->pool_size = one * NBUFS;

    p->pool_fd = anon_shm(p->pool_size);
    if (p->pool_fd < 0)
        return false;

    p->pool_data = mmap(NULL, p->pool_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, p->pool_fd, 0);
    if (p->pool_data == MAP_FAILED) {
        p->pool_data = NULL;
        return false;
    }

    p->pool = wl_shm_create_pool(p->shm, p->pool_fd, (int32_t)p->pool_size);
    for (int i = 0; i < NBUFS; i++) {
        buffer_t *b = &p->bufs[i];
        b->offset = one * (size_t)i;
        b->size = one;
        b->data = (uint32_t *)((char *)p->pool_data + b->offset);
        b->wl = wl_shm_pool_create_buffer(p->pool, (int32_t)b->offset,
                                          p->buf_w, p->buf_h,
                                          (int32_t)stride,
                                          WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(b->wl, &buffer_listener, b);
        b->busy = false;
        b->fb.px = b->data;
        b->fb.w = p->buf_w;
        b->fb.h = p->buf_h;
        b->fb.stride = p->buf_w;
    }

    /* Tell the compositor our oversized buffer still occupies the logical
     * area it asked for, so it presents one buffer pixel per screen pixel. */
    if (p->viewport)
        wp_viewport_set_destination(p->viewport, p->width, p->height);
    return true;
}

static bool pool_matches(const platform_t *p)
{
    return p->pool && p->bufs[0].fb.w == p->buf_w &&
           p->bufs[0].fb.h == p->buf_h;
}

/* ---- keyboard ----------------------------------------------------------- */

static uint16_t keysym_to_mask(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_comma: case XKB_KEY_less:
    case XKB_KEY_Up:                      return K_FLAPU;
    case XKB_KEY_slash: case XKB_KEY_question:
    case XKB_KEY_Down:                    return K_FLAPD;
    case XKB_KEY_period: case XKB_KEY_greater:
    case XKB_KEY_Return: case XKB_KEY_KP_Enter: return K_FLIP;
    case XKB_KEY_x: case XKB_KEY_X:
    case XKB_KEY_Right:                   return K_ACCEL;
    case XKB_KEY_z: case XKB_KEY_Z:
    case XKB_KEY_Left:                    return K_DEACC;
    case XKB_KEY_space:                   return K_SHOT;
    case XKB_KEY_b: case XKB_KEY_B:       return K_BOMB;
    case XKB_KEY_v: case XKB_KEY_V:       return K_MISSILE;
    case XKB_KEY_c: case XKB_KEY_C:       return K_STARBURST;
    case XKB_KEY_h: case XKB_KEY_H:       return K_HOME;
    default:                              return 0;
    }
}

static int keysym_to_event(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Escape:                  return SWKEY_QUIT;
    case XKB_KEY_p: case XKB_KEY_P:       return SWKEY_PAUSE;
    case XKB_KEY_s: case XKB_KEY_S:       return SWKEY_SOUND;
    case XKB_KEY_F2:                      return SWKEY_STYLE;
    case XKB_KEY_Return: case XKB_KEY_KP_Enter: return SWKEY_ENTER;
    case XKB_KEY_Up:                      return SWKEY_UP;
    case XKB_KEY_Down:                    return SWKEY_DOWN;
    case XKB_KEY_r: case XKB_KEY_R:       return SWKEY_RESTART;
    default:                              return SWKEY_NONE;
    }
}

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
                      int fd, uint32_t size)
{
    platform_t *p = data;
    (void)kb;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }
    struct xkb_keymap *km = xkb_keymap_new_from_string(
        p->xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (!km)
        return;
    if (p->xkbstate) xkb_state_unref(p->xkbstate);
    if (p->keymap)   xkb_keymap_unref(p->keymap);
    p->keymap = km;
    p->xkbstate = xkb_state_new(km);
}

static void kb_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *s, struct wl_array *keys)
{
    (void)kb; (void)serial; (void)s; (void)keys;
    platform_t *p = data;
    p->keys = 0;
    p->kb_focus = true;
    p->had_focus = true;
}

static void kb_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *s)
{
    (void)kb; (void)serial; (void)s;
    /* Dropping focus must not leave the throttle jammed open. */
    platform_t *p = data;
    p->keys = 0;
    p->kb_focus = false;
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state)
{
    platform_t *p = data;
    (void)kb; (void)serial; (void)time;
    if (!p->xkbstate)
        return;

    xkb_keycode_t code = key + 8;
    const xkb_keysym_t *syms;
    int n = xkb_state_key_get_syms(p->xkbstate, code, &syms);

    for (int i = 0; i < n; i++) {
        uint16_t mask = keysym_to_mask(syms[i]);
        if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            p->keys |= mask;
            int ev = keysym_to_event(syms[i]);
            if (ev != SWKEY_NONE)
                push_event(p, ev);
        } else {
            p->keys &= (uint16_t)~mask;
        }
    }
}

static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t dep, uint32_t lat, uint32_t lock,
                         uint32_t group)
{
    platform_t *p = data;
    (void)kb; (void)serial;
    if (p->xkbstate)
        xkb_state_update_mask(p->xkbstate, dep, lat, lock, 0, 0, group);
}

static void kb_repeat(void *data, struct wl_keyboard *kb, int32_t rate,
                      int32_t delay)
{
    (void)data; (void)kb; (void)rate; (void)delay;
    /* Held keys are tracked directly, so compositor repeat is ignored. */
}

static const struct wl_keyboard_listener kb_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat,
};

/* ---- pointer ------------------------------------------------------------
 *
 * The overlay has an empty input region, so in the ordinary course of events
 * the pointer never visits it.  The one exception is the compositor forcing
 * pointer focus onto an exclusive layer surface when whatever was covering it
 * goes away -- which is exactly the moment the keyboard should come back to
 * us, and the moment it does not.  So an enter here is the signal to re-grab,
 * and because it cannot arrive while the menu or the screenshot picker still
 * holds the pointer, re-grabbing on it cannot fight them for focus.
 *
 * The pointer is only bound while the overlay is grabbing; in window mode no
 * wl_pointer exists and the cursor stays the compositor's business.
 */
static void pt_enter(void *data, struct wl_pointer *pt, uint32_t serial,
                     struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    platform_t *p = data;
    (void)pt; (void)serial; (void)s; (void)x; (void)y;
    if (grabbing(p) && p->had_focus && !p->kb_focus &&
        monotonic() - p->last_regrab > 0.5)
        regrab(p);
}
static void pt_leave(void *d, struct wl_pointer *pt, uint32_t serial,
                     struct wl_surface *s)
{ (void)d; (void)pt; (void)serial; (void)s; }
static void pt_motion(void *d, struct wl_pointer *pt, uint32_t t,
                      wl_fixed_t x, wl_fixed_t y)
{ (void)d; (void)pt; (void)t; (void)x; (void)y; }
static void pt_button(void *d, struct wl_pointer *pt, uint32_t serial,
                      uint32_t t, uint32_t b, uint32_t st)
{ (void)d; (void)pt; (void)serial; (void)t; (void)b; (void)st; }
static void pt_axis(void *d, struct wl_pointer *pt, uint32_t t, uint32_t a,
                    wl_fixed_t v)
{ (void)d; (void)pt; (void)t; (void)a; (void)v; }
static void pt_frame(void *d, struct wl_pointer *pt) { (void)d; (void)pt; }
static void pt_axis_source(void *d, struct wl_pointer *pt, uint32_t src)
{ (void)d; (void)pt; (void)src; }
static void pt_axis_stop(void *d, struct wl_pointer *pt, uint32_t t,
                         uint32_t a)
{ (void)d; (void)pt; (void)t; (void)a; }
static void pt_axis_discrete(void *d, struct wl_pointer *pt, uint32_t a,
                             int32_t steps)
{ (void)d; (void)pt; (void)a; (void)steps; }
static const struct wl_pointer_listener pt_listener = {
    .enter = pt_enter, .leave = pt_leave, .motion = pt_motion,
    .button = pt_button, .axis = pt_axis, .frame = pt_frame,
    .axis_source = pt_axis_source, .axis_stop = pt_axis_stop,
    .axis_discrete = pt_axis_discrete,
};

/* Bind the pointer only for as long as the overlay needs it to notice that
 * the keyboard has been handed back. */
static void pointer_sync(platform_t *p)
{
    bool want = grabbing(p) && (p->seat_caps & WL_SEAT_CAPABILITY_POINTER);
    if (want && !p->ptr) {
        p->ptr = wl_seat_get_pointer(p->seat);
        wl_pointer_add_listener(p->ptr, &pt_listener, p);
    } else if (!want && p->ptr) {
        wl_pointer_release(p->ptr);
        p->ptr = NULL;
    }
}

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    platform_t *p = data;
    p->seat_caps = caps;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !p->kb) {
        p->kb = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(p->kb, &kb_listener, p);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && p->kb) {
        wl_keyboard_destroy(p->kb);
        p->kb = NULL;
    }
    pointer_sync(p);
}
static void seat_name(void *d, struct wl_seat *s, const char *n)
{ (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

/* ---- shells ------------------------------------------------------------- */

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_listener = { wm_ping };

static void xsurf_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
    platform_t *p = data;
    xdg_surface_ack_configure(s, serial);
    compute_buffer_size(p);
    if (!pool_matches(p))
        pool_create(p);
    p->configured = true;
}
static const struct xdg_surface_listener xsurf_listener = { xsurf_configure };

static void xtop_configure(void *data, struct xdg_toplevel *t, int32_t w,
                           int32_t h, struct wl_array *states)
{
    platform_t *p = data;
    (void)t; (void)states;
    if (w > 0 && h > 0) {
        p->width = w;
        p->height = h;
    }
}
static void xtop_close(void *data, struct xdg_toplevel *t)
{
    (void)t;
    ((platform_t *)data)->closed = true;
}
static void xtop_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h)
{ (void)d; (void)t; (void)w; (void)h; }
static void xtop_caps(void *d, struct xdg_toplevel *t, struct wl_array *c)
{ (void)d; (void)t; (void)c; }
static const struct xdg_toplevel_listener xtop_listener = {
    xtop_configure, xtop_close, xtop_bounds, xtop_caps,
};

static void lsurf_configure(void *data, struct zwlr_layer_surface_v1 *s,
                            uint32_t serial, uint32_t w, uint32_t h)
{
    platform_t *p = data;
    zwlr_layer_surface_v1_ack_configure(s, serial);
    if (w > 0 && h > 0) {
        p->width = (int)w;
        p->height = (int)h;
    }
    compute_buffer_size(p);
    if (!pool_matches(p))
        pool_create(p);
    p->configured = true;
}
static void lsurf_closed(void *data, struct zwlr_layer_surface_v1 *s)
{
    (void)s;
    ((platform_t *)data)->closed = true;
}
static const struct zwlr_layer_surface_v1_listener lsurf_listener = {
    lsurf_configure, lsurf_closed,
};

static void fscale_preferred(void *data, struct wp_fractional_scale_v1 *fs,
                             uint32_t scale)
{
    platform_t *p = data;
    (void)fs;
    if (scale == 0 || (int)scale == p->scale120)
        return;
    p->scale120 = (int)scale;
    compute_buffer_size(p);
    if (!pool_matches(p))
        pool_create(p);
}
static const struct wp_fractional_scale_v1_listener fscale_listener = {
    fscale_preferred,
};

/* ---- registry ----------------------------------------------------------- */

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t version)
{
    platform_t *p = data;

    if (!strcmp(iface, wl_compositor_interface.name)) {
        p->comp = wl_registry_bind(reg, name, &wl_compositor_interface,
                                   version < 4 ? version : 4);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        p->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        p->seat = wl_registry_bind(reg, name, &wl_seat_interface,
                                   version < 5 ? version : 5);
        wl_seat_add_listener(p->seat, &seat_listener, p);
    } else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        p->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface,
                                      version < 2 ? version : 2);
        xdg_wm_base_add_listener(p->wm_base, &wm_listener, p);
    } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
        p->layer_shell = wl_registry_bind(reg, name,
                                          &zwlr_layer_shell_v1_interface,
                                          version < 4 ? version : 4);
    } else if (!strcmp(iface, wp_viewporter_interface.name)) {
        p->viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
    } else if (!strcmp(iface,
                       wp_fractional_scale_manager_v1_interface.name)) {
        p->scale_mgr = wl_registry_bind(
            reg, name, &wp_fractional_scale_manager_v1_interface, 1);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n)
{ (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = {
    reg_global, reg_remove,
};

/* ---- surface lifecycle -------------------------------------------------- */

static void surface_destroy(platform_t *p)
{
    if (p->frame_cb) { wl_callback_destroy(p->frame_cb); p->frame_cb = NULL; }
    pool_destroy(p);
    if (p->fscale) { wp_fractional_scale_v1_destroy(p->fscale); p->fscale = NULL; }
    if (p->viewport) { wp_viewport_destroy(p->viewport); p->viewport = NULL; }
    if (p->lsurf) { zwlr_layer_surface_v1_destroy(p->lsurf); p->lsurf = NULL; }
    if (p->xtop)  { xdg_toplevel_destroy(p->xtop);  p->xtop = NULL; }
    if (p->xsurf) { xdg_surface_destroy(p->xsurf);  p->xsurf = NULL; }
    if (p->surf)  { wl_surface_destroy(p->surf);    p->surf = NULL; }
    p->configured = false;
    p->frame_pending = false;
    /* Focus belongs to the surface that just went away. */
    p->kb_focus = false;
    p->had_focus = false;
}

static bool surface_create(platform_t *p)
{
    p->surf = wl_compositor_create_surface(p->comp);
    if (!p->surf)
        return false;

    if (p->viewporter)
        p->viewport = wp_viewporter_get_viewport(p->viewporter, p->surf);
    if (p->scale_mgr) {
        p->fscale = wp_fractional_scale_manager_v1_get_fractional_scale(
            p->scale_mgr, p->surf);
        wp_fractional_scale_v1_add_listener(p->fscale, &fscale_listener, p);
    }

    if (p->style == RENDER_BREAKOUT) {
        p->lsurf = zwlr_layer_shell_v1_get_layer_surface(
            p->layer_shell, p->surf, NULL,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "barnstormer");
        if (!p->lsurf)
            return false;
        zwlr_layer_surface_v1_add_listener(p->lsurf, &lsurf_listener, p);
        zwlr_layer_surface_v1_set_anchor(p->lsurf,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_size(p->lsurf, 0, 0);
        /* -1 keeps us out of the way of panels' exclusive zones and lets us
         * cover the entire output. */
        zwlr_layer_surface_v1_set_exclusive_zone(p->lsurf, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(p->lsurf,
            p->opts.keyboard_exclusive
                ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
                : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);

        /* An empty input region means the pointer goes straight through to
         * whatever the game is floating above. */
        struct wl_region *empty = wl_compositor_create_region(p->comp);
        wl_surface_set_input_region(p->surf, empty);
        wl_region_destroy(empty);
    } else {
        p->xsurf = xdg_wm_base_get_xdg_surface(p->wm_base, p->surf);
        if (!p->xsurf)
            return false;
        xdg_surface_add_listener(p->xsurf, &xsurf_listener, p);
        p->xtop = xdg_surface_get_toplevel(p->xsurf);
        xdg_toplevel_add_listener(p->xtop, &xtop_listener, p);
        xdg_toplevel_set_title(p->xtop, p->opts.title ? p->opts.title
                                                      : "Sopwith Barnstormer");
        xdg_toplevel_set_app_id(p->xtop, "barnstormer");
    }

    wl_surface_commit(p->surf);
    wl_display_roundtrip(p->dpy);

    pointer_sync(p);

    /* An xdg toplevel gets no size in its first configure, so pick ours. */
    if (p->style == RENDER_CLASSIC && !p->pool) {
        if (!pool_create(p))
            return false;
    }
    return true;
}

/* ---- public API --------------------------------------------------------- */

platform_t *platform_open(const platform_opts_t *opts)
{
    platform_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->pool_fd = -1;
    p->retired_fd = -1;
    p->scale120 = 120;
    p->opts = *opts;
    p->style = opts->style;
    p->width = opts->width > 0 ? opts->width : SCR_WDTH * 3;
    p->height = opts->height > 0 ? opts->height : SCR_HGHT * 3;

    p->dpy = wl_display_connect(NULL);
    if (!p->dpy) {
        fprintf(stderr, "barnstormer: cannot connect to a Wayland compositor "
                        "(is WAYLAND_DISPLAY set?)\n");
        free(p);
        return NULL;
    }

    p->reg = wl_display_get_registry(p->dpy);
    wl_registry_add_listener(p->reg, &reg_listener, p);
    wl_display_roundtrip(p->dpy);

    if (!p->comp || !p->shm || !p->wm_base) {
        fprintf(stderr, "barnstormer: compositor is missing wl_compositor, "
                        "wl_shm or xdg_wm_base\n");
        platform_close(p);
        return NULL;
    }
    wl_display_roundtrip(p->dpy);   /* settle seat capabilities            */

    p->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!p->xkb) {
        platform_close(p);
        return NULL;
    }

    if (p->style == RENDER_BREAKOUT && !p->layer_shell) {
        fprintf(stderr, "barnstormer: this compositor does not support "
                        "wlr-layer-shell; falling back to a window\n");
        p->style = RENDER_CLASSIC;
    }

    if (!surface_create(p)) {
        platform_close(p);
        return NULL;
    }
    return p;
}

void platform_close(platform_t *p)
{
    if (!p)
        return;
    surface_destroy(p);
    release_retired(p);
    if (p->xkbstate)   xkb_state_unref(p->xkbstate);
    if (p->keymap)     xkb_keymap_unref(p->keymap);
    if (p->xkb)        xkb_context_unref(p->xkb);
    if (p->ptr)        wl_pointer_release(p->ptr);
    if (p->kb)         wl_keyboard_destroy(p->kb);
    if (p->seat)       wl_seat_destroy(p->seat);
    if (p->scale_mgr)  wp_fractional_scale_manager_v1_destroy(p->scale_mgr);
    if (p->viewporter) wp_viewporter_destroy(p->viewporter);
    if (p->layer_shell) zwlr_layer_shell_v1_destroy(p->layer_shell);
    if (p->wm_base)    xdg_wm_base_destroy(p->wm_base);
    if (p->shm)        wl_shm_destroy(p->shm);
    if (p->comp)       wl_compositor_destroy(p->comp);
    if (p->reg)        wl_registry_destroy(p->reg);
    if (p->dpy)        wl_display_disconnect(p->dpy);
    free(p);
}

bool platform_poll(platform_t *p)
{
    if (wl_display_dispatch_pending(p->dpy) < 0)
        return false;
    if (wl_display_flush(p->dpy) < 0 && errno != EAGAIN)
        return false;
    return !p->closed;
}

uint16_t platform_keys(const platform_t *p) { return p->keys; }

int platform_take_event(platform_t *p)
{
    if (p->nevents == 0)
        return SWKEY_NONE;
    int ev = p->events[0];
    memmove(p->events, p->events + 1, sizeof(int) * (size_t)(--p->nevents));
    return ev;
}

bool platform_ready(const platform_t *p)
{
    return p->configured && p->pool != NULL;
}

bool platform_has_overlay(const platform_t *p) { return p->layer_shell != NULL; }

bool platform_input_lost(const platform_t *p)
{
    return grabbing(p) && p->had_focus && !p->kb_focus;
}

void platform_regrab(platform_t *p) { regrab(p); }
render_style_t platform_style(const platform_t *p) { return p->style; }

static void frame_done(void *data, struct wl_callback *cb, uint32_t t)
{
    platform_t *p = data;
    (void)t;
    wl_callback_destroy(cb);
    p->frame_cb = NULL;
    p->frame_pending = false;
}
static const struct wl_callback_listener frame_listener = { frame_done };

framebuf_t *platform_begin_frame(platform_t *p)
{
    if (!platform_ready(p) || p->frame_pending)
        return NULL;

    for (int i = 0; i < NBUFS; i++) {
        if (!p->bufs[i].busy) {
            p->current = &p->bufs[i];
            return &p->current->fb;
        }
    }
    return NULL;
}

void platform_end_frame(platform_t *p)
{
    buffer_t *b = p->current;
    if (!b)
        return;
    p->current = NULL;
    b->busy = true;

    /* The previous mapping cannot still be on screen now that a buffer from
     * the current one is being committed. */
    release_retired(p);

    p->frame_cb = wl_surface_frame(p->surf);
    wl_callback_add_listener(p->frame_cb, &frame_listener, p);
    p->frame_pending = true;

    wl_surface_attach(p->surf, b->wl, 0, 0);
    wl_surface_damage_buffer(p->surf, 0, 0, p->buf_w, p->buf_h);
    wl_surface_commit(p->surf);
    wl_display_flush(p->dpy);
}

bool platform_set_style(platform_t *p, render_style_t style)
{
    if (style == p->style)
        return true;
    if (style == RENDER_BREAKOUT && !p->layer_shell)
        return false;

    surface_destroy(p);
    p->style = style;
    if (style == RENDER_CLASSIC) {
        p->width = p->opts.width > 0 ? p->opts.width : SCR_WDTH * 3;
        p->height = p->opts.height > 0 ? p->opts.height : SCR_HGHT * 3;
    }
    if (!surface_create(p)) {
        p->closed = true;
        return false;
    }
    return true;
}

/* Block until the compositor has something for us, so the game loop does not
 * spin.  `timeout_ms` bounds the wait so the simulation still ticks. */
int platform_wait(platform_t *p, int timeout_ms)
{
    while (wl_display_prepare_read(p->dpy) != 0)
        wl_display_dispatch_pending(p->dpy);
    wl_display_flush(p->dpy);

    struct pollfd pfd = {
        .fd = wl_display_get_fd(p->dpy),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, timeout_ms);
    if (n > 0)
        wl_display_read_events(p->dpy);
    else
        wl_display_cancel_read(p->dpy);

    wl_display_dispatch_pending(p->dpy);
    return n;
}
