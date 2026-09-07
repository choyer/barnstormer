/*
 * sound.c -- PC-speaker voice selection.
 *
 * The original had exactly one voice: the 8253's channel 2 driving the
 * speaker cone.  Everything that wanted to be heard called sound() with a
 * priority, the lowest number won the tick, and the winner was turned into a
 * timer divisor.  Continuous effects (a falling bomb, a spinning aircraft)
 * carried their own tone and a per-tick drift so they slid in pitch.
 *
 * All of that is reproduced here, including the little four-bar tune that
 * plays while wreckage is in the air.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

/* ---- the explosion tune (original SWSOUND.C) --------------------------- */

static const char *const expltune[] = {
    "b4/d8/d2/r16/c8/b8/a8/b4./c4./c+4./d4./",
    "e4/g8/g2/r16/>a8/<g8/e8/d2./",
    "b4/d8/d2/r16/c8/b8/a8/b4./c4./c+4./d4./",
    "e4/>a8/a2/r16/<g8/f+8/e8/d2./",
    "d8/g2/r16/g8/g+2/r16/g+8/>a2/r16/a8/c2/r16/",
    "b8/a8/<g8/>b4/<g8/>b4/<g8/>a4./<g1/",
    "",
};

static struct {
    int  line, place;
    int  octave;
    unsigned freq;
    int  duration;
} tuneplay;

static int  numexpls;
static int  explticks;
static unsigned expltone;
static int  explline, explplace, exploctv;
static unsigned lastfreq;
static unsigned shot_saved;

/* The original's fixed pseudo-random table, so "hit" noise sounds the same. */
static const uint16_t noise_seed[50] = {
    0x90B9, 0xBCFB, 0x6564, 0x3313, 0x3190, 0xA980, 0xBCF0, 0x6F97,
    0x37F4, 0x064B, 0x9FD8, 0x595B, 0x1EEE, 0x820C, 0x4201, 0x651E,
    0x848E, 0x15D5, 0x1DE7, 0x1585, 0xA850, 0x213B, 0x3953, 0x1EB0,
    0x97A7, 0x35DD, 0xAF2F, 0x1629, 0xBE9B, 0x243F, 0x847D, 0x313A,
    0x3295, 0xBC11, 0x6E6D, 0x3398, 0xAD43, 0x51CE, 0x8F95, 0x507E,
    0x499E, 0x3BC1, 0x5243, 0x2017, 0x9510, 0x9865, 0x65F6, 0x6B56,
    0x36B9, 0x5026,
};
static int noise_i;

static unsigned noise_rand(unsigned modulo)
{
    if (noise_i >= 50)
        noise_i = 0;
    return noise_seed[noise_i++] % modulo;
}

/* 16.16 multiply-and-shift, matching the original's soundmul assembly. */
static unsigned soundmul(unsigned a, unsigned b, unsigned c)
{
    return (unsigned)(((uint64_t)a * b * c) >> 16) & 0xFFFF;
}

/* Parse one note out of the tune and leave its divisor in tuneplay.freq. */
static void play_note(void)
{
    static const int noteindex[] = { 0, 2, 3, 5, 7, 8, 10 };
    static const int notefreq[]  = { 440, 466, 494, 523, 554, 587,
                                     622, 659, 698, 740, 784, 831 };
    char durstring[8] = { 0 };
    int durplace = 0, indexadj = 0, dotted = 2;
    int noteoctave = 256, index = 0;
    char noteletter = 0;
    bool first = true;

    for (;;) {
        if (!tuneplay.line && !tuneplay.place)
            tuneplay.octave = 256;

        char c = (char)toupper((unsigned char)
                               expltune[tuneplay.line][tuneplay.place++]);
        if (!c) {
            tuneplay.place = 0;
            if (!expltune[++tuneplay.line][0])
                tuneplay.line = 0;
            if (first)
                continue;
            break;
        }
        first = false;
        if (c == '/')
            break;

        if (isalpha((unsigned char)c)) {
            index = noteindex[(c - 'A') % 7];
            noteletter = c;
        } else switch (c) {
            case '>': tuneplay.octave <<= 1; break;
            case '<': tuneplay.octave >>= 1; break;
            case '+': indexadj++; break;
            case '-': indexadj--; break;
            case '.': dotted = 3; break;
            default:
                if (isdigit((unsigned char)c) &&
                    durplace < (int)sizeof(durstring) - 1)
                    durstring[durplace++] = c;
                break;
        }
    }

    durstring[durplace] = 0;
    int duration = atoi(durstring);
    if (duration <= 0)
        duration = 4;
    tuneplay.duration = (1440 * dotted / (60 * duration)) >> 1;

    unsigned freq;
    if (noteletter == 'R') {
        freq = 32000;                       /* effectively silent          */
    } else {
        index += indexadj;
        while (index < 0)   { index += 12; noteoctave >>= 1; }
        while (index >= 12) { index -= 12; noteoctave <<= 1; }
        freq = soundmul((unsigned)notefreq[index],
                        (unsigned)tuneplay.octave, (unsigned)noteoctave);
    }
    tuneplay.freq = freq ? (unsigned)(1331000UL / freq) : 0;
}

static void expl_note(void)
{
    tuneplay.line = explline;
    tuneplay.place = explplace;
    tuneplay.octave = exploctv;
    play_note();
    explline = tuneplay.line;
    explplace = tuneplay.place;
    exploctv = tuneplay.octave;
    expltone = tuneplay.freq;
    explticks += tuneplay.duration;
}

/* ---- public interface -------------------------------------------------- */

void sw_sound_reset(game_t *g)
{
    numexpls = explticks = 0;
    explline = explplace = 0;
    exploctv = 256;
    expltone = 0;
    lastfreq = 0;
    noise_i = 0;
    g->sound_type = g->sound_parm = S_NONE;
    g->sound_obj = g->sound_last = NULL;
    g->sound.type = S_NONE;
    g->sound.tone = 0;
}

void sw_sound_request(game_t *g, int type, int parm, object_t *ob)
{
    if (type < g->sound_type) {
        g->sound_type = type;
        g->sound_parm = parm;
        g->sound_obj = ob;
    } else if (type == g->sound_type && parm < g->sound_parm) {
        g->sound_parm = parm;
        g->sound_obj = ob;
    }
}

void sw_sound_start(game_t *g, object_t *ob, int type)
{
    if (ob->has_sound)
        return;

    if (ob->type == OBJ_EXPLOSION) {
        if (++numexpls == 1) {
            explline = explplace = 0;
            exploctv = 256;
            explticks = 0;
            expl_note();
        }
        ob->has_sound = true;
        return;
    }

    switch (type) {
    case S_BOMB:                     /* rising divisor: a falling whistle  */
        ob->sound_tone = 0x0300;
        ob->sound_chng = 8;
        break;
    case S_FALLING:                  /* falling divisor: a rising scream   */
        ob->sound_tone = 0x1200;
        ob->sound_chng = -8;
        break;
    default:
        return;
    }
    ob->has_sound = true;
    (void)g;
}

void sw_sound_stop(game_t *g, object_t *ob)
{
    (void)g;
    if (!ob->has_sound)
        return;
    if (ob->type == OBJ_EXPLOSION && numexpls)
        numexpls--;
    ob->has_sound = false;
}

/* Called at the original 18.2 Hz timer rate. */
void sw_sound_adj(game_t *g)
{
    if (numexpls && --explticks < 0)
        expl_note();

    /* The machine gun alternates between its note and a near-silent one,
     * which is what gives it that ratcheting texture. */
    if (g->sound.type == S_SHOT) {
        if (lastfreq == 0xF000) {
            lastfreq = shot_saved;
        } else {
            shot_saved = lastfreq;
            lastfreq = 0xF000;
        }
        g->sound.tone = (int)lastfreq;
    }
}

void sw_sound_resolve(game_t *g)
{
    /* Continuous voices drift in pitch by a fixed amount every tick.  The
     * original drifted per 18.2 Hz timer tick, i.e. 1.5 times per move. */
    int drift = (g->countmove & 1) ? 2 : 1;
    for (object_t *ob = g->top; ob; ob = ob->next)
        if (ob->has_sound && ob->type != OBJ_EXPLOSION)
            ob->sound_tone += ob->sound_chng * drift;

    unsigned tone = 0;
    int type = g->sound_type;

    switch (type) {
    case S_PLANE:
        /* parm is -speed, so a faster engine gives a smaller divisor. */
        tone = (g->sound_parm == 0) ? 0xF000u
                                    : (unsigned)(0xD000 + g->sound_parm * 0x1000);
        break;

    case S_BOMB:
    case S_FALLING:
        if (g->sound_obj && g->sound_obj->has_sound)
            tone = (unsigned)g->sound_obj->sound_tone & 0xFFFF;
        break;

    case S_HIT:
        tone = noise_rand(2) ? 0x9000 : 0xF000;
        break;

    case S_EXPLOSION:
        tone = expltone;
        break;

    case S_SHOT:
        tone = 0x1000;
        break;

    default:
        type = S_NONE;
        tone = 0;
        break;
    }

    g->sound.restart = (type != g->sound.type);
    g->sound.type = type;
    g->sound.tone = g->sound_on ? (int)tone : 0;
    lastfreq = tone;

    g->sound_type = g->sound_parm = S_NONE;
    g->sound_obj = NULL;
}
