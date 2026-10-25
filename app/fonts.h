#ifndef FONTS_H
#define FONTS_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shared fonts (created once in fonts_init)

// Text: Manrope (design guide), tabular digits. Use the roles in ui_theme.h
// (UI_FONT_*) where one fits.
// Manrope 500 (text): 18, 22 px
extern lv_font_t *font_manrope_500_18;
extern lv_font_t *font_manrope_500_22;

// Manrope 600 (names, titles, values): 22, 26, 32, 40, 56, 72, 90 px
extern lv_font_t *font_manrope_600_22;
extern lv_font_t *font_manrope_600_26;
extern lv_font_t *font_manrope_600_32;
extern lv_font_t *font_manrope_600_40;
extern lv_font_t *font_manrope_600_56;
extern lv_font_t *font_manrope_600_72;
extern lv_font_t *font_manrope_600_90;
// Manrope 600, 96 px: digits and ':' only (the dashboard clock)
extern lv_font_t *font_manrope_600_96;
// Manrope 600, 160 px: digits and ':' only (the screensaver clock)
extern lv_font_t *font_manrope_600_160;
// Manrope 700, 44 px (the screensaver's event title)
extern lv_font_t *font_manrope_700_44;
// Manrope 700, 56 px: the letters of "homepane" only (the wordmark)
extern lv_font_t *font_manrope_700_56;

// Lucide line icons (design guide): 18, 24, 28, 36, 48, 56 px; glyphs in
// app/lucide_icons.h (LI_*)
extern lv_font_t *font_lucide_18;
extern lv_font_t *font_lucide_24;
extern lv_font_t *font_lucide_28;
extern lv_font_t *font_lucide_36;
extern lv_font_t *font_lucide_48;
extern lv_font_t *font_lucide_56;

// Initialize shared fonts; safe to call once at startup
void fonts_init(void);

// Optional: free/destroy fonts if needed at shutdown
void fonts_deinit(void);

// When using pre-generated fonts, their symbols are typically exposed as
// extern const lv_font_t font_name; in generated C. We keep pointers above
// for a uniform interface across both modes.

#ifdef __cplusplus
}
#endif

#endif // FONTS_H
