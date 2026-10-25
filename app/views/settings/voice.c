/**
 * @file voice.c
 * @brief Voice assistant settings tab
 *
 * One switch for the assistant and the microphone it needs, then the wake
 * word, which Home Assistant assistant (pipeline), its look, its sounds
 * and their volume.
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "voice.h"
#include "settings.h"
#include "settings_tab.h"
#include "../../config.h"
#include "../../fonts.h"
#include "../../cpp/app_facade.h"
#include "../voice_overlay.h"

static lv_obj_t *s_voice_switch = NULL;
static lv_obj_t *s_wake_dd = NULL;
static lv_obj_t *s_pipeline_dd = NULL;
static lv_obj_t *s_voice_hint = NULL;
static lv_obj_t *s_ui_dd = NULL;
static lv_obj_t *s_sound_dd = NULL;
static lv_obj_t *s_volume = NULL;

static const char *const SOUND_IDS[] = {"chime", "shimmer", "blip", "switch", "off"};
#define SOUND_COUNT 5

#define HINT_COLOR 0xA39F97
#define WARN_COLOR 0xD98A73

static const char *const WAKE_IDS[] = {"okay_nabu", "hey_jarvis", "none"};
#define WAKE_COUNT 3

static bool config_on(const char *key)
{
    const char *v = getConfig(key);
    return v && strcmp(v, "1") == 0;
}

static void set_hint(const char *text, uint32_t color)
{
    if (!s_voice_hint) return;
    lv_label_set_text(s_voice_hint, text);
    lv_obj_set_style_text_color(s_voice_hint, lv_color_hex(color), 0);
    if (text[0]) lv_obj_remove_flag(lv_obj_get_parent(s_voice_hint), LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lv_obj_get_parent(s_voice_hint), LV_OBJ_FLAG_HIDDEN);
}

/* Wake word and assistant only make sense while the assistant is on. */
static void update_voice_rows(void)
{
    const bool on = config_on("voice_enabled");
    lv_obj_t *dds[] = {s_wake_dd, s_pipeline_dd, s_ui_dd, s_sound_dd, s_volume};
    for (int i = 0; i < 5; ++i) {
        if (!dds[i]) continue;
        if (on) lv_obj_remove_state(dds[i], LV_STATE_DISABLED);
        else lv_obj_add_state(dds[i], LV_STATE_DISABLED);
    }
}

static void show_pipeline_state(void)
{
    switch (app_cpp_voice_pipelines_state()) {
    case APP_CPP_VOICE_PIPELINES_NONE:
        set_hint(tr(TR_SETTINGS_HA_NO_PIPELINE), WARN_COLOR);
        break;
    case APP_CPP_VOICE_PIPELINES_UNAVAILABLE:
        set_hint(tr(TR_SETTINGS_HA_VOICE_NOT_CONNECTED), WARN_COLOR);
        break;
    default: break;
    }
}

static void pipelines_loaded_cb(void *user)
{
    (void)user;
    if (!s_pipeline_dd) return; /* page closed meanwhile */
    const int n = app_cpp_voice_pipeline_count();
    char opts[512];
    int pos = 0;
    const int pref = app_cpp_voice_pipeline_preferred();
    /* Index 0 = HA's default (follows changes made in HA); then each assistant. */
    if (pref >= 0) pos += snprintf(opts + pos, sizeof(opts) - pos, "Default (%s)", app_cpp_voice_pipeline_name(pref));
    else pos += snprintf(opts + pos, sizeof(opts) - pos, "Default");
    for (int i = 0; i < n && pos < (int)sizeof(opts) - 2; ++i) {
        pos += snprintf(opts + pos, sizeof(opts) - pos, "\n%s", app_cpp_voice_pipeline_name(i));
    }
    lv_dropdown_set_options(s_pipeline_dd, opts);
    const char *current = getConfig("voice_pipeline");
    uint32_t sel = 0;
    for (int i = 0; current && current[0] && i < n; ++i) {
        if (strcmp(current, app_cpp_voice_pipeline_id(i)) == 0) sel = (uint32_t)(i + 1);
    }
    lv_dropdown_set_selected(s_pipeline_dd, sel);
    show_pipeline_state();
    update_voice_rows();
    if (app_cpp_voice_pipelines_state() != APP_CPP_VOICE_PIPELINES_OK) lv_obj_add_state(s_pipeline_dd, LV_STATE_DISABLED);
}

static void voice_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    /* one switch: the microphone goes with the assistant (on: first the
     * microphone, off: first the assistant; SettingRules ties them) */
    char side[64];
    app_cpp_setting_set(on ? "microphone_enabled" : "voice_enabled", on, side, sizeof(side));
    app_cpp_setting_set(on ? "voice_enabled" : "microphone_enabled", on, side, sizeof(side));
    voice_overlay_apply_settings();
    update_voice_rows();
    set_hint("", HINT_COLOR);
    if (on) show_pipeline_state();
}

static void wake_changed_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (sel >= WAKE_COUNT) return;
    updateConfig("voice_wake_word", WAKE_IDS[sel]);
    saveConfig();
    voice_overlay_apply_settings();
}

static void pipeline_changed_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    /* 0 = HA's default: store empty so a changed default in HA is followed. */
    updateConfig("voice_pipeline", sel == 0 ? "" : app_cpp_voice_pipeline_id((int)sel - 1));
    saveConfig();
}

static void ui_changed_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    updateConfig("voice_ui", sel == 1 ? "b" : "a");
    saveConfig();
}

static void sound_changed_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (sel >= SOUND_COUNT) return;
    updateConfig("voice_sound", SOUND_IDS[sel]);
    saveConfig();
    voice_overlay_apply_settings();
    app_cpp_voice_preview_sound(SOUND_IDS[sel]); /* let the user hear it */
}

static void volume_changed_cb(lv_event_t *e)
{
    char v[8];
    snprintf(v, sizeof(v), "%d", (int)lv_slider_get_value(lv_event_get_target(e)));
    updateConfig("voice_volume", v);
    saveConfig();
    voice_overlay_apply_settings();
    const char *sound = getConfig("voice_sound");
    app_cpp_voice_preview_sound(sound && sound[0] ? sound : "chime"); /* hear the new level */
}

static void page_delete_cb(lv_event_t *e)
{
    (void)e;
    s_voice_switch = s_wake_dd = s_pipeline_dd = s_voice_hint = s_ui_dd = s_sound_dd = s_volume = NULL;
}

static void create_voice_section(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, page_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *row = settings_icon_row(page, LI_MIC, tr(TR_SETTINGS_HA_VOICE));
    s_voice_switch = lv_switch_create(row);
    if (config_on("voice_enabled") && config_on("microphone_enabled")) lv_obj_add_state(s_voice_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_voice_switch, voice_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *hint_row = settings_row_create(page);
    s_voice_hint = lv_label_create(hint_row);
    lv_obj_set_width(s_voice_hint, LV_PCT(100));
    lv_label_set_long_mode(s_voice_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_voice_hint, font_manrope_500_18, 0);
    set_hint("", HINT_COLOR);

    row = settings_icon_row(page, LI_MESSAGE_CIRCLE, tr(TR_SETTINGS_HA_WAKE_WORD));
    s_wake_dd = settings_dropdown(row, tr(TR_SETTINGS_HA_WAKE_OPTIONS));
    const char *wake = getConfig("voice_wake_word");
    for (uint32_t i = 0; wake && i < WAKE_COUNT; ++i) {
        if (strcmp(wake, WAKE_IDS[i]) == 0) lv_dropdown_set_selected(s_wake_dd, i);
    }
    lv_obj_add_event_cb(s_wake_dd, wake_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_icon_row(page, LI_BOT, tr(TR_SETTINGS_HA_ASSISTANT));
    s_pipeline_dd = settings_dropdown(row, tr(TR_SETTINGS_LOADING));
    lv_obj_add_state(s_pipeline_dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_pipeline_dd, pipeline_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Look of the assistant overlay (design guide variants A and B). */
    row = settings_icon_row(page, LI_SLIDERS_HORIZONTAL, tr(TR_SETTINGS_HA_VOICE_UI));
    s_ui_dd = settings_dropdown(row, tr(TR_SETTINGS_HA_UI_OPTIONS));
    const char *ui = getConfig("voice_ui");
    if (ui && strcmp(ui, "b") == 0) lv_dropdown_set_selected(s_ui_dd, 1);
    lv_obj_add_event_cb(s_ui_dd, ui_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Feedback sounds (synthesized, see app/cpp/voice/earcons.c). */
    row = settings_icon_row(page, LI_MIC, tr(TR_SETTINGS_HA_SOUND));
    s_sound_dd = settings_dropdown(row, tr(TR_SETTINGS_HA_SOUND_OPTIONS));
    const char *sound = getConfig("voice_sound");
    for (uint32_t i = 0; sound && i < SOUND_COUNT; ++i) {
        if (strcmp(sound, SOUND_IDS[i]) == 0) lv_dropdown_set_selected(s_sound_dd, i);
    }
    lv_obj_add_event_cb(s_sound_dd, sound_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_icon_row(page, LI_SLIDERS_HORIZONTAL, tr(TR_SETTINGS_HA_VOLUME));
    s_volume = lv_slider_create(row);
    lv_obj_set_width(s_volume, 240);
    lv_slider_set_range(s_volume, 30, 100);
    const char *vol = getConfig("voice_volume");
    lv_slider_set_value(s_volume, vol && vol[0] ? atoi(vol) : 90, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_volume, volume_changed_cb, LV_EVENT_RELEASED, NULL);

    update_voice_rows();
    app_cpp_voice_fetch_pipelines(pipelines_loaded_cb, NULL);
}

static lv_obj_t *create_voice_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);
    create_voice_section(page);
    return page;
}

static const settings_tab_t voice_tab = {
    .name = "Voice assistant",
    .create_page = create_voice_page,
    .name_key = TR_SETTINGS_VOICE,
    .summary_key = TR_SETTINGS_VOICE_SUMMARY,
    .line_icon = LI_MIC,
};

void settings_voice_init(void)
{
    settings_register_tab(&voice_tab);
}
