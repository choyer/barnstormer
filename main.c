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
#include "editor.h"
#include "game.h"
#include "platform.h"
#include "render.h"
#include "score.h"

extern const int sw_title_menu_len;

#ifndef BARNSTORMER_VERSION
#define BARNSTORMER_VERSION "unknown"
#endif

typedef enum { UI_TITLE, UI_PLAY, UI_PAUSED, UI_OVER, UI_ENTRY,
               UI_EDIT, UI_LEVELS, UI_ATTRACT } uistate_t;

/* Left alone on the title screen, the game starts showing off: after
 * TITLE_IDLE_SECS with nothing typed it cycles the three high score boards,
 * ATTRACT_BOARD_SECS each, until a key brings the menu back. */
#define TITLE_IDLE_SECS     65.0
#define ATTRACT_BOARD_SECS  15.0

/* The boards in the order the attract cycle shows them, which is the order
 * they are listed on the title screen. */
static const playmode_t attract_modes[SCORE_BOARDS] = {
    PLAY_NOVICE, PLAY_SINGLE, PLAY_COMPUTER,
};

/* A digest of the menu keys pressed just after launch, and how long there is
 * to press them.  The title screen recognises one pattern this way: it is
 * compared as a digest rather than matched key by key, which keeps the
 * comparison to one branch on the hot path and the table out of the binary.
 *
 * KEY_DIGEST_SEED/STEP are the usual 32-bit FNV-1a pair. */
#define KEY_DIGEST_SEED   2166136261u
#define KEY_DIGEST_STEP     16777619u
#define KEY_DIGEST_MATCH  0xd41e839du
#define KEY_WINDOW_OPEN     3.0     /* seconds from launch to the first key */
#define KEY_WINDOW_SPAN     3.5     /* seconds from that key to the last    */
#define KEY_RESERVE          30     /* the allowance a match asks for       */

/* Letters are folded to upper case so the digest does not depend on whether
 * shift happened to be down. */
static uint32_t key_digest(uint32_t d, int ev)
{
    if (SWKEY_IS_CHAR(ev)) {
        char ch = SWKEY_CHAR(ev);
        if (ch >= 'a' && ch <= 'z')
            ev = SWKEY_CHAR_BASE + (ch - 'a' + 'A');
    }
    return (d ^ (uint32_t)ev) * KEY_DIGEST_STEP;
}

/* A short square-wave flourish over the first half second of a run that
 * begins on the other allowance: a rising arpeggio, the speaker's way of
 * saying the run is not an ordinary one.  Divisors, like every other sound
 * in the game -- the speaker only ever took those. */
#define CHIME_DIV(hz)  ((unsigned)(PIT_CLOCK_HZ / (hz) + 0.5))

static const struct { unsigned div; double secs; } chime[] = {
    { CHIME_DIV(523.25), 0.06 },    /* C5 */
    { CHIME_DIV(659.26), 0.06 },    /* E5 */
    { CHIME_DIV(783.99), 0.06 },    /* G5 */
    { CHIME_DIV(1046.5), 0.06 },    /* C6 */
    { CHIME_DIV(1568.0), 0.22 },    /* G6, held                           */
};
#define CHIME_NOTES  ((int)(sizeof(chime) / sizeof(chime[0])))

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
"      --no-smooth       draw only the 12.14 positions a second the\n"
"                        simulation produces, as the original did\n"
"      --dump-frame F    write one frame to F as a PPM and exit\n"
"      --dump-after N    wait N frames before dumping (default 0)\n"
"\n"
"Game:\n"
"  -n, --novice          forgiving: no stalls, no wildlife, unlimited ammo\n"
"  -s, --single          one pilot, no enemy aircraft\n"
"  -c, --computer        against three computer pilots (the default)\n"
"  -g, --game N          start at difficulty N (0-%d)\n"
"  -q, --quiet           start with the sound off\n"
"  -l, --level FILE      fly a level file instead of the classic map\n"
"                        (doc/LEVEL_FORMAT.md); runs on it are not ranked\n"
"  -e, --edit FILE       open FILE in the level editor, creating it if it\n"
"                        is not there yet\n"
"      --check FILE      say whether FILE is a level and exit, without\n"
"                        opening a window\n"
"\n"
"Controls:\n"
"  ,  pull up      /  dive        .  flip over\n"
"  x  throttle up  z  throttle down\n"
"  space  guns     b  bomb        v  missile     c  flare\n"
"  h  fly home     s  sound       p  pause       F2  window/overlay\n"
"  d  throttle and airspeed dials (remembered between runs)\n"
"  r  restart the current game\n"
"  Esc  retire (parked at home) or abandon the run (in the air),\n"
"       then back to the menu, then quit\n"
"\n"
"Editor (--edit):\n"
"  arrows  move the cursor / raise and lower the ground\n"
"  space  place      Backspace  remove      t  tool      k  kind\n"
"  f  flatten        s  smooth              [  ]  brush width\n"
"  g  pick up what is under the cursor, g again to put it down\n"
"  n  name           a  author             w  write the file\n"
"  Tab  fly it, and again to come back     Esc  leave\n",
        argv0, MAX_GAME);
}

int main(int argc, char **argv)
{
    playmode_t mode = PLAY_COMPUTER;
    render_style_t style = RENDER_CLASSIC;
    int width = SCR_WDTH * 3, height = SCR_HGHT * 3;
    int gamenum = 0;
    bool sound = true, grab = true, skip_title = false;
    bool smooth = true;
    const char *dump_path = NULL;
    const char *level_path = NULL;
    const char *edit_path = NULL;
    const char *check_path = NULL;
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
        } else if (!strcmp(a, "--no-smooth")) {
            smooth = false;
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
        } else if ((!strcmp(a, "-l") || !strcmp(a, "--level")) &&
                   i + 1 < argc) {
            level_path = argv[++i];
        } else if ((!strcmp(a, "-e") || !strcmp(a, "--edit")) &&
                   i + 1 < argc) {
            edit_path = argv[++i];
        } else if (!strcmp(a, "--check") && i + 1 < argc) {
            check_path = argv[++i];
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

    /* Before the window: a level that will not load should say so on the
     * terminal the player typed into, not flash a window and vanish. */
    const level_t *level = &level_classic;
    level_t *custom = NULL;
    if (level_path) {
        if (level_load(level_path, &custom) < 0) {
            fprintf(stderr, "barnstormer: %s: %s\n",
                    level_path, level_error());
            return 1;
        }
        level = custom;
        printf("flying \"%s\"%s%s [%08x]\n", level->name,
               level->author ? " by " : "",
               level->author ? level->author : "",
               level_hash(level));
    }

    /* Answered before anything opens a window, so that it works over ssh, in
     * a container, and in whatever a level generator is being driven from. */
    if (check_path) {
        level_t *lv = NULL;
        if (level_load(check_path, &lv) < 0) {
            fprintf(stderr, "%s: %s\n", check_path, level_error());
            return 1;
        }
        int lo = LEVEL_GROUND_MAX, hi = 0;
        for (int i = 0; i < lv->width; i++) {
            if (lv->ground[i] < lo) lo = lv->ground[i];
            if (lv->ground[i] > hi) hi = lv->ground[i];
        }
        printf("%s: ok [%08x]  \"%s\"%s%s\n", check_path, level_hash(lv),
               lv->name, lv->author ? " by " : "",
               lv->author ? lv->author : "");
        printf("  terrain %d..%d   %d runway%s  %d building%s  %d ox%s\n",
               lo, hi, lv->n_runways, lv->n_runways == 1 ? "" : "s",
               lv->n_targets, lv->n_targets == 1 ? "" : "s",
               lv->n_oxen, lv->n_oxen == 1 ? "" : "en");
        level_free(lv);
        return 0;
    }

    /* The editor holds its own working copy; --edit wins if both are given,
     * since the file being edited is the one you asked to see. */
    editor_t editor;
    memset(&editor, 0, sizeof(editor));
    if (edit_path) {
        if (editor_open(&editor, edit_path) < 0) {
            fprintf(stderr, "barnstormer: %s: %s\n", edit_path,
                    level_error());
            return 1;
        }
        level = editor_level(&editor);
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
    game_start(&game, level, mode, gamenum);

    uistate_t ui = edit_path ? UI_EDIT : (skip_title ? UI_PLAY : UI_TITLE);
    bool flying = false;          /* test flight launched from the editor */
    bool leave_armed = false;     /* Esc pressed once with work unsaved   */
    unsigned edit_t = 0;          /* paces the held-key editing keys      */

    /* The level directory, read when the picker is opened rather than at
     * startup: levels arrive while the game is running, from the editor a
     * Tab away or from a file manager. */
    level_info_t levels[LEVEL_LIST_MAX];
    int n_levels = 0, levels_skipped = 0, level_sel = 0;
    int menu_sel = (mode == PLAY_NOVICE) ? 0 : (mode == PLAY_SINGLE ? 1 : 2);
    unsigned title_t = 0;

    /* Seconds since the last key on the title screen, and where the attract
     * cycle has got to once that runs out. */
    double idle_t = 0.0;
    double attract_acc = 0.0;
    int attract_board = 0;

    /* The launch key window: open until it expires, and never reopened, so
     * what it recognises is only reachable on a freshly started game. */
    uint32_t key_dig = KEY_DIGEST_SEED;
    double key_first = 0.0;
    bool key_open = !skip_title && !edit_path;
    bool key_armed = false;      /* the pattern was completed in time     */
    bool run_marked = false;     /* this run flies on the other allowance */
    int chime_at = CHIME_NOTES;  /* CHIME_NOTES when nothing is playing   */
    double chime_t = 0.0;

    const double tick_dt = 1.0 / GAME_TICK_HZ;
    const double snd_dt = 1.0 / SOUND_ADJ_HZ;
    double last = now_seconds();
    double shown = 0.0;         /* when the first frame reached the screen */
    double tick_acc = 0.0, snd_acc = 0.0;
    long frames = 0;
    bool running = true;
    bool deaf_pause = false;    /* paused because the keyboard was taken  */

    scores_t scores;
    scores_load(&scores);
    bool dials = scores.dials;  /* remembered between runs                */
    score_table_t board;        /* what the end screen shows              */
    char initials[SCORE_NAME_LEN + 1] = "AAA";
    int  rank = -1;             /* row the run earned, -1 for none        */
    int  cell = 0;              /* initial being edited                   */
    int  final_score = 0;
    bool saved = true;
    bool ranked_run = false;    /* the finished run reached a board        */
    int  level_best = 0;        /* best ever flown on the level just flown */
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
            /* Any key counts as somebody being there: it puts the idle
             * timer back to zero, and the one that ends the attract cycle
             * only does that -- Esc should not quit the game on the way
             * back from a screen the player never asked for. */
            idle_t = 0.0;
            if (ui == UI_ATTRACT) {
                ui = UI_TITLE;
                continue;
            }

            /* Menu keys go through the digest on their way to the switch
             * below, so a pattern is recognised before the last key of it
             * is acted on. */
            if (key_open && ui == UI_TITLE) {
                if (key_dig == KEY_DIGEST_SEED)
                    key_first = t;
                key_dig = key_digest(key_dig, ev);
                if (key_dig == KEY_DIGEST_MATCH)
                    key_armed = true;
            }

            /* The editor takes the keyboard whole: its letters mean editing,
             * not sound and restart, and the arrows are read as held keys
             * further down rather than as events. */
            if (ui == UI_EDIT) {
                /* A field being typed takes every key: the letters are the
                 * name, not the tool keys they would otherwise be. */
                if (editor_typing(&editor) != ED_FIELD_NONE) {
                    if (ev == SWKEY_ENTER)
                        editor_type_end(&editor, true);
                    else if (ev == SWKEY_QUIT)
                        editor_type_end(&editor, false);
                    else if (ev == SWKEY_BACKSPACE)
                        editor_type_back(&editor);
                    else if (SWKEY_IS_CHAR(ev))
                        editor_type_char(&editor, SWKEY_CHAR(ev));
                    continue;
                }

                /* While something is being carried, only moving it, putting
                 * it down and putting it back mean anything. */
                if (editor_carrying(&editor) != ED_CARRY_NONE) {
                    char ch = SWKEY_IS_CHAR(ev) ? SWKEY_CHAR(ev) : 0;
                    if (ev == SWKEY_QUIT)
                        editor_ungrab(&editor);
                    else if (ev == SWKEY_ENTER || ch == ' ' ||
                             ch == 'g' || ch == 'G')
                        editor_drop(&editor);
                    continue;
                }

                if (ev == SWKEY_TAB) {
                    game_start(&game, editor_level(&editor), mode, gamenum);
                    run_marked = false;
                    flying = true;
                    ui = UI_PLAY;
                } else if (ev == SWKEY_QUIT) {
                    if (editor_dirty(&editor) && !leave_armed) {
                        leave_armed = true;
                        editor_note(&editor,
                                    "unsaved -- w to write, Esc again to go");
                    } else {
                        running = false;
                    }
                    continue;
                } else if (ev == SWKEY_BACKSPACE) {
                    editor_erase(&editor);
                } else if (SWKEY_IS_CHAR(ev)) {
                    /* Commands are letters whatever the shift key was doing;
                     * the text fields below take them as typed. */
                    char ch = SWKEY_CHAR(ev);
                    if (ch >= 'a' && ch <= 'z')
                        ch = (char)(ch - 'a' + 'A');
                    switch (ch) {
                    case ' ': editor_place(&editor);      break;
                    case 'T': editor_tool(&editor, 1);    break;
                    case 'K': editor_variant(&editor, 1); break;
                    case 'F': editor_flatten(&editor);    break;
                    case 'S': editor_smooth(&editor);     break;
                    case 'W': editor_save(&editor);       break;
                    case 'G': editor_grab(&editor);       break;
                    case 'N': editor_type_begin(&editor, ED_FIELD_NAME);
                              break;
                    case 'A': editor_type_begin(&editor, ED_FIELD_AUTHOR);
                              break;
                    case '[': editor_brush(&editor, -2);  break;
                    case ']': editor_brush(&editor, 2);   break;
                    default: break;
                    }
                }
                leave_armed = false;
                continue;
            }

            if (ui == UI_LEVELS) {
                if (ev == SWKEY_UP) {
                    level_sel = (level_sel + n_levels) % (n_levels + 1);
                } else if (ev == SWKEY_DOWN) {
                    level_sel = (level_sel + 1) % (n_levels + 1);
                } else if (ev == SWKEY_QUIT) {
                    ui = UI_TITLE;
                } else if (ev == SWKEY_ENTER) {
                    if (level_sel == 0) {
                        level_free(custom);
                        custom = NULL;
                        level = &level_classic;
                        ui = UI_TITLE;
                    } else {
                        level_t *picked = NULL;
                        if (level_load(levels[level_sel - 1].path,
                                       &picked) == 0) {
                            level_free(custom);
                            custom = picked;
                            level = custom;
                            ui = UI_TITLE;
                        } else {
                            /* It listed a moment ago, so something has
                             * happened to it since.  Show the list again. */
                            n_levels = level_list(levels, LEVEL_LIST_MAX,
                                                  &levels_skipped);
                            if (level_sel > n_levels)
                                level_sel = n_levels;
                        }
                    }
                }
                continue;
            }

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
                    rank = scores_insert(&scores, mode, initials,
                                         final_score);
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
            case SWKEY_TAB:
                if (flying) {
                    flying = false;
                    ui = UI_EDIT;
                    editor_note(&editor, "back to editing");
                }
                break;

            case SWKEY_QUIT:
                /* A test flight is not a run: Esc puts the level back on the
                 * bench rather than walking out through the score screen. */
                if (flying && (ui == UI_PLAY || ui == UI_PAUSED)) {
                    flying = false;
                    ui = UI_EDIT;
                    editor_note(&editor, "back to editing");
                } else if (ui == UI_PLAY || ui == UI_PAUSED) {
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

            case SWKEY_DIALS:
                /* Written out as soon as it changes: there is no exit hook
                 * to save it in, and the file is tiny. */
                dials = !dials;
                scores.dials = dials;
                scores_save(&scores);
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
                if (ui == UI_TITLE && menu_sel == 3) {
                    n_levels = level_list(levels, LEVEL_LIST_MAX,
                                          &levels_skipped);
                    level_sel = 0;
                    for (int i = 0; i < n_levels; i++)
                        if (custom && !strcmp(levels[i].name, custom->name))
                            level_sel = i + 1;
                    ui = UI_LEVELS;
                } else if (ui == UI_TITLE) {
                    mode = (menu_sel == 0) ? PLAY_NOVICE
                         : (menu_sel == 1) ? PLAY_SINGLE : PLAY_COMPUTER;
                    game_start(&game, level, mode, gamenum);
                    /* The window shuts behind the run it applies to: the
                     * allowance belongs to this game, not to the next. */
                    run_marked = key_armed;
                    if (run_marked) {
                        game_set_reserve(&game, KEY_RESERVE);
                        if (game.sound_on) {
                            chime_at = 0;
                            chime_t = 0.0;
                        }
                    }
                    key_open = false;
                    key_armed = false;
                    ui = UI_PLAY;
                } else if (ui == UI_OVER) {
                    game_start(&game, level, mode, gamenum);
                    run_marked = false;
                    ui = UI_PLAY;
                }
                break;

            case SWKEY_RESTART:
                if (ui == UI_PLAY) {
                    game_start(&game, level, mode, gamenum);
                    run_marked = false;
                }
                break;

            default:
                break;
            }
        }

        /* Panning and sculpting are held, not typed: a world 3000 columns
         * wide is no place to walk one key press at a time.  The longer an
         * arrow is down the faster the cursor runs. */
        if (ui == UI_EDIT && editor_typing(&editor) == ED_FIELD_NONE) {
            uint16_t held = platform_keys(plat);
            int dir = ((held & K_ACCEL) ? 1 : 0) - ((held & K_DEACC) ? 1 : 0);
            if (dir) {
                edit_t++;
                int step = 1 + (int)(edit_t / 8);
                editor_move(&editor, dir * (step > 16 ? 16 : step));
            } else {
                edit_t = 0;
            }

            int lift = ((held & K_FLAPU) ? 1 : 0) - ((held & K_FLAPD) ? 1 : 0);
            if (lift && (frames % 3) == 0)
                editor_raise(&editor, lift);
        }

        /* The title screen left alone turns into the attract cycle, and the
         * cycle steps from one board to the next.  Both run on real seconds
         * rather than frames, so they take as long on a 60 Hz panel as on a
         * 144 Hz one. */
        if (ui == UI_TITLE) {
            idle_t += dt;
            if (idle_t >= TITLE_IDLE_SECS) {
                attract_board = 0;
                attract_acc = 0.0;
                ui = UI_ATTRACT;
            }
        } else if (ui == UI_ATTRACT) {
            attract_acc += dt;
            if (attract_acc >= ATTRACT_BOARD_SECS) {
                attract_acc -= ATTRACT_BOARD_SECS;
                attract_board = (attract_board + 1) % SCORE_BOARDS;
            }
        } else {
            idle_t = 0.0;
        }

        /* The window is a startup affair, timed from the first frame on the
         * screen rather than from the first line of main(): what a player
         * sees start is the title coming up.  It closes when its time is up,
         * or the moment the title screen is left for anything else, and
         * there is nothing that opens it again. */
        if (key_open && shown > 0.0) {
            bool untouched = key_dig == KEY_DIGEST_SEED;
            if (ui != UI_TITLE ||
                (untouched  && t - shown     > KEY_WINDOW_OPEN) ||
                (!untouched && t - key_first > KEY_WINDOW_SPAN)) {
                key_open = false;
                key_armed = false;
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
        if (game.over && flying && (ui == UI_PLAY || ui == UI_PAUSED)) {
            flying = false;
            ui = UI_EDIT;
            editor_note(&editor, "the test flight ended -- back to editing");
        } else if (game.over && (ui == UI_PLAY || ui == UI_PAUSED)) {
            int b = score_board_of(mode);
            if (b < 0) b = 0;
            final_score = game_player(&game)->score;

            /* A run on the larger allowance is not a run the boards can be
             * compared against, any more than a run on somebody's own level
             * is: it is shown and not ranked. */
            ranked_run = game_ranked(&game) && !run_marked;
            rank = ranked_run ? scores_rank(&scores, mode, final_score) : -1;
            board = scores.board[b];
            over_t = 0;

            /* What a level is worth to the player who flew it, whether or
             * not any board will take it.  Keyed by the level's own hash, so
             * two copies of a level share a best and an edited one does not
             * inherit it. */
            level_best = 0;
            if (game_completed(&game) && !run_marked) {
                uint32_t id = level_hash(level);
                if (scores_best_set(&scores, id, final_score))
                    scores_save(&scores);
                level_best = scores_best(&scores, id);
            }

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

        /* While it lasts the chime owns the speaker: one square wave at a
         * time is all there ever was, and the engine has the rest of the
         * run to be heard in.  Switching sound off cuts it short. */
        if (chime_at < CHIME_NOTES) {
            if (!game.sound_on || ui != UI_PLAY) {
                chime_at = CHIME_NOTES;
            } else {
                chime_t += dt;
                while (chime_at < CHIME_NOTES && chime_t >= chime[chime_at].secs) {
                    chime_t -= chime[chime_at].secs;
                    chime_at++;
                }
            }
        }

        audio_tone(audio,
                   chime_at < CHIME_NOTES ? chime[chime_at].div
                   : (ui == UI_PLAY && game.sound_on)
                         ? (unsigned)game.sound.tone : 0u);
        audio_pump(audio, dt);

        framebuf_t *fb = platform_begin_frame(plat);
        if (fb) {
            render_ctx_t ctx;
            render_layout(&ctx, platform_style(plat), fb->w, fb->h);
            ctx.dials = dials;

            /* Where this frame sits inside the tick still being accumulated,
             * centred so the display runs half a tick early at most and half
             * a tick late at most, averaging the original's timing exactly.
             * Only while the world is actually advancing: on a paused or
             * finished game the accumulator is reset, and a stale fraction
             * would draw everything half a tick from where it is. */
            if (smooth && ui == UI_PLAY) {
                double a = tick_acc / tick_dt;
                if (a < 0.0) a = 0.0;
                if (a > 1.0) a = 1.0;
                ctx.lead_ticks = a - 0.5;
                ctx.trail_ticks = a - 1.0;   /* never ahead of a collision */
            }

            switch (ui) {
            case UI_TITLE:
                render_title(fb, &ctx, title_t++, menu_sel,
                             custom ? custom->name : NULL);
                break;
            case UI_ATTRACT: {
                /* No run to report, so no headline and no score line: just
                 * the board whose turn it is. */
                playmode_t m = attract_modes[attract_board];
                scoreboard_t v = {
                    .headline   = NULL,
                    .board_name = score_board_name(m),
                    .table      = &scores.board[score_board_of(m)],
                    .highlight  = -1,
                    .edit_cell  = -1,
                    .saved      = true,
                    .t          = title_t++,
                };
                render_scores(fb, &ctx, &v);
                break;
            }
            case UI_LEVELS: {
                char dir[512];
                /* One personal best per level, looked up by the hash
                 * level_list() recorded while it had the level open. */
                int bests[LEVEL_LIST_MAX];
                for (int i = 0; i < n_levels; i++)
                    bests[i] = scores_best(&scores, levels[i].hash);
                levelpick_t v = {
                    .items   = levels,
                    .n       = n_levels,
                    .sel     = level_sel,
                    .skipped = levels_skipped,
                    .dir     = level_dir(dir, sizeof(dir)) ? dir : NULL,
                    .best    = bests,
                    .best_classic = scores_best(&scores,
                                                level_hash(&level_classic)),
                };
                render_levels(fb, &ctx, &v);
                break;
            }
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
            case UI_EDIT: {
                editview_t v = {
                    .level     = editor_level(&editor),
                    .cursor    = editor.cursor,
                    .brush     = editor.brush,
                    .footprint = editor.tool == ED_TERRAIN ? 0
                               : editor.tool == ED_RUNWAY ? LEVEL_RUNWAY_SPAN
                                                          : LEVEL_TARGET_WIDTH,
                    .tool      = editor_tool_name(&editor),
                    .variant   = editor_variant_name(&editor),
                    .status    = editor_status(&editor),
                    .typing    = editor_typing(&editor) != ED_FIELD_NONE
                               ? editor_field_name(editor_typing(&editor))
                               : NULL,
                    .typing_text = editor_typing_text(&editor),
                    .dirty     = editor_dirty(&editor),
                    .t         = title_t++,
                };
                editor_carry_span(&editor, &v.carry_x, &v.carry_w);
                render_edit(fb, &ctx, &v);
                break;
            }
            case UI_OVER:
            case UI_ENTRY: {
                scoreboard_t v = {
                    .headline    = game.over_msg ? game.over_msg : "GAME OVER",
                    .board_name  = score_board_name(mode),
                    .final_score = final_score,
                    .ranked      = ranked_run,
                    .level_best  = level_best,
                    .table       = &board,
                    .highlight   = rank,
                    .edit_cell   = ui == UI_ENTRY ? cell : -1,
                    .saved       = saved,
                    .t           = over_t++,
                };
                /* render_scores() draws its own plain background: the board
                 * is a page like the menus, not a caption over the world. */
                render_scores(fb, &ctx, &v);
                break;
            }
            }
            if (dump_path && frames >= dump_after) {
                dump_ppm(fb, dump_path);
                running = false;
            }
            if (shown == 0.0)
                shown = t;
            frames++;
            platform_end_frame(plat);
        }

        platform_wait(plat, 8);
    }

    audio_close(audio);
    platform_close(plat);
    level_free(custom);
    return 0;
}
