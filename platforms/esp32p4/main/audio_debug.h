#pragma once

// Developer-only HTTP endpoints for the audio bring-up (voice plan, V0):
// tone, record (returns a stereo WAV), playback, gain/volume, loopback.
// Compiled only with HOMEPANE_AUDIO_DEBUG (environment variable at CMake
// configure time): it exposes the microphones on the LAN without
// authentication and must never be in a release build.

#ifdef __cplusplus
extern "C" {
#endif

void audio_debug_start(void);

#ifdef __cplusplus
}
#endif
