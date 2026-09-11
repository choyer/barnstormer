/*
 * main.c -- argument handling and the outer loop.
 *
 * The simulation runs at the original's fixed 12.14 moves a second while the
 * display refreshes as fast as the compositor lets it, so the game feels
 * exactly as it did without being tied to the frame rate.
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "game.h"
#include "platform.h"
#include "render.h"
#include "score.h"

extern const int sw_title_menu_len;

#ifndef BARNSTORMER_VERSION
#define BARNSTORMER_VERSION "unknown"
#endif

typedef enum { UI_TITLE, UI_PLAY, UI_PAUSED, UI_OVER, UI_ENTRY } uistate_t;

/* SIGUSR1 asks for the keyboard back, for a keybind to fire at the game when
 * the overlay has been left deaf.  See platform_regrab(). */
static volatile sig_atomic_t want_regrab = 0;
static void on_sigusr1(int sig) { (void)sig; want_regrab = 1; }

/* Write the framebuffer as a binary PPM.  Used by --dump-frame, which exists
 * so a frame can be inspected without a compositor in the loop. */
static void dump_ppm(const framebuf_t *fb, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("barnstormer: --dump-frame");
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++) {
            uint32_t px = fb->px[(size_t)y * fb->stride + x];
            unsigned char rgb[3] = {
                (unsigned char)(px >> 16), (unsigned char)(px >> 8),
                (unsigned char)px,
            };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void usage(const char *argv0)
{
    printf(
"Sopwith Barnstormer -- a re-implementation of David L. Clark's 1984\n"
"game for Wayland.\n"
"\n"
"Usage: %s [options]\n"
"\n"
"      --version         print the version and exit\n"
"\n"
"Presentation:\n"
"  -w, --window          play in a window (the default)\n"
"  -b, --breakout        play as a transparent overlay on your desktop\n"
"      --size WxH        window size in classic mode (default 960x600)\n"
"      --no-grab         in breakout mode, share the keyboard with the\n"
"                        desktop instead of grabbing it (the overlay then\n"
"                        cannot be played, only watched)\n"
"      --dump-frame F    write one frame to F as a PPM and exit\n"
"      --dump-after N    wait N frames before dumping (default 0)\n"
"\n"
"Game:\n"
"  -n, --novice          forgiving: no stalls, no wildlife, unlimited ammo\n"
"  -s, --single          one pilot, no enemy aircraft\n"
"  -c, --computer        against three computer pilots (the default)\n"
"  -g, --game N          start at difficulty N (0-%d)\n"
"  -q, --quiet           start with the sound off\n"
"\n"
"Controls:\n"
"  ,  pull up      /  dive        .  flip over\n"
"  x  throttle up  z  throttle down\n"
"  space  guns     b  bomb        v  missile     c  flare\n"
"  h  fly home     s  sound       p  pause       F2  window/overlay\n"
"  r  restart the current game\n"
"  Esc  retire (parked at home) or abandon the run (in the air),\n"
"       then back to the menu, then quit\n",
        argv0, MAX_GAME);
}

int main(int argc, char **argv)
{
    playmode_t mode = PLAY_COMPUTER;
    render_style_t style = RENDER_CLASSIC;
    int width = SCR_WDTH * 3, height = SCR_HGHT * 3;
    int gamenum = 0;
    bool sound = true, grab = true, skip_title = false;
    const char *dump_path = NULL;
    long dump_after = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("barnstormer %s\n", BARNSTORMER_VERSION);
            return 0;
        } else if (!strcmp(a, "-w") || !strcmp(a, "--window")) {
            style = RENDER_CLASSIC;
        } else if (!strcmp(a, "-b") || !strcmp(a, "--breakout")) {
            style = RENDER_BREAKOUT;
        } else if (!strcmp(a, "--no-grab")) {
            grab = false;
        } else if (!strcmp(a, "-n") || !strcmp(a, "--novice")) {
            mode = PLAY_NOVICE; skip_title = true;
        } else if (!strcmp(a, "-s") || !strcmp(a, "--single")) {
            mode = PLAY_SINGLE; skip_title = true;
        } else if (!strcmp(a, "-c") || !strcmp(a, "--computer")) {
            mode = PLAY_COMPUTER; skip_title = true;
        } else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
            sound = false;
        } else if ((!strcmp(a, "-g") || !strcmp(a, "--game")) && i + 1 < argc) {
            gamenum = atoi(argv[++i]);
            if (gamenum < 0) gamenum = 0;
            if (gamenum > MAX_GAME) gamenum = MAX_GAME;
        } else if (!strcmp(a, "--dump-frame") && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (!strcmp(a, "--dump-after") && i + 1 < argc) {
            dump_after = atol(argv[++i]);
        } else if (!strcmp(a, "--size") && i + 1 < argc) {
            int w, h;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                width = w;
                height = h;
            }
        } else {
            fprintf(stderr, "barnstormer: unknown option '%s' "
                            "(try --help)\n", a);
            return 1;
        }
    }

    sprites_build_solid();

    platform_opts_t opts = {
        .style = style,
        .title = "Sopwith Barnstormer",
        .width = width,
        .height = height,
        .keyboard_exclusive = grab,
    };
    platform_t *plat = platform_open(&opts);
    if (!plat)
        return 1;

    struct sigaction sa = { .sa_handler = on_sigusr1 };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);

    audio_t *audio = audio_open();

    game_t game;
    memset(&game, 0, sizeof(game));
    game.sound_on = sound;
    game_start(&game, &level_classic, mode, gamenum);

    uistate_t ui = skip_title ? UI_PLAY : UI_TITLE;
    int menu_sel = (mode == PLAY_NOVICE) ? 0 : (mode == PLAY_SINGLE ? 1 : 2);
    unsigned title_t = 0;

    const double tick_dt = 1.0 / GAME_TICK_HZ;
    const double snd_dt = 1.0 / SOUND_ADJ_HZ;
    double last = now_seconds();
    double tick_acc = 0.0, snd_acc = 0.0;
    long frames = 0;
    bool running = true;
    bool deaf_pause = false;    /* paused because the keyboard was taken  */

    scores_t scores;
    scores_load(&scores);
    score_table_t board;        /* what the end screen shows              */
    char initials[SCORE_NAME_LEN + 1] = "AAA";
    int  rank = -1;             /* row the run earned, -1 for none        */
    int  cell = 0;              /* initial being edited                   */
    int  final_score = 0;
    bool saved = true;
    unsigned over_t = 0;

    while (running && platform_poll(plat)) {
        double t = now_seconds();
        double dt = t - last;
        last = t;
        if (dt > 0.25)
            dt = 0.25;              /* after a stall, do not fast-forward  */

        if (want_regrab) {
            want_regrab = 0;
            platform_regrab(plat);
        }

        /* Losing the keyboard mid-flight would otherwise fly the aircraft
         * into the ground while the menu is open, so hold the game there
         * and pick it up again when the keys come back. */
        if (platform_input_lost(plat)) {
            if (ui == UI_PLAY) {
                ui = UI_PAUSED;
                deaf_pause = true;
            }
        } else if (deaf_pause) {
            deaf_pause = false;
            if (ui == UI_PAUSED)
                ui = UI_PLAY;
        }

        int ev;
        while ((ev = platform_take_event(plat)) != SWKEY_NONE) {
            if (ui == UI_ENTRY) {
                if (ev == SWKEY_LEFT) {
                    cell = (cell + SCORE_NAME_LEN - 1) % SCORE_NAME_LEN;
                } else if (ev == SWKEY_RIGHT) {
                    cell = (cell + 1) % SCORE_NAME_LEN;
                } else if (ev == SWKEY_UP) {
                    initials[cell] = score_char_cycle(initials[cell], 1);
                } else if (ev == SWKEY_DOWN) {
                    initials[cell] = score_char_cycle(initials[cell], -1);
                } else if (ev == SWKEY_BACKSPACE) {
                    if (cell > 0) cell--;
                    initials[cell] = ' ';
                } else if (SWKEY_IS_CHAR(ev)) {
                    char ch = score_char_valid(SWKEY_CHAR(ev));
                    if (ch) {
                        initials[cell] = ch;
                        if (cell < SCORE_NAME_LEN - 1)
                            cell++;
                    }
                } else if (ev == SWKEY_ENTER || ev == SWKEY_QUIT) {
                    /* Esc commits too: nobody should lose a high score to
                     * the key they habitually press to get out of things. */
                    rank = scores_insert(&scores, mode, initials, final_score);
                    saved = scores_save(&scores);
                    int b = score_board_of(mode);
                    board = scores.board[b < 0 ? 0 : b];
                    ui = UI_OVER;
                }
                if (ui == UI_ENTRY && rank >= 0)
                    memcpy(board.e[rank].name, initials, SCORE_NAME_LEN);
                continue;
            }

            switch (ev) {
            /* Esc walks back out one step at a time -- run, then score,
             * then the title screen -- so quitting is always a deliberate
             * press from the menu rather than one key away mid-flight. */
            case SWKEY_QUIT:
                if (ui == UI_PLAY || ui == UI_PAUSED) {
                    game_abandon(&game);
                } else if (ui == UI_OVER) {
                    ui = UI_TITLE;
                } else {
                    running = false;
                }
                break;

            case SWKEY_STYLE: {
                render_style_t want = platform_style(plat) == RENDER_CLASSIC
                                          ? RENDER_BREAKOUT : RENDER_CLASSIC;
                if (!platform_set_style(plat, want))
                    fprintf(stderr, "barnstormer: overlay mode is unavailable "
                                    "on this compositor\n");
                break;
            }

            case SWKEY_SOUND:
                game.sound_on = !game.sound_on;
                break;

            case SWKEY_PAUSE:
                if (ui == UI_PLAY)        ui = UI_PAUSED;
                else if (ui == UI_PAUSED) ui = UI_PLAY;
                break;

            case SWKEY_UP:
                if (ui == UI_TITLE)
                    menu_sel = (menu_sel + sw_title_menu_len - 1) %
                               sw_title_menu_len;
                break;

            case SWKEY_DOWN:
                if (ui == UI_TITLE)
                    menu_sel = (menu_sel + 1) % sw_title_menu_len;
                break;

            case SWKEY_ENTER:
                if (ui == UI_TITLE) {
                    mode = (menu_sel == 0) ? PLAY_NOVICE
                         : (menu_sel == 1) ? PLAY_SINGLE : PLAY_COMPUTER;
                    game_start(&game, &level_classic, mode, gamenum);
                    ui = UI_PLAY;
                } else if (ui == UI_OVER) {
                    game_start(&game, &level_classic, mode, gamenum);
                    ui = UI_PLAY;
                }
                break;

            case SWKEY_RESTART:
                if (ui == UI_PLAY) {
                    game_start(&game, &level_classic, mode, gamenum);
                }
                break;

            default:
                break;
            }
        }

        if (ui == UI_PLAY) {
            tick_acc += dt;
            snd_acc += dt;
            int guard = 8;
            while (tick_acc >= tick_dt && guard--) {
                uint16_t keys[MAX_PLYR] = { 0 };
                keys[game.player] = platform_keys(plat);
                game_tick(&game, keys);
                tick_acc -= tick_dt;
                if (game.over)
                    break;
            }
            while (snd_acc >= snd_dt) {
                sw_sound_adj(&game);
                snd_acc -= snd_dt;
            }
        } else {
            tick_acc = snd_acc = 0.0;
        }

        /* A run can finish by crashing out, by retiring, or by bailing out;
         * all three arrive here, and only the first two are offered a place
         * on the board.
         *
         * Only a run we were still playing counts as newly finished.  Testing
         * the state we are leaving rather than the ones we are going to is
         * what lets Esc get off this screen: `game.over` stays true until the
         * next game starts, so a looser test drags the title screen straight
         * back here on the same frame. */
        if (game.over && (ui == UI_PLAY || ui == UI_PAUSED)) {
            int b = score_board_of(mode);
            if (b < 0) b = 0;
            final_score = game_player(&game)->score;
            rank = game_ranked(&game)
                       ? scores_rank(&scores, mode, final_score) : -1;
            board = scores.board[b];
            over_t = 0;

            if (rank >= 0) {
                memcpy(initials, scores.last_name, sizeof(initials));
                cell = 0;
                for (int i = SCORE_ROWS - 1; i > rank; i--)
                    board.e[i] = board.e[i - 1];
                memcpy(board.e[rank].name, initials, SCORE_NAME_LEN);
                board.e[rank].score = final_score;
                ui = UI_ENTRY;
            } else {
                ui = UI_OVER;
            }
        }

        audio_tone(audio, (ui == UI_PLAY && game.sound_on)
                              ? (unsigned)game.sound.tone : 0u);
        audio_pump(audio, dt);

        framebuf_t *fb = platform_begin_frame(plat);
        if (fb) {
            render_ctx_t ctx;
            render_layout(&ctx, platform_style(plat), fb->w, fb->h);

            switch (ui) {
            case UI_TITLE:
                render_title(fb, &ctx, title_t++, menu_sel);
                break;
            case UI_PLAY:
                render_frame(fb, &ctx, &game);
                break;
            case UI_PAUSED: {
                /* The overlay says why it stopped: a menu or the screenshot
                 * picker has the keyboard, and the game is waiting for it. */
                const char *msg = deaf_pause ? "KEYBOARD RELEASED" : "PAUSED";
                render_frame(fb, &ctx, &game);
                fb_blend_rect(fb, 0, 0, fb->w, fb->h, 0x80000000);
                fb_text(fb, (fb->w - fb_text_width(ctx.scale * 3, msg)) / 2,
                        fb->h / 2, ctx.scale * 3, sw_palette[PAL_HUD], msg);
                if (deaf_pause) {
                    const char *hint = "RESUMES WHEN THE KEYBOARD COMES BACK";
                    fb_text(fb,
                            (fb->w - fb_text_width(ctx.scale, hint)) / 2,
                            fb->h / 2 + ctx.scale * 3 * 7 + ctx.scale * 4,
                            ctx.scale, sw_palette[PAL_HUD], hint);
                }
                break;
            }
            case UI_OVER:
            case UI_ENTRY: {
                scoreboard_t v = {
                    .headline    = game.over_msg ? game.over_msg : "GAME OVER",
                    .board_name  = score_board_name(mode),
                    .final_score = final_score,
                    .ranked      = game_ranked(&game),
                    .table       = &board,
                    .highlight   = rank,
                    .edit_cell   = ui == UI_ENTRY ? cell : -1,
                    .saved       = saved,
                    .t           = over_t++,
                };
                render_frame(fb, &ctx, &game);
                render_scores(fb, &ctx, &v);
                break;
            }
            }
            if (dump_path && frames >= dump_after) {
                dump_ppm(fb, dump_path);
                running = false;
            }
            frames++;
            platform_end_frame(plat);
        }

        platform_wait(plat, 8);
    }

    audio_close(audio);
    platform_close(plat);
    return 0;
}
