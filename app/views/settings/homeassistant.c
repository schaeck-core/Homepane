/**
 * @file homeassistant.c
 * @brief Home Assistant settings tab implementation
 *
 * Provides settings for:
 * - Home Assistant URL
 * - Reset connection button
 * - Voice assistant: on/off (needs the microphone, see SettingRules),
 *   wake word, which HA assistant (pipeline) to use
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "homeassistant.h"
#include "settings.h"
#include "settings_tab.h"
#include "../../config.h"
#include "../../fonts.h"
#include "../../cpp/app_facade.h"
#include "../voice_overlay.h"

// UI elements
static lv_obj_t *url_textarea = NULL;
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
    lv_dropdown_set_selected(s_pipeline_dd, sel, LV_ANIM_OFF);
    show_pipeline_state();
    update_voice_rows();
    if (app_cpp_voice_pipelines_state() != APP_CPP_VOICE_PIPELINES_OK) lv_obj_add_state(s_pipeline_dd, LV_STATE_DISABLED);
}

static void voice_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    char side[64];
    app_cpp_setting_set("voice_enabled", on, side, sizeof(side));
    voice_overlay_apply_settings();
    update_voice_rows();
    if (on && strstr(side, "microphone_enabled")) {
        set_hint(tr(TR_SETTINGS_HA_MIC_ENABLED_TOO), HINT_COLOR);
    } else {
        set_hint("", HINT_COLOR);
        if (on) show_pipeline_state();
    }
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

static lv_obj_t *voice_row(lv_obj_t *page, const char *icon, const char *text)
{
    lv_obj_t *row = settings_row_create(page);
    lv_obj_t *i = lv_label_create(row);
    lv_label_set_text(i, icon);
    lv_obj_set_style_text_font(i, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(i, lv_color_hex(UI_TEXT), 0);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(l, 1);
    return row;
}

static lv_obj_t *voice_dropdown(lv_obj_t *row, const char *options)
{
    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_width(dd, 280);
    lv_obj_set_style_text_font(dd, font_manrope_500_18, 0);
    return dd;
}

/* ── MQTT: the panel as a Home Assistant device (sensors, diagnostics) ── */

static lv_obj_t *s_mqtt_status = NULL;
static lv_timer_t *s_mqtt_timer = NULL;

static void mqtt_status_update(lv_timer_t *t)
{
    (void)t;
    if (!s_mqtt_status) return;
    const bool on = config_on("mqtt_enabled");
    char buf[96];
    const char *args[] = {tr(!on ? TR_STATE_OFF : app_cpp_mqtt_connected() ? TR_SETTINGS_CONNECTED : TR_SETTINGS_CONNECTING),
                          app_cpp_mqtt_node_id()};
    lv_label_set_text(s_mqtt_status, tr_format(buf, sizeof(buf), TR_SETTINGS_HA_MQTT_STATUS, args, 2));
}

static void mqtt_switch_cb(lv_event_t *e)
{
    const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    updateConfig("mqtt_enabled", on ? "1" : "0");
    saveConfig();
    app_cpp_mqtt_apply_settings();
    mqtt_status_update(NULL);
}

static void mqtt_field_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    const char *key = lv_event_get_user_data(e);
    updateConfig(key, lv_textarea_get_text(ta));
    saveConfig();
    app_cpp_mqtt_apply_settings();
}

static void mqtt_page_delete_cb(lv_event_t *e)
{
    (void)e;
    s_mqtt_status = NULL;
    if (s_mqtt_timer) {
        lv_timer_delete(s_mqtt_timer);
        s_mqtt_timer = NULL;
    }
}

static void mqtt_text_row(lv_obj_t *page, const char *icon, const char *label, const char *key, const char *placeholder,
                          bool password)
{
    lv_obj_t *row = voice_row(page, icon, label);
    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_width(ta, 280);
    lv_obj_set_style_text_font(ta, font_manrope_500_18, 0);
    const char *v = getConfig(key);
    if (v) lv_textarea_set_text(ta, v);
    lv_obj_add_event_cb(ta, mqtt_field_cb, LV_EVENT_DEFOCUSED, (void *)key);
    settings_attach_keyboard(ta);
}

static void create_mqtt_section(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, mqtt_page_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *row = voice_row(page, LI_LINK, tr(TR_SETTINGS_HA_MQTT));
    lv_obj_t *sw = lv_switch_create(row);
    if (config_on("mqtt_enabled")) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, mqtt_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    mqtt_text_row(page, LI_LINK, tr(TR_SETTINGS_HA_BROKER), "mqtt_host", tr(TR_SETTINGS_HA_BROKER_DEFAULT), false);
    mqtt_text_row(page, LI_SLIDERS_HORIZONTAL, tr(TR_SETTINGS_HA_USER), "mqtt_user", "homepane", false);
    mqtt_text_row(page, LI_SLIDERS_HORIZONTAL, tr(TR_SETTINGS_PASSWORD), "mqtt_password", "", true);

    lv_obj_t *status_row = settings_row_create(page);
    s_mqtt_status = lv_label_create(status_row);
    lv_obj_set_style_text_font(s_mqtt_status, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(s_mqtt_status, lv_color_hex(HINT_COLOR), 0);
    mqtt_status_update(NULL);
    s_mqtt_timer = lv_timer_create(mqtt_status_update, 1000, NULL);
}

/* ── Room: the HA area the panel is in ── */

static lv_obj_t *s_room_dd = NULL;
static lv_obj_t *s_room_hint = NULL;
static lv_timer_t *s_room_timer = NULL;

/* The area is set by the panel itself, which needs an HA admin user. */
static void room_hint_update(lv_timer_t *t)
{
    (void)t;
    if (!s_room_hint) return;
    if (app_cpp_room_denied()) {
        lv_label_set_text(s_room_hint, tr(TR_SETTINGS_HA_AREA_DENIED));
        lv_obj_remove_flag(lv_obj_get_parent(s_room_hint), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lv_obj_get_parent(s_room_hint), LV_OBJ_FLAG_HIDDEN);
    }
}

static void rooms_loaded_cb(void *user, bool ok)
{
    (void)user;
    if (!s_room_dd) return;
    char opts[512];
    int pos = snprintf(opts, sizeof(opts), "None");
    const char *current = getConfig("room_area_id");
    uint32_t sel = 0;
    for (int i = 0; i < app_cpp_room_count() && pos < (int)sizeof(opts) - 2; ++i) {
        pos += snprintf(opts + pos, sizeof(opts) - pos, "\n%s", app_cpp_room_name(i));
        if (current && strcmp(current, app_cpp_room_id(i)) == 0) sel = (uint32_t)(i + 1);
    }
    lv_dropdown_set_options(s_room_dd, ok ? opts : tr(TR_SETTINGS_NOT_CONNECTED));
    lv_dropdown_set_selected(s_room_dd, sel, LV_ANIM_OFF);
    if (ok) lv_obj_remove_state(s_room_dd, LV_STATE_DISABLED);
}

static void room_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (sel == 0) app_cpp_room_set("", "");
    else app_cpp_room_set(app_cpp_room_id((int)sel - 1), app_cpp_room_name((int)sel - 1));
}

static void room_delete_cb(lv_event_t *e)
{
    (void)e;
    s_room_dd = s_room_hint = NULL;
    if (s_room_timer) {
        lv_timer_delete(s_room_timer);
        s_room_timer = NULL;
    }
}

static void create_room_section(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, room_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *row = voice_row(page, LI_HOUSE, tr(TR_SETTINGS_HA_ROOM));
    s_room_dd = voice_dropdown(row, tr(TR_SETTINGS_LOADING));
    lv_obj_add_state(s_room_dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_room_dd, room_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    app_cpp_rooms_fetch(rooms_loaded_cb, NULL);

    lv_obj_t *hint_row = settings_row_create(page);
    s_room_hint = lv_label_create(hint_row);
    lv_obj_set_width(s_room_hint, LV_PCT(100));
    lv_label_set_long_mode(s_room_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_room_hint, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(s_room_hint, lv_color_hex(WARN_COLOR), 0);
    room_hint_update(NULL);
    s_room_timer = lv_timer_create(room_hint_update, 1000, NULL);
}

/* Default dashboard and dashboard mode (config keys haas_dashboard,
 * dashboard_mode); the dashboard is rebuilt when the settings close. */
static lv_obj_t *s_dashboard_dd = NULL;

static void dashboards_loaded_cb(void *user, bool ok)
{
    (void)user;
    if (!s_dashboard_dd) return;
    char opts[512] = "";
    const char *current = getConfig("haas_dashboard");
    uint32_t sel = 0;
    for (int i = 0; i < app_cpp_dashboards_count(); ++i) {
        const char *title = app_cpp_dashboards_title(i);
        if (i) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        strncat(opts, title[0] ? title : tr(TR_SETUP_DEFAULT_DASHBOARD), sizeof(opts) - strlen(opts) - 1);
        if (current && strcmp(current, app_cpp_dashboards_path(i)) == 0) sel = (uint32_t)i;
    }
    ok = ok && app_cpp_dashboards_count() > 0;
    lv_dropdown_set_options(s_dashboard_dd, ok ? opts : tr(TR_SETTINGS_NOT_CONNECTED));
    lv_dropdown_set_selected(s_dashboard_dd, sel, LV_ANIM_OFF);
    if (ok) lv_obj_remove_state(s_dashboard_dd, LV_STATE_DISABLED);
}

static void dashboard_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if ((int)sel >= app_cpp_dashboards_count()) return;
    updateConfig("haas_dashboard", app_cpp_dashboards_path((int)sel));
    saveConfig();
    settings_reload_dashboard_on_close();
}

static void mode_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    updateConfig("dashboard_mode", sel == 1 ? "multi" : "single");
    saveConfig();
    settings_reload_dashboard_on_close();
}

static void dashboard_delete_cb(lv_event_t *e)
{
    (void)e;
    s_dashboard_dd = NULL;
}

static void create_dashboard_section(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, dashboard_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *row = voice_row(page, LI_LAYOUT_DASHBOARD, tr(TR_SETTINGS_HA_DASHBOARD));
    s_dashboard_dd = voice_dropdown(row, tr(TR_SETTINGS_LOADING));
    lv_obj_add_state(s_dashboard_dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_dashboard_dd, dashboard_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    app_cpp_dashboards_fetch(dashboards_loaded_cb, NULL);

    row = voice_row(page, LI_LIST, tr(TR_SETTINGS_HA_DASHBOARD_MODE));
    char opts[128];
    snprintf(opts, sizeof(opts), "%s\n%s", tr(TR_DASHBOARD_MODE_SINGLE), tr(TR_DASHBOARD_MODE_MULTI));
    lv_obj_t *dd = voice_dropdown(row, opts);
    const char *mode = getConfig("dashboard_mode");
    lv_dropdown_set_selected(dd, mode && strcmp(mode, "multi") == 0 ? 1 : 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(dd, mode_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_voice_section(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, page_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *row = voice_row(page, LI_MIC, tr(TR_SETTINGS_HA_VOICE));
    s_voice_switch = lv_switch_create(row);
    if (config_on("voice_enabled") && config_on("microphone_enabled")) lv_obj_add_state(s_voice_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_voice_switch, voice_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *hint_row = settings_row_create(page);
    s_voice_hint = lv_label_create(hint_row);
    lv_obj_set_width(s_voice_hint, LV_PCT(100));
    lv_label_set_long_mode(s_voice_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_voice_hint, font_manrope_500_18, 0);
    set_hint("", HINT_COLOR);

    row = voice_row(page, LI_MESSAGE_CIRCLE, tr(TR_SETTINGS_HA_WAKE_WORD));
    s_wake_dd = voice_dropdown(row, tr(TR_SETTINGS_HA_WAKE_OPTIONS));
    const char *wake = getConfig("voice_wake_word");
    for (uint32_t i = 0; wake && i < WAKE_COUNT; ++i) {
        if (strcmp(wake, WAKE_IDS[i]) == 0) lv_dropdown_set_selected(s_wake_dd, i, LV_ANIM_OFF);
    }
    lv_obj_add_event_cb(s_wake_dd, wake_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = voice_row(page, LI_BOT, tr(TR_SETTINGS_HA_ASSISTANT));
    s_pipeline_dd = voice_dropdown(row, tr(TR_SETTINGS_LOADING));
    lv_obj_add_state(s_pipeline_dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_pipeline_dd, pipeline_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Look of the assistant overlay (design guide variants A and B). */
    row = voice_row(page, LI_SLIDERS_HORIZONTAL, tr(TR_SETTINGS_HA_VOICE_UI));
    s_ui_dd = voice_dropdown(row, tr(TR_SETTINGS_HA_UI_OPTIONS));
    const char *ui = getConfig("voice_ui");
    if (ui && strcmp(ui, "b") == 0) lv_dropdown_set_selected(s_ui_dd, 1, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui_dd, ui_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Feedback sounds (synthesized, see app/cpp/voice/earcons.c). */
    row = voice_row(page, LI_MIC, tr(TR_SETTINGS_HA_SOUND));
    s_sound_dd = voice_dropdown(row, tr(TR_SETTINGS_HA_SOUND_OPTIONS));
    const char *sound = getConfig("voice_sound");
    for (uint32_t i = 0; sound && i < SOUND_COUNT; ++i) {
        if (strcmp(sound, SOUND_IDS[i]) == 0) lv_dropdown_set_selected(s_sound_dd, i, LV_ANIM_OFF);
    }
    lv_obj_add_event_cb(s_sound_dd, sound_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = voice_row(page, LI_SLIDERS_HORIZONTAL, tr(TR_SETTINGS_HA_VOLUME));
    s_volume = lv_slider_create(row);
    lv_obj_set_width(s_volume, 240);
    lv_slider_set_range(s_volume, 30, 100);
    const char *vol = getConfig("voice_volume");
    lv_slider_set_value(s_volume, vol && vol[0] ? atoi(vol) : 90, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_volume, volume_changed_cb, LV_EVENT_RELEASED, NULL);

    update_voice_rows();
    app_cpp_voice_fetch_pipelines(pipelines_loaded_cb, NULL);
}

static void url_changed_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    const char *url = lv_textarea_get_text(ta);
    if (url && strlen(url) > 0) {
        updateConfig("haas_url", url);
        saveConfig();
        printf("[Settings] Home Assistant URL changed to: %s\n", url);
    }
}

static void close_msgbox_cb(lv_event_t *e)
{
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    lv_msgbox_close(msgbox);
}

static void reset_connection_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    printf("[Settings] Resetting Home Assistant connection...\n");

    // Stop the live dashboard connection (CPP_MIGRATION_PLAN.md Phase 10
    // tr(TR_SETTINGS_HA_TOKEN) consumer: the legacy closeConnection() only ever touched
    // websocket_client.c's own global connection, which the dashboard
    // stopped using once it cut over to the new C++ stack — calling it
    // here was a silent no-op).
    app_cpp_dashboard_disconnect();

    // Clear stored token — and the dashboard selection: after re-pairing
    // (possibly against a different HA server) the old dashboard path may
    // not exist, and with it still set the app would skip the dashboard
    // selection screen and go straight to loading it.
    updateConfig("haas_token", "");
    updateConfig("haas_dashboard", "");
    saveConfig();

    // Show confirmation
    lv_obj_t *msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(msgbox, tr(TR_SETTINGS_HA_RESET_DONE_TITLE));
    lv_msgbox_add_text(msgbox, tr(TR_SETTINGS_HA_RESET_DONE));
    lv_msgbox_add_close_button(msgbox);
    lv_obj_add_event_cb(msgbox, close_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(msgbox);
}

static lv_obj_t* create_homeassistant_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);

    // === URL Setting ===
    lv_obj_t *url_cont = settings_row_create(page);

    lv_obj_t *url_icon = lv_label_create(url_cont);
    lv_label_set_text(url_icon, LI_LINK); // link icon
    lv_obj_set_style_text_font(url_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(url_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *url_label = lv_label_create(url_cont);
    lv_label_set_text(url_label, tr(TR_SETTINGS_URL));
    lv_obj_set_style_text_font(url_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(url_label, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(url_label, 1);

    url_textarea = lv_textarea_create(url_cont);
    lv_textarea_set_one_line(url_textarea, true);
    lv_textarea_set_placeholder_text(url_textarea, "http://homeassistant.local:8123");
    lv_obj_set_width(url_textarea, 280);
    lv_obj_set_style_text_font(url_textarea, font_manrope_500_18, 0);

    const char *current_url = getConfig("haas_url");
    if (current_url) {
        lv_textarea_set_text(url_textarea, current_url);
    }
    lv_obj_add_event_cb(url_textarea, url_changed_cb, LV_EVENT_DEFOCUSED, NULL);
    settings_attach_keyboard(url_textarea);

    // === Connection Status (info) ===
    lv_obj_t *status_cont = settings_row_create(page);

    lv_obj_t *status_icon = lv_label_create(status_cont);
    lv_label_set_text(status_icon, LI_INFO); // info-circle
    lv_obj_set_style_text_font(status_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(status_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *status_label = lv_label_create(status_cont);

    // Check connection status by checking if token exists
    const char *token = getConfig("haas_token");
    bool connected = (token && strlen(token) > 0);
    if (connected) {
        lv_label_set_text(status_label, tr(TR_SETTINGS_HA_STATUS_CONNECTED));
        lv_obj_set_style_text_color(status_label, lv_color_hex(UI_OK), 0); // green
    } else {
        lv_label_set_text(status_label, tr(TR_SETTINGS_HA_STATUS_DISCONNECTED));
        lv_obj_set_style_text_color(status_label, lv_color_hex(UI_WARN), 0); // red
    }
    lv_obj_set_style_text_font(status_label, font_manrope_600_22, 0);
    lv_obj_set_flex_grow(status_label, 1);

    // === Reset Connection Button ===
    lv_obj_t *reset_cont = settings_row_create(page);
    lv_obj_add_flag(reset_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(reset_cont, lv_color_hex(UI_WARN_BG), 0); // destructive: warning colours
    lv_obj_set_style_bg_opa(reset_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(reset_cont, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);

    lv_obj_t *reset_icon = lv_label_create(reset_cont);
    lv_label_set_text(reset_icon, LI_REFRESH_CW); // sync icon
    lv_obj_set_style_text_font(reset_icon, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(reset_icon, lv_color_hex(UI_TEXT), 0);

    lv_obj_t *reset_label = lv_label_create(reset_cont);
    lv_label_set_text(reset_label, tr(TR_SETTINGS_HA_RESET));
    lv_obj_set_style_text_font(reset_label, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(reset_label, lv_color_hex(UI_WARN), 0);
    lv_obj_set_flex_grow(reset_label, 1);

    lv_obj_add_event_cb(reset_cont, reset_connection_cb, LV_EVENT_CLICKED, NULL);

    // === Info text ===
    lv_obj_t *info_cont = settings_row_create(page);

    lv_obj_t *info_label = lv_label_create(info_cont);
    lv_label_set_text(info_label, tr(TR_SETTINGS_HA_RESET_HINT));
    lv_obj_set_style_text_font(info_label, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(UI_TEXT2), 0);

    // === Dashboard: the default one, single / several ===
    create_dashboard_section(page);

    // === Room (HA area; used by the voice assistant and the header) ===
    create_room_section(page);

    // === Voice assistant ===
    create_voice_section(page);

    // === MQTT device ===
    create_mqtt_section(page);

    return page;
}

/* Overview tile: connection state of the dashboard's HA link. */
static const char *ha_status(bool *ok)
{
    *ok = app_cpp_ha_connected();
    return tr(*ok ? TR_SETTINGS_CONNECTED : TR_SETTINGS_NOT_CONNECTED);
}

static const settings_tab_t homeassistant_tab = {
    .name = "Home Assistant",
    .create_page = create_homeassistant_page,
    .name_key = TR_SETTINGS_HOMEASSISTANT,
    .line_icon = LI_HOUSE,
    .status = ha_status,
};

void settings_homeassistant_init(void)
{
    settings_register_tab(&homeassistant_tab);
}

const settings_tab_t* settings_homeassistant_get_tab(void)
{
    return &homeassistant_tab;
}
