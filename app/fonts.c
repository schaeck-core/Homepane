#include <stddef.h>
#include "lvgl/lvgl.h"
#include "fonts.h"


// Shared font pointers

// Manrope 500: 18, 22 px
lv_font_t *font_manrope_500_18 = NULL;
lv_font_t *font_manrope_500_22 = NULL;

// Manrope 600: 22, 26, 32, 40, 56, 72, 90, 96 (digits) px
lv_font_t *font_manrope_600_22 = NULL;
lv_font_t *font_manrope_600_26 = NULL;
lv_font_t *font_manrope_600_32 = NULL;
lv_font_t *font_manrope_600_40 = NULL;
lv_font_t *font_manrope_600_56 = NULL;
lv_font_t *font_manrope_600_72 = NULL;
lv_font_t *font_manrope_600_90 = NULL;
lv_font_t *font_manrope_600_96 = NULL;
lv_font_t *font_manrope_600_160 = NULL;
lv_font_t *font_manrope_700_44 = NULL;
lv_font_t *font_manrope_700_56 = NULL;


// Lucide line icons
lv_font_t *font_lucide_18 = NULL;
lv_font_t *font_lucide_24 = NULL;
lv_font_t *font_lucide_28 = NULL;
lv_font_t *font_lucide_36 = NULL;
lv_font_t *font_lucide_48 = NULL;
lv_font_t *font_lucide_56 = NULL;

static bool fonts_inited = false;

void fonts_init(void) {
    if (fonts_inited) return;

    // Link to generated font symbols if available

    // Lucide
    extern const lv_font_t font_lucide_18_c;
    font_lucide_18 = (lv_font_t *)&font_lucide_18_c;
    extern const lv_font_t font_lucide_24_c;
    font_lucide_24 = (lv_font_t *)&font_lucide_24_c;
    extern const lv_font_t font_lucide_28_c;
    font_lucide_28 = (lv_font_t *)&font_lucide_28_c;
    extern const lv_font_t font_lucide_36_c;
    font_lucide_36 = (lv_font_t *)&font_lucide_36_c;
    extern const lv_font_t font_lucide_48_c;
    font_lucide_48 = (lv_font_t *)&font_lucide_48_c;
    extern const lv_font_t font_lucide_56_c;
    font_lucide_56 = (lv_font_t *)&font_lucide_56_c;
    
    // Manrope 500
    extern const lv_font_t font_manrope_500_18_c;
    font_manrope_500_18 = (lv_font_t *)&font_manrope_500_18_c;

    extern const lv_font_t font_manrope_500_22_c;
    font_manrope_500_22 = (lv_font_t *)&font_manrope_500_22_c;

    // Manrope 600
    extern const lv_font_t font_manrope_600_22_c;
    font_manrope_600_22 = (lv_font_t *)&font_manrope_600_22_c;

    extern const lv_font_t font_manrope_600_26_c;
    font_manrope_600_26 = (lv_font_t *)&font_manrope_600_26_c;

    extern const lv_font_t font_manrope_600_32_c;
    font_manrope_600_32 = (lv_font_t *)&font_manrope_600_32_c;

    extern const lv_font_t font_manrope_600_40_c;
    font_manrope_600_40 = (lv_font_t *)&font_manrope_600_40_c;

    extern const lv_font_t font_manrope_600_56_c;
    font_manrope_600_56 = (lv_font_t *)&font_manrope_600_56_c;

    extern const lv_font_t font_manrope_600_72_c;
    font_manrope_600_72 = (lv_font_t *)&font_manrope_600_72_c;

    extern const lv_font_t font_manrope_600_90_c;
    font_manrope_600_90 = (lv_font_t *)&font_manrope_600_90_c;

    extern const lv_font_t font_manrope_600_96_c;
    font_manrope_600_96 = (lv_font_t *)&font_manrope_600_96_c;

    extern const lv_font_t font_manrope_600_160_c;
    font_manrope_600_160 = (lv_font_t *)&font_manrope_600_160_c;

    extern const lv_font_t font_manrope_700_44_c;
    font_manrope_700_44 = (lv_font_t *)&font_manrope_700_44_c;

    extern const lv_font_t font_manrope_700_56_c;
    font_manrope_700_56 = (lv_font_t *)&font_manrope_700_56_c;

    fonts_inited = true;
}

void fonts_deinit(void) {
    // Typically not required in embedded lifecycle; noop on desktop sim as well
}
