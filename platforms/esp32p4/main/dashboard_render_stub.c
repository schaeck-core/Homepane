/**
 * ESP32-P4 Dashboard render stub.
 *
 * The full dashboard view (app/views/dashboard.c) depends on all widget
 * implementations and the settings subsystem.  Until those are ported to
 * ESP32, this stub provides a minimal placeholder so the firmware links
 * and the setup flow completes successfully.
 *
 * TODO: Replace with the real dashboard_render once all widgets compile.
 */

#ifdef ESP_PLATFORM
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "../app/fonts.h"
#include "../app/config.h"

void dashboard_render(lv_obj_t *parent)
{
    // Dark background
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x111111), LV_PART_MAIN);

    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(container);
    lv_label_set_text(icon, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(icon, font_manrope_600_56, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x2962FF), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(container);
    const char *dashboard = getConfig("haas_dashboard");
    char buf[128];
    snprintf(buf, sizeof(buf), "Dashboard: %s", dashboard ? dashboard : "(default)");
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_font(label, font_manrope_600_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *hint = lv_label_create(container);
    lv_label_set_text(hint, "Setup complete.\nFull dashboard rendering coming soon.");
    lv_obj_set_style_text_font(hint, font_manrope_500_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x999999), LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

#endif /* ESP_PLATFORM */
