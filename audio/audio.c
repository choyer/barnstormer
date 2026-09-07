/*
 * audio.c -- square-wave synthesis standing in for the PC speaker.
 *
 * The game asks for tones the way DOS did: a divisor for the 8253's 1.19 MHz
 * clock.  We turn that into a frequency, generate a square wave, and push it
 * at ALSA.  Built without ALSA the whole module collapses into no-ops and the
 * game is simply silent, so nothing else has to care.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

#ifdef HAVE_ALSA
#include <alloca.h>
#include <alsa/asoundlib.h>

#define RATE      22050
#define PERIOD    256
#define BUFFER    2048
#define AMPLITUDE 5000     /* the real thing was harsh; this is not        */

struct audio {
    snd_pcm_t *pcm;
    unsigned divisor;
    double phase;          /* 0..1 through the current square-wave cycle   */
    int16_t buf[BUFFER];
};

audio_t *audio_open(void)
{
    audio_t *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;

    int err = snd_pcm_open(&a->pcm, "default", SND_PCM_STREAM_PLAYBACK,
                           SND_PCM_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "barnstormer: no audio device (%s); playing silently\n",
                snd_strerror(err));
        a->pcm = NULL;
        return a;                       /* silent, but still usable        */
    }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(a->pcm, hw);
    snd_pcm_hw_params_set_access(a->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(a->pcm, hw, SND_PCM_FORMAT_S16);
    snd_pcm_hw_params_set_channels(a->pcm, hw, 1);
    unsigned rate = RATE;
    snd_pcm_hw_params_set_rate_near(a->pcm, hw, &rate, NULL);
    snd_pcm_uframes_t period = PERIOD, buffer = BUFFER;
    snd_pcm_hw_params_set_period_size_near(a->pcm, hw, &period, NULL);
    snd_pcm_hw_params_set_buffer_size_near(a->pcm, hw, &buffer);

    if (snd_pcm_hw_params(a->pcm, hw) < 0) {
        fprintf(stderr, "barnstormer: audio device will not take 22 kHz mono "
                        "16-bit; playing silently\n");
        snd_pcm_close(a->pcm);
        a->pcm = NULL;
        return a;
    }
    snd_pcm_prepare(a->pcm);
    return a;
}

void audio_close(audio_t *a)
{
    if (!a)
        return;
    if (a->pcm) {
        snd_pcm_drop(a->pcm);
        snd_pcm_close(a->pcm);
    }
    free(a);
}

bool audio_available(const audio_t *a) { return a && a->pcm; }

void audio_tone(audio_t *a, unsigned divisor)
{
    if (a)
        a->divisor = divisor;
}

void audio_pump(audio_t *a, double seconds)
{
    (void)seconds;
    if (!a || !a->pcm)
        return;

    snd_pcm_sframes_t avail = snd_pcm_avail_update(a->pcm);
    if (avail < 0) {
        if (snd_pcm_recover(a->pcm, (int)avail, 1) < 0)
            return;
        avail = snd_pcm_avail_update(a->pcm);
        if (avail < 0)
            return;
    }

    /* Frequencies below about 20 Hz are the engine rumble; the original
     * produced a train of clicks there and so do we, for free. */
    double freq = a->divisor ? PIT_CLOCK_HZ / (double)a->divisor : 0.0;
    double step = freq / (double)RATE;

    while (avail > 0) {
        int n = (int)(avail > BUFFER ? BUFFER : avail);
        for (int i = 0; i < n; i++) {
            if (step <= 0.0) {
                a->buf[i] = 0;
                continue;
            }
            a->phase += step;
            if (a->phase >= 1.0)
                a->phase -= (double)(int)a->phase;
            a->buf[i] = (a->phase < 0.5) ? AMPLITUDE : -AMPLITUDE;
        }
        snd_pcm_sframes_t w = snd_pcm_writei(a->pcm, a->buf, (unsigned)n);
        if (w < 0) {
            snd_pcm_recover(a->pcm, (int)w, 1);
            return;
        }
        avail -= w;
        if (w < n)
            break;
    }
}

#else /* !HAVE_ALSA */

struct audio { int unused; };

audio_t *audio_open(void) { return NULL; }
void audio_close(audio_t *a) { (void)a; }
void audio_tone(audio_t *a, unsigned d) { (void)a; (void)d; }
void audio_pump(audio_t *a, double s) { (void)a; (void)s; }
bool audio_available(const audio_t *a) { (void)a; return false; }

#endif
