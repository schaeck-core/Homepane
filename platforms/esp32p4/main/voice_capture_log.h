#pragma once

// The microphone audio of the last voice request, exactly as it was sent to
// Home Assistant (16 kHz mono), kept for diagnosis (audio debug server:
// /audio/voice.wav). Implemented in EspVoiceAudio.cpp.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Valid until the next voice request starts.
void voice_capture_log_get(const int16_t **samples, size_t *count);

#ifdef __cplusplus
}
#endif
