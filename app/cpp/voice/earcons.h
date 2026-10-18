#ifndef APP_CPP_VOICE_EARCONS_H
#define APP_CPP_VOICE_EARCONS_H

// Short feedback sounds of the voice assistant ("earcons"), synthesized at
// run time: no audio files, no licensing questions, no flash space. The
// panel's small speaker reproduces little below ~800 Hz, so all sounds live
// at 1-3 kHz. The same code renders the preview WAVs on a desktop.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EARCON_STYLE_SHIMMER = 0, // rising, shimmering sweep ("futuristic activation")
    EARCON_STYLE_CHIME,       // two soft bell tones, a fifth apart
    EARCON_STYLE_BLIP,        // quick double blip gliding up ("gadget")
    EARCON_STYLE_SWITCH,      // soft click + airy tone ("switched")
    EARCON_STYLE_COUNT
} earcon_style_t;

typedef enum {
    EARCON_START = 0, // wake word / button: now listening
    EARCON_END,       // done listening (a falling, quieter counterpart)
} earcon_kind_t;

// Samples needed at `rate` Hz (upper bound).
size_t earcon_length(earcon_style_t style, earcon_kind_t kind, uint32_t rate);
// Renders mono int16 into `out` (capacity `cap`); returns the sample count.
size_t earcon_render(earcon_style_t style, earcon_kind_t kind, uint32_t rate, int16_t *out, size_t cap);

// Settings value <-> style ("shimmer", "chime", "blip", "switch"; anything
// else, e.g. "off", returns -1).
int earcon_style_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif // APP_CPP_VOICE_EARCONS_H
