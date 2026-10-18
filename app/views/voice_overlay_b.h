#ifndef APP_VIEWS_VOICE_OVERLAY_B_H
#define APP_VIEWS_VOICE_OVERLAY_B_H

#include "../cpp/app_facade.h"

#ifdef __cplusplus
extern "C" {
#endif

// Voice assistant overlay, design variant B "Aurora"
// (docs/design/canvas/VoiceAssistantB.dc.html): glowing waves that follow
// the microphone while listening and the response audio while speaking, a
// particle ring while processing, a ring + check when done.
//
// Drawn by a small software rasterizer into a half-resolution RGB888 canvas
// that LVGL scales up 2x (bilinear): the upscale gives the soft glow of the
// design (LVGL 9.2 has no blur) at a quarter of the pixel work.

// Builds the (hidden) overlay on the top layer; `on_tap` for taps on it and
// on its microphone button. Under the display lock.
void voice_overlay_b_init(lv_event_cb_t on_tap);
// Shows the overlay for `st` (or hides it for IDLE). UI thread.
void voice_overlay_b_update(const app_cpp_voice_status_t *st);

// Developer aid (audio debug server): show a state with a fixed level
// (0..1; < 0 = live levels again).
void voice_overlay_b_demo(app_cpp_voice_state_t state, float level, const char *text);

#ifdef __cplusplus
}
#endif

#endif // APP_VIEWS_VOICE_OVERLAY_B_H
