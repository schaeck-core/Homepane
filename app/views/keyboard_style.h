#ifndef APP_VIEWS_KEYBOARD_STYLE_H
#define APP_VIEWS_KEYBOARD_STYLE_H

#include "lvgl/lvgl.h"
#include "../fonts.h"

// Dark on-screen keyboard matching the app's surfaces (the LVGL default
// theme renders it light grey/white). Shared by the settings keyboard and
// the WiFi picker's password dialog.
static inline void ui_style_keyboard(lv_obj_t *kb)
{
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(kb, 8, LV_PART_MAIN);

    lv_obj_set_style_bg_color(kb, lv_color_hex(0x2e2e2e), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    // Control keys (shift, mode, backspace, enter, ...) a shade lighter.
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x444444), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xD6B98A), LV_PART_ITEMS | LV_STATE_PRESSED); /* sand */
    lv_obj_set_style_text_color(kb, lv_color_black(), LV_PART_ITEMS | LV_STATE_PRESSED);
}

#endif // APP_VIEWS_KEYBOARD_STYLE_H
