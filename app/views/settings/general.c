/**
 * @file general.c
 * @brief General settings tab implementation
 *
 * Provides settings for:
 * - Device name, room (the HA area)
 * - Language, time and date format
 * - Hardware: haptic feedback (greyed out without the BOS1921), presence
 *   sensor (greyed out without a radar)
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "general.h"
#include "homeassistant.h"
#include "clock.h"
#include "settings.h"
#include "settings_tab.h"
#include "../../config.h"
#include "../voice_overlay.h"
#include "../views.h"
#include "../../cpp/app_facade.h"
#include "../../fonts.h"

// Available languages
/* Languages: one file each in app/i18n (i18n_languages, generated). */

// UI elements
static lv_obj_t *device_name_textarea = NULL;
static lv_obj_t *language_dropdown = NULL;
static lv_obj_t *haptic_switch = NULL;
static lv_obj_t *mmwave_switch = NULL;

// Forward declarations
static void device_name_changed_cb(lv_event_t *e);
static void language_changed_cb(lv_event_t *e);
static void haptic_changed_cb(lv_event_t *e);
static void mmwave_changed_cb(lv_event_t *e);

static void device_name_changed_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    const char *name = lv_textarea_get_text(ta);
    if (name && strlen(name) > 0) {
        updateConfig("device_name", name);
        saveConfig();
        printf("[Settings] Device name changed to: %s\n", name);
    }
}

static void reload_ui_cb(void *arg)
{
    (void)arg;
    settings_close();
    dashboard_render(lv_screen_active());
    settings_open(lv_screen_active());
}

static void language_changed_cb(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    uint32_t sel = lv_dropdown_get_selected(dropdown);

    if (sel < (uint32_t)i18n_language_count) {
        const char *code = i18n_languages[sel].code;
        if (strcmp(code, i18n_language()) == 0) return;
        updateConfig("language", code);
        saveConfig();
        i18n_set_language(code);
        printf("[Settings] Language changed to: %s\n", code);
        /* Rebuild the UI in the new language (after this event: it deletes
         * the dropdown that is sending it). */
        lv_async_call(reload_ui_cb, NULL);
    }
}

static void haptic_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    updateConfig("haptic_enabled", enabled ? "1" : "0");
    saveConfig();
    printf("[Settings] Haptic feedback %s\n", enabled ? "enabled" : "disabled");
}

static void mmwave_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    updateConfig("mmwave_enabled", enabled ? "1" : "0");
    saveConfig();
    printf("[Settings] MM-Wave sensor %s\n", enabled ? "enabled" : "disabled");
}

/* A row whose hardware was not found at boot (docs/plans/
 * HARDWARE_CAPABILITIES.md): stays visible, greyed out, with "Not
 * detected" before the disabled switch. */
static void hw_row_missing(lv_obj_t *row, lv_obj_t *icon, lv_obj_t *label, lv_obj_t *sw)
{
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_TEXT3), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT3), 0);
    lv_obj_t *hint = lv_label_create(row);
    lv_label_set_text(hint, tr(TR_SETTINGS_GENERAL_NOT_DETECTED));
    lv_obj_set_style_text_font(hint, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT3), 0);
    lv_obj_move_to_index(hint, lv_obj_get_index(sw));
    /* shown off and darker than an enabled "off"; the saved setting stays.
     * Not clickable rather than LV_STATE_DISABLED: the default theme lightens
     * disabled widgets. USER_2: settings.c styles it greyed out. */
    lv_obj_remove_state(sw, LV_STATE_CHECKED);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_USER_2);
}

static lv_obj_t* create_general_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);

    // === Device Name ===
    lv_obj_t *name_cont = settings_row_create(page);

    lv_obj_t *name_icon = lv_label_create(name_cont);
    lv_label_set_text(name_icon, LI_ID_CARD); // id-card icon
    lv_obj_set_style_text_font(name_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(name_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *name_label = lv_label_create(name_cont);
    lv_label_set_text(name_label, tr(TR_SETTINGS_GENERAL_DEVICE_NAME));
    lv_obj_set_style_text_font(name_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(name_label, 1);

    device_name_textarea = lv_textarea_create(name_cont);
    lv_textarea_set_one_line(device_name_textarea, true);
    lv_textarea_set_placeholder_text(device_name_textarea, tr(TR_SETTINGS_GENERAL_ENTER_NAME));
    lv_obj_set_width(device_name_textarea, 180);
    lv_obj_set_style_text_font(device_name_textarea, font_manrope_500_18, 0);

    const char *current_name = getConfig("device_name");
    if (current_name) {
        lv_textarea_set_text(device_name_textarea, current_name);
    }
    lv_obj_add_event_cb(device_name_textarea, device_name_changed_cb, LV_EVENT_DEFOCUSED, NULL);
    settings_attach_keyboard(device_name_textarea);

    // === Room (the panel's HA area: the voice assistant, the header) ===
    settings_room_rows(page);

    // === Language ===
    lv_obj_t *lang_cont = settings_row_create(page);

    lv_obj_t *lang_icon = lv_label_create(lang_cont);
    lv_label_set_text(lang_icon, LI_GLOBE); // globe icon
    lv_obj_set_style_text_font(lang_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(lang_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *lang_label = lv_label_create(lang_cont);
    lv_label_set_text(lang_label, tr(TR_SETTINGS_GENERAL_LANGUAGE));
    lv_obj_set_style_text_font(lang_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(lang_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(lang_label, 1);

    language_dropdown = lv_dropdown_create(lang_cont);
    char options[256] = "";
    for (int i = 0; i < i18n_language_count; i++) {
        if (i) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
        strncat(options, i18n_languages[i].name, sizeof(options) - strlen(options) - 1);
    }
    lv_dropdown_set_options(language_dropdown, options);
    lv_obj_set_width(language_dropdown, 150);
    lv_obj_set_style_text_font(language_dropdown, font_manrope_500_18, 0);

    const char *current_lang = getConfig("language");
    if (current_lang) {
        for (int i = 0; i < i18n_language_count; i++) {
            if (strcmp(current_lang, i18n_languages[i].code) == 0) {
                lv_dropdown_set_selected(language_dropdown, i);
                break;
            }
        }
    }
    lv_obj_add_event_cb(language_dropdown, language_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // === Time and date format ===
    settings_clock_rows(page);

    // === Hardware: greyed out until the part is detected ===
    // === Enable Haptic Feedback ===
    lv_obj_t *haptic_cont = settings_row_create(page);

    lv_obj_t *haptic_icon = lv_label_create(haptic_cont);
    lv_label_set_text(haptic_icon, LI_HAND); // hand-point-right icon
    lv_obj_set_style_text_font(haptic_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(haptic_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *haptic_label = lv_label_create(haptic_cont);
    lv_label_set_text(haptic_label, tr(TR_SETTINGS_GENERAL_HAPTIC));
    lv_obj_set_style_text_font(haptic_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(haptic_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(haptic_label, 1);

    haptic_switch = lv_switch_create(haptic_cont);
    const char *haptic_val = getConfig("haptic_enabled");
    if (haptic_val && strcmp(haptic_val, "1") == 0) {
        lv_obj_add_state(haptic_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(haptic_switch, haptic_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (!app_cpp_hw_has(APP_CPP_HW_HAPTICS)) hw_row_missing(haptic_cont, haptic_icon, haptic_label, haptic_switch);

    // === Enable MM-Wave Sensor ===
    lv_obj_t *mmwave_cont = settings_row_create(page);

    lv_obj_t *mmwave_icon = lv_label_create(mmwave_cont);
    lv_label_set_text(mmwave_icon, LI_RADAR); // signal icon
    lv_obj_set_style_text_font(mmwave_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(mmwave_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *mmwave_label = lv_label_create(mmwave_cont);
    lv_label_set_text(mmwave_label, tr(TR_SETTINGS_GENERAL_MMWAVE));
    lv_obj_set_style_text_font(mmwave_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(mmwave_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(mmwave_label, 1);

    mmwave_switch = lv_switch_create(mmwave_cont);
    const char *mmwave_val = getConfig("mmwave_enabled");
    if (mmwave_val && strcmp(mmwave_val, "1") == 0) {
        lv_obj_add_state(mmwave_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(mmwave_switch, mmwave_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (!app_cpp_hw_has(APP_CPP_HW_PRESENCE)) hw_row_missing(mmwave_cont, mmwave_icon, mmwave_label, mmwave_switch);

    return page;
}

static const settings_tab_t general_tab = {
    .name = "General",
    .create_page = create_general_page,
    .name_key = TR_SETTINGS_GENERAL,
    .summary_key = TR_SETTINGS_GENERAL_SUMMARY,
    .line_icon = LI_SLIDERS_HORIZONTAL,
};

void settings_general_init(void)
{
    settings_register_tab(&general_tab);
}

const settings_tab_t* settings_general_get_tab(void)
{
    return &general_tab;
}
