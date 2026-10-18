#ifndef APP_VIEWS_TEXT_FIT_H
#define APP_VIEWS_TEXT_FIT_H

#include <stddef.h>

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Copies UTF-8 `in` to `out` so every character exists in `font`: missing
// ones get a plain substitute (typographic quotes/dashes/ellipsis/bullets/
// no-break spaces) or are dropped (emoji, symbols, scripts the bitmap font
// does not carry) — no empty boxes in texts from HA or the LLM.
void text_fit_font(const lv_font_t *font, const char *in, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // APP_VIEWS_TEXT_FIT_H
