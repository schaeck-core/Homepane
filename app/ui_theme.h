/* Design tokens (docs/design/DESIGN_GUIDE.md): colours, sizes and font
 * roles in one place. New UI uses these instead of hex values. */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "fonts.h"
#include "lucide_icons.h"

/* Colours */
#define UI_BG          0x000000 /* screen: true black */
#define UI_SURFACE     0x141413 /* tiles, cards, buttons */
#define UI_SURFACE2    0x1F1E1C /* icon circles, dividers, tracks */
#define UI_ACTIVE      0x2B2A27 /* selected, slider fill, toggle off */
#define UI_TEXT        0xECE9E3 /* main text, icons */
#define UI_TEXT2       0xA39F97 /* secondary text, labels */
#define UI_TEXT3       0x8A857D /* tertiary text, chevrons, "–" */
#define UI_ACCENT      0xD6B98A /* sand: toggle on, progress, highlight */
#define UI_ACCENT_BG   0x2E271C /* background behind sand ("on" icon circle) */
#define UI_HEAT        0xCF9271
#define UI_HEAT_BG     0x33231B
#define UI_COOL        0x94AFC4
#define UI_COOL_BG     0x1C252C
#define UI_AUTO        0xC9B79A /* climate auto mode */
#define UI_WARN        0xD98A73 /* alarm, error */
#define UI_WARN_BG     0x2A1D18
#define UI_OK          0x8FA88A /* connected, ok (status dot) */
#define UI_KEY_BG      0x0B0B0A /* special keys in keypads */
#define UI_BAR         0x34322E /* chart bars, dashed empty line */
#define UI_MUTED       0x5E5A54 /* placeholder art */
#define UI_INACTIVE    0x4A4842 /* inactive page dots, unchecked boxes */

/* Sizes */
#define UI_EDGE        16  /* screen edge */
#define UI_GAP         12  /* grid gap */
#define UI_GAP_LIST    8
#define UI_TILE_W      221 /* dashboard tile: 3 columns at 16 edge, 12 gap */
#define UI_TILE_H      308
#define UI_TILE_PAD    16
#define UI_R_TILE      24
#define UI_R_CONTROL   20  /* rows, header buttons, tile buttons */
#define UI_R_KEY       22
#define UI_TOUCH_MIN   64
#define UI_HEADER_H    64
#define UI_BUTTON_H    64  /* buttons inside tiles */
#define UI_BUBBLE      112 /* icon circle of a centred tile */
#define UI_BUBBLE_SMALL 96 /* ... when the tile also has buttons */
#define UI_HOLD_MS     480

/* Font roles: Manrope with tabular digits (scripts/make_manrope.py). The
 * sizes are still the ones the UI was laid out with; the guide's in
 * brackets where they differ. */
#define UI_FONT_LABEL  font_manrope_500_18 /* 18/500: status, labels, secondary */
#define UI_FONT_BODY   font_manrope_500_22 /* 22/500: running text */
#define UI_FONT_NAME   font_manrope_600_22 /* 22/600: names, row titles */
#define UI_FONT_TITLE  font_manrope_600_26 /* 28/600: page titles */
#define UI_FONT_VALUE  font_manrope_600_40 /* 44/600: measured values */
#define UI_FONT_HUGE   font_manrope_600_90 /* 96/600: clock, main value */

/* Icon sizes (Lucide line icons) */
#define UI_ICON_S      font_lucide_18
#define UI_ICON_BUTTON font_lucide_24 /* buttons */
#define UI_ICON_M      font_lucide_28
#define UI_ICON_VALUE  font_lucide_36
#define UI_ICON_TILE   font_lucide_48 /* icon in a tile's circle */
#define UI_ICON_L      font_lucide_56

#endif /* UI_THEME_H */
