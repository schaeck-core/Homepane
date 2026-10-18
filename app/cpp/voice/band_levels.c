#include "BandLevels.h"

#include <math.h>
#include <string.h>

/* Two one-pole low-passes split the signal: low = LP(300 Hz), the rest
 * minus LP(2 kHz) = high, in between = mid. Cheap enough to run on every
 * sample at 16 kHz (a few multiply-adds). */
#define A_LOW 0.111f  /* 1 - exp(-2*pi*300/16000) */
#define A_MID 0.544f  /* 1 - exp(-2*pi*2000/16000) */

void band_levels_reset(band_levels_t *b) { memset(b, 0, sizeof(*b)); }

void band_levels_feed(band_levels_t *b, const int16_t *samples, size_t count)
{
    if (!count) return;
    double e[3] = {0, 0, 0};
    for (size_t i = 0; i < count; ++i) {
        const float x = samples[i];
        b->low_state += A_LOW * (x - b->low_state);
        b->mid_state += A_MID * (x - b->mid_state);
        const float low = b->low_state;
        const float mid = b->mid_state - b->low_state;
        const float high = x - b->mid_state;
        e[0] += (double)low * low;
        e[1] += (double)mid * mid;
        e[2] += (double)high * high;
    }
    for (int k = 0; k < 3; ++k) {
        const float v = (float)(e[k] / count);
        b->e[k] += (v - b->e[k]) * 0.5f; /* per block; the view smooths further */
    }
}

void band_levels_get(const band_levels_t *b, float out[3])
{
    for (int k = 0; k < 3; ++k) {
        /* ~-55 dBFS -> 0, ~-15 dBFS -> 1 */
        const float db = 10.0f * log10f(b->e[k] / (32768.0f * 32768.0f) + 1e-12f);
        float v = (db + 55.0f) / 40.0f;
        out[k] = v < 0 ? 0 : v > 1 ? 1 : v;
    }
}
