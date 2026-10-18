/**
 * @file clock.c
 * @brief Clock settings tab implementation
 *
 * Provides settings for:
 * - Time format (12h / 24h)
 * - Date format: European, American or Asian order (the language gives
 *   the words; app/cpp/core/DateText)
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "clock.h"
#include "settings_tab.h"
#include "settings.h"
#include "../../config.h"
#include "../../fonts.h"
#include "../../cpp/app_facade.h"

// Date format options (TR_SETTINGS_CLOCK_DATE_ORDERS order)
static const char *date_format_codes[] = {"eu", "us", "iso"};
static const int date_format_count = 3;

// UI elements
static lv_obj_t *time_format_switch = NULL;
static lv_obj_t *date_format_dropdown = NULL;
static lv_obj_t *example_label = NULL;

/* applies the format everywhere and shows an example */
static void apply_format(void)
{
    app_cpp_datetime_apply_settings();
    if (!example_label) return;
    char now[96];
    app_cpp_datetime_example(now, sizeof(now));
    const char *args[] = {now};
    char text[128];
    lv_label_set_text(example_label, tr_format(text, sizeof(text), TR_SETTINGS_CLOCK_EXAMPLE, args, 1));
}

static void example_deleted_cb(lv_event_t *e)
{
    (void)e;
    example_label = NULL;
}

static void time_format_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool is_24h = lv_obj_has_state(sw, LV_STATE_CHECKED);

    updateConfig("time_format", is_24h ? "24h" : "12h");
    saveConfig();
    apply_format();
    printf("[Settings] Time format changed to: %s\n", is_24h ? "24h" : "12h");
}

static void date_format_changed_cb(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    uint32_t sel = lv_dropdown_get_selected(dropdown);

    if (sel < (uint32_t)date_format_count) {
        updateConfig("date_format", date_format_codes[sel]);
        saveConfig();
        apply_format();
        printf("[Settings] Date format changed to: %s\n", date_format_codes[sel]);
    }
}

static lv_obj_t* create_clock_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);

    // === Time format setting ===
    lv_obj_t *time_cont = settings_row_create(page);

    lv_obj_t *time_icon = lv_label_create(time_cont);
    lv_label_set_text(time_icon, LI_CLOCK); // clock icon
    lv_obj_set_style_text_font(time_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(time_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *time_label = lv_label_create(time_cont);
    lv_label_set_text(time_label, tr(TR_SETTINGS_CLOCK_24H));
    lv_obj_set_style_text_font(time_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(time_label, 1);

    time_format_switch = lv_switch_create(time_cont);

    const char *time_fmt = getConfig("time_format");
    if (time_fmt && strcmp(time_fmt, "24h") == 0) {
        lv_obj_add_state(time_format_switch, LV_STATE_CHECKED);
    } else if (!time_fmt) {
        // Default to 24h
        lv_obj_add_state(time_format_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(time_format_switch, time_format_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // === Date format setting ===
    lv_obj_t *date_cont = settings_row_create(page);

    lv_obj_t *date_icon = lv_label_create(date_cont);
    lv_label_set_text(date_icon, LI_CALENDAR); // calendar icon
    lv_obj_set_style_text_font(date_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(date_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *date_label = lv_label_create(date_cont);
    lv_label_set_text(date_label, tr(TR_SETTINGS_CLOCK_DATE_FORMAT));
    lv_obj_set_style_text_font(date_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(date_label, 1);

    date_format_dropdown = lv_dropdown_create(date_cont);
    lv_dropdown_set_options(date_format_dropdown, tr(TR_SETTINGS_CLOCK_DATE_ORDERS));
    lv_obj_set_width(date_format_dropdown, 200);
    lv_obj_set_style_text_font(date_format_dropdown, font_manrope_500_18, 0);

    const char *date_fmt = getConfig("date_format");
    if (date_fmt) {
        for (int i = 0; i < date_format_count; i++) {
            if (strcmp(date_fmt, date_format_codes[i]) == 0) {
                lv_dropdown_set_selected(date_format_dropdown, i, LV_ANIM_OFF);
                break;
            }
        }
    }
    lv_obj_add_event_cb(date_format_dropdown, date_format_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // === Example of the chosen format ===
    lv_obj_t *info_cont = settings_row_create(page);

    example_label = lv_label_create(info_cont);
    lv_obj_set_style_text_font(example_label, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(example_label, lv_color_hex(UI_TEXT2), 0);
    lv_obj_set_flex_grow(example_label, 1);
    lv_label_set_long_mode(example_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(example_label, example_deleted_cb, LV_EVENT_DELETE, NULL);
    apply_format();

    return page;
}

static const settings_tab_t clock_tab = {
    .name = "Clock & Date",
    .create_page = create_clock_page,
    .name_key = TR_SETTINGS_CLOCK,
    .summary_key = TR_SETTINGS_CLOCK_SUMMARY,
    .line_icon = LI_CLOCK,
};

void settings_clock_init(void)
{
    settings_register_tab(&clock_tab);
}

const settings_tab_t* settings_clock_get_tab(void)
{
    return &clock_tab;
}
