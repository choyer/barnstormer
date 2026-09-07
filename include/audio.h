/*
 * audio.h -- PC-speaker emulation.
 *
 * The original drove the 8253 channel 2 directly, so every sound is a square
 * wave described by a timer divisor.  We reproduce that literally: the game
 * hands us divisors, we synthesise the square wave and push it to ALSA.  If
 * ALSA is unavailable at build time every call becomes a no-op and the game
 * still runs.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define PIT_CLOCK_HZ 1193182.0

typedef struct audio audio_t;

audio_t *audio_open(void);
void     audio_close(audio_t *a);

/* Set the current square-wave divisor; 0 silences the speaker. */
void     audio_tone(audio_t *a, unsigned divisor);

/* Push `frames` worth of samples, called once per rendered frame. */
void     audio_pump(audio_t *a, double seconds);

bool     audio_available(const audio_t *a);

#endif /* AUDIO_H */
