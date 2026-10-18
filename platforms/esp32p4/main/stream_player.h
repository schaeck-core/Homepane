#pragma once

// Streaming audio player: feed it a WAV (16-bit PCM) or MP3 stream in
// arbitrary pieces as they arrive (HTTP body of the Assist TTS response,
// which HA produces while the answer is still being generated). The format
// is detected from the first bytes; audio is mixed to mono, resampled
// linearly to AUDIO_HW_SAMPLE_RATE and written to the speaker
// (audio_hw_write, blocking).
//
// The struct is large (MP3 decoder state + buffers): allocate it on the heap.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stream_player stream_player_t;

stream_player_t *stream_player_create(void);
void stream_player_destroy(stream_player_t *p);
// Returns false once the stream is unusable (stream_player_error() says why).
bool stream_player_feed(stream_player_t *p, const uint8_t *data, size_t len);
// End of input: decodes what is buffered and writes the rest.
void stream_player_finish(stream_player_t *p);
const char *stream_player_error(const stream_player_t *p);

// Level of what is playing right now, 0..1 (~-50 dBFS .. ~-10 dBFS, log
// scale), for the "speaking" animation. Any thread.
float stream_player_level(void);
// Low / mid / high band levels 0..1 of what is playing. Any thread (racy
// reads of floats are fine for an animation).
void stream_player_bands(float out[3]);

#ifdef __cplusplus
}
#endif
