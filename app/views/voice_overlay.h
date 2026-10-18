#ifndef APP_VIEWS_VOICE_OVERLAY_H
#define APP_VIEWS_VOICE_OVERLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Push-to-talk test UI for the voice assistant (docs/voice/VOICE_ASSISTANT_PLAN.md,
// V2): a microphone button on the top layer and a full-screen overlay
// (design guide variant A, simplified) while the assistant is active.
// Call once, under the display lock, after the app UI exists.
void voice_overlay_init(void);

// Re-reads the settings (microphone_enabled, voice_enabled, voice_wake_word)
// and applies them: microphone button shown or hidden, assistant and wake
// word on or off. Call after any of them changed (UI thread).
void voice_overlay_apply_settings(void);

// Hides the active screen while a full-screen voice overlay is shown.
// LVGL does not treat the top layer as covering the screen below, so the
// dashboard would otherwise be redrawn under the overlay on every frame.
void voice_overlay_cover_screen(bool cover);

#ifdef __cplusplus
}
#endif

#endif // APP_VIEWS_VOICE_OVERLAY_H
