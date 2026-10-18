#include "earcons.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define TWO_PI 6.28318531f

static float durations[EARCON_STYLE_COUNT][2] = {
    /* start, end (seconds) */
    {0.55f, 0.40f}, /* shimmer */
    {0.70f, 0.50f}, /* chime */
    {0.26f, 0.22f}, /* blip */
    {0.30f, 0.25f}, /* switch */
};

size_t earcon_length(earcon_style_t style, earcon_kind_t kind, uint32_t rate)
{
    if (style >= EARCON_STYLE_COUNT) return 0;
    return (size_t)(durations[style][kind] * rate) + 1;
}

int earcon_style_from_name(const char *name)
{
    static const char *const names[EARCON_STYLE_COUNT] = {"shimmer", "chime", "blip", "switch"};
    for (int i = 0; name && i < EARCON_STYLE_COUNT; ++i) {
        if (strcmp(name, names[i]) == 0) return i;
    }
    return -1;
}

/* Small deterministic noise (no rand(): same sound on every call). */
static float noise(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return ((int32_t)(*state >> 8) - 0x400000) / (float)0x400000;
}

/* sinf() of a phase in cycles, reduced to one cycle first: newlib's sinf
 * falls back to a very slow argument reduction for large angles (seconds of
 * CPU on the device for one sound). */
static float sin_cycles(float cycles)
{
    cycles -= floorf(cycles + 0.5f); /* -0.5..0.5 */
    return sinf(TWO_PI * cycles);
}

/* Bell-like partial: decaying sine with a short attack. */
static float bell(float t, float f, float decay, float attack)
{
    if (t < 0) return 0;
    const float env = (t < attack ? t / attack : 1.0f) * expf(-t / decay);
    return env * sin_cycles(f * t);
}

/* Keeps an accumulated phase (radians) within one turn. */
static float wrap(float phase)
{
    return phase > TWO_PI ? phase - TWO_PI : phase;
}

size_t earcon_render(earcon_style_t style, earcon_kind_t kind, uint32_t rate, int16_t *out, size_t cap)
{
    const size_t n = earcon_length(style, kind, rate);
    if (!out || n > cap) return 0;
    const float dur = durations[style][kind];
    const bool end = kind == EARCON_END;
    const float gain = end ? 0.55f : 0.8f; /* the end sound is quieter */
    uint32_t seed = 12345u + (uint32_t)style * 77u;
    float phase = 0, phase2 = 0, lp = 0;

    for (size_t i = 0; i < n; ++i) {
        const float t = (float)i / rate;
        const float u = t / dur; /* 0..1 over the sound */
        float v = 0;
        switch (style) {
        case EARCON_STYLE_SHIMMER: {
            /* Sweep 1.1 -> 2.2 kHz (end: falling), smooth-step glide, with a
             * vibrato-like shimmer and a sparkling upper octave fading in. */
            const float g = u * u * (3 - 2 * u);
            const float f = end ? 2000.0f - 800.0f * g : 1100.0f + 1100.0f * g;
            phase = wrap(phase + TWO_PI * f / rate);
            phase2 = wrap(phase2 + TWO_PI * (f * 2.0f + 7.0f) / rate); /* slight detune: shimmer */
            const float env = sinf(3.14159265f * fminf(u * 1.15f, 1.0f)); /* swell and fade */
            v = env * (0.65f * sinf(phase) + 0.25f * u * sinf(phase2) + 0.08f * sin_cycles(phase * 3.0f / TWO_PI));
            break;
        }
        case EARCON_STYLE_CHIME: {
            /* Two bell tones a fifth apart (C6 -> G6; end: G6 -> C6), each
             * with a soft octave partial. */
            const float f1 = end ? 1568.0f : 1046.5f, f2 = end ? 1046.5f : 1568.0f;
            const float second = 0.14f;
            v = 0.55f * (bell(t, f1, 0.22f, 0.004f) + 0.3f * bell(t, f1 * 2.01f, 0.09f, 0.002f)) +
                0.55f * (bell(t - second, f2, 0.28f, 0.004f) + 0.3f * bell(t - second, f2 * 2.01f, 0.1f, 0.002f));
            break;
        }
        case EARCON_STYLE_BLIP: {
            /* Two short blips, each gliding up (end: down), the second higher. */
            const float split = 0.11f;
            const bool second = t >= split;
            const float tt = second ? t - split : t;
            const float len = second ? dur - split : split;
            const float k = tt / len;
            const float base = second ? 1900.0f : 1400.0f;
            const float f = end ? base * (1.15f - 0.3f * k) : base * (0.85f + 0.3f * k);
            phase = wrap(phase + TWO_PI * f / rate);
            const float env = sinf(3.14159265f * fminf(k, 1.0f));
            v = env * env * (0.7f * sinf(phase) + 0.15f * sin_cycles(phase * 2.0f / TWO_PI));
            break;
        }
        case EARCON_STYLE_SWITCH: {
            /* A soft filtered click, then an airy tone with a little breath. */
            /* click high-passed (noise minus its low-pass): a crisp tick
             * the small speaker can reproduce, no low rumble */
            const float click = t < 0.010f ? noise(&seed) * (1.0f - t / 0.010f) : 0.0f;
            lp += 0.25f * (click - lp);
            const float tick = click - lp;
            const float f = end ? 1300.0f : 1750.0f;
            const float tone = bell(t - 0.015f, f, end ? 0.07f : 0.09f, 0.008f);
            const float air = noise(&seed) * 0.05f * expf(-t / 0.06f);
            v = 0.45f * tick + 0.6f * tone + air;
            break;
        }
        default: break;
        }
        /* 5 ms fade-out at the very end: no click */
        const float tail = (n - i) / (0.005f * rate);
        if (tail < 1) v *= tail;
        float s = v * gain * 32767.0f;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
    (void)phase2;
    return n;
}
