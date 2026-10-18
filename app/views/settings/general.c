/**
 * @file general.c
 * @brief General settings tab implementation
 *
 * Provides settings for:
 * - Device name
 * - Language selection (German, English)
 * - Tile rows on one screen
 * - Enable Microphone
 * - Enable haptic feedback
 * - Enable mm-wave sensor
 * - System information
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "general.h"
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
static lv_obj_t *microphone_switch = NULL;
static lv_obj_t *haptic_switch = NULL;
static lv_obj_t *mmwave_switch = NULL;

// Forward declarations
static void device_name_changed_cb(lv_event_t *e);
static void language_changed_cb(lv_event_t *e);
static void microphone_changed_cb(lv_event_t *e);
static void haptic_changed_cb(lv_event_t *e);
static void mmwave_changed_cb(lv_event_t *e);
static void show_system_info_cb(lv_event_t *e);

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

/* Tiles on one screen (config key dashboard_rows): fixed, or relative to
 * the view's columns (DashboardLayout.h rows_on_screen()). */
static const char *const ROWS_IDS[] = {"1", "2", "3", "4", "cols-1", "cols"};
#define ROWS_COUNT (sizeof(ROWS_IDS) / sizeof(ROWS_IDS[0]))

static void rows_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (sel >= ROWS_COUNT) return;
    updateConfig("dashboard_rows", ROWS_IDS[sel]);
    saveConfig();
    settings_reload_dashboard_on_close();
}

static lv_obj_t *microphone_hint = NULL;

static void microphone_hint_delete_cb(lv_event_t *e)
{
    (void)e;
    microphone_hint = NULL;
}

static void microphone_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    // Through the dependency rules: features that need the microphone
    // (the voice assistant) are switched off with it.
    char side[64];
    app_cpp_setting_set("microphone_enabled", enabled, side, sizeof(side));
    voice_overlay_apply_settings();
    if (microphone_hint) {
        const bool voice_off = !enabled && strstr(side, "voice_enabled");
        lv_label_set_text(microphone_hint, voice_off ? tr(TR_SETTINGS_GENERAL_VOICE_OFF_TOO) : "");
        if (voice_off) lv_obj_remove_flag(lv_obj_get_parent(microphone_hint), LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lv_obj_get_parent(microphone_hint), LV_OBJ_FLAG_HIDDEN);
    }
    printf("[Settings] Microphone %s\n", enabled ? "enabled" : "disabled");
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

static void close_msgbox_cb(lv_event_t *e)
{
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    lv_msgbox_close(msgbox);
}

static void show_system_info_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    app_cpp_update_status_t st;
    app_cpp_update_get_status(&st);
    const char *device = getConfig("device_name");
    char lvgl_version[16], build[32];
    snprintf(lvgl_version, sizeof(lvgl_version), "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    snprintf(build, sizeof(build), "%s %s", __DATE__, __TIME__);
    const char *args[] = {device && device[0] ? device : "Homepane",
                          st.installed_version[0] ? st.installed_version : tr(TR_SETTINGS_UNKNOWN), lvgl_version, build};
    char info_text[512];
    tr_format(info_text, sizeof(info_text), TR_SETTINGS_GENERAL_SYSINFO, args, 4);

    lv_obj_t *msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(msgbox, tr(TR_SETTINGS_GENERAL_SYSTEM_INFO));
    lv_msgbox_add_text(msgbox, info_text);
    lv_msgbox_add_close_button(msgbox);
    lv_obj_add_event_cb(msgbox, close_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(msgbox);
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
                lv_dropdown_set_selected(language_dropdown, i, LV_ANIM_OFF);
                break;
            }
        }
    }
    lv_obj_add_event_cb(language_dropdown, language_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // === Tile rows on one screen ===
    lv_obj_t *rows_cont = settings_row_create(page);
    lv_obj_t *rows_icon = lv_label_create(rows_cont);
    lv_label_set_text(rows_icon, LI_LAYOUT_DASHBOARD);
    lv_obj_set_style_text_font(rows_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(rows_icon, lv_color_hex(UI_TEXT), 0);
    lv_obj_t *rows_label = lv_label_create(rows_cont);
    lv_label_set_text(rows_label, tr(TR_SETTINGS_GENERAL_TILE_ROWS));
    lv_obj_set_style_text_font(rows_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(rows_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(rows_label, 1);
    lv_obj_t *rows_dd = lv_dropdown_create(rows_cont);
    char rows_opts[160];
    snprintf(rows_opts, sizeof(rows_opts), "1\n2\n3\n4\n%s\n%s", tr(TR_SETTINGS_GENERAL_ROWS_COLS_MINUS_1),
             tr(TR_SETTINGS_GENERAL_ROWS_COLS));
    lv_dropdown_set_options(rows_dd, rows_opts);
    lv_obj_set_width(rows_dd, 200);
    lv_obj_set_style_text_font(rows_dd, font_manrope_500_18, 0);
    const char *rows_val = getConfig("dashboard_rows");
    lv_dropdown_set_selected(rows_dd, 1, LV_ANIM_OFF); /* default: 2 */
    for (uint32_t i = 0; rows_val && i < ROWS_COUNT; i++) {
        if (strcmp(rows_val, ROWS_IDS[i]) == 0) lv_dropdown_set_selected(rows_dd, i, LV_ANIM_OFF);
    }
    lv_obj_add_event_cb(rows_dd, rows_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // === Enable Microphone ===
    lv_obj_t *mic_cont = settings_row_create(page);

    lv_obj_t *mic_icon = lv_label_create(mic_cont);
    lv_label_set_text(mic_icon, LI_MIC); // microphone icon
    lv_obj_set_style_text_font(mic_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(mic_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *mic_label = lv_label_create(mic_cont);
    lv_label_set_text(mic_label, tr(TR_SETTINGS_GENERAL_MICROPHONE));
    lv_obj_set_style_text_font(mic_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(mic_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(mic_label, 1);

    microphone_switch = lv_switch_create(mic_cont);
    const char *mic_val = getConfig("microphone_enabled");
    if (mic_val && strcmp(mic_val, "1") == 0) {
        lv_obj_add_state(microphone_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(microphone_switch, microphone_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *mic_hint_row = settings_row_create(page);
    microphone_hint = lv_label_create(mic_hint_row);
    lv_obj_set_style_text_font(microphone_hint, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(microphone_hint, lv_color_hex(UI_TEXT2), 0);
    lv_label_set_text(microphone_hint, "");
    lv_obj_add_event_cb(microphone_hint, microphone_hint_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_flag(mic_hint_row, LV_OBJ_FLAG_HIDDEN);

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

    // === System Information ===
    lv_obj_t *sysinfo_cont = settings_row_create(page);
    lv_obj_add_flag(sysinfo_cont, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *sysinfo_icon = lv_label_create(sysinfo_cont);
    lv_label_set_text(sysinfo_icon, LI_INFO); // info-circle icon
    lv_obj_set_style_text_font(sysinfo_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(sysinfo_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *sysinfo_label = lv_label_create(sysinfo_cont);
    lv_label_set_text(sysinfo_label, tr(TR_SETTINGS_GENERAL_SYSTEM_INFO));
    lv_obj_set_style_text_font(sysinfo_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(sysinfo_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(sysinfo_label, 1);

    lv_obj_t *sysinfo_arrow = lv_label_create(sysinfo_cont);
    lv_label_set_text(sysinfo_arrow, LI_CHEVRON_RIGHT); // chevron-right
    lv_obj_set_style_text_font(sysinfo_arrow, UI_ICON_S, 0);
    lv_obj_set_style_text_color(sysinfo_arrow, lv_color_hex(UI_TEXT2), 0);

    lv_obj_add_event_cb(sysinfo_cont, show_system_info_cb, LV_EVENT_CLICKED, NULL);

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
