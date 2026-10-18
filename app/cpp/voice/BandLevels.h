#ifndef APP_CPP_VOICE_BANDLEVELS_H
#define APP_CPP_VOICE_BANDLEVELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Levels 0..1 of three bands (low < ~300 Hz, mid ~300-2000 Hz, high > 2 kHz)
// for animations; plain C so the C views and the C stream player use it.
typedef struct {
    float low_state, mid_state;   // filter states
    float e[3];                   // smoothed band energies
} band_levels_t;
void band_levels_reset(band_levels_t* b);
// Feed mono samples (16 kHz); band_levels_get() maps energies to 0..1 (log scale).
void band_levels_feed(band_levels_t* b, const int16_t* samples, size_t count);
void band_levels_get(const band_levels_t* b, float out[3]);
#ifdef __cplusplus
}
#endif

#endif // APP_CPP_VOICE_BANDLEVELS_H
