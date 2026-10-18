#pragma once

// Speaker (ES8311 + amplifier) and microphones (ES7210) on the shared I2S
// bus of the ESP32-P4-WIFI6-Touch-LCD-4B, for the voice assistant
// (docs/voice/VOICE_ASSISTANT_PLAN.md, phase V0).
//
// Both directions run at AUDIO_HW_SAMPLE_RATE, 16 bit, 2 channels
// (interleaved L/R frames): playback and capture share the I2S clock, and the
// ES7210 delivers MIC1 on the left and MIC2 on the right slot.

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_HW_SAMPLE_RATE 16000
#define AUDIO_HW_CHANNELS 2

// Idempotent; safe to call from any task. Opens both codecs.
esp_err_t audio_hw_init(void);

// Blocking: reads/writes exactly `frames` stereo frames (2 * frames samples).
esp_err_t audio_hw_read(int16_t *frames_lr, size_t frames);
esp_err_t audio_hw_write(const int16_t *frames_lr, size_t frames);

// Speaker volume 0-100; microphone gain in dB (ES7210: 0-37.5).
esp_err_t audio_hw_set_volume(int percent);
esp_err_t audio_hw_set_mic_gain(float db);

#ifdef __cplusplus
}
#endif
