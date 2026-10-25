/**
 * @file homeassistant.c
 * @brief Home Assistant settings tab implementation
 *
 * Two tabs:
 * - Home Assistant: URL, connection state, the panel as an MQTT device
 * - Dashboard: which one, single or several, tile rows, how changes
 *   from HA's editor are applied
 * and the room rows (the HA area) for Settings > General.
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

// UI elements
static lv_obj_t *url_textarea = NULL;

#define HINT_COLOR 0xA39F97
#define WARN_COLOR 0xD98A73

static bool config_on(const char *key)
{
    const char *v = getConfig(key);
    return v && strcmp(v, "1") == 0;
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
    lv_obj_t *row = settings_icon_row(page, icon, label);
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
    lv_obj_t *row = settings_icon_row(page, LI_LINK, tr(TR_SETTINGS_HA_MQTT));
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
    lv_dropdown_set_selected(s_room_dd, sel);
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

void settings_room_rows(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, room_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *row = settings_icon_row(page, LI_HOUSE, tr(TR_SETTINGS_HA_ROOM));
    s_room_dd = settings_dropdown(row, tr(TR_SETTINGS_LOADING));
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
    lv_dropdown_set_selected(s_dashboard_dd, sel);
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

/* Changes in HA's dashboard editor: at once, or once editing ended
 * (dashboard_updates; the facade turns it into the controller's delay). */
static void updates_changed_cb(lv_event_t *e)
{
    const uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    updateConfig("dashboard_updates", sel == 0 ? "live" : "settled");
    saveConfig();
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

static void dashboard_delete_cb(lv_event_t *e)
{
    (void)e;
    s_dashboard_dd = NULL;
}

static void create_dashboard_section(lv_obj_t *page)
{
    lv_obj_add_event_cb(page, dashboard_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *row = settings_icon_row(page, LI_LAYOUT_DASHBOARD, tr(TR_SETTINGS_HA_DASHBOARD));
    s_dashboard_dd = settings_dropdown(row, tr(TR_SETTINGS_LOADING));
    lv_obj_add_state(s_dashboard_dd, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_dashboard_dd, dashboard_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    app_cpp_dashboards_fetch(dashboards_loaded_cb, NULL);

    row = settings_icon_row(page, LI_LIST, tr(TR_SETTINGS_HA_DASHBOARD_MODE));
    char opts[128];
    snprintf(opts, sizeof(opts), "%s\n%s", tr(TR_DASHBOARD_MODE_SINGLE), tr(TR_DASHBOARD_MODE_MULTI));
    lv_obj_t *dd = settings_dropdown(row, opts);
    const char *mode = getConfig("dashboard_mode");
    lv_dropdown_set_selected(dd, mode && strcmp(mode, "multi") == 0 ? 1 : 0);
    lv_obj_add_event_cb(dd, mode_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_icon_row(page, LI_LAYOUT_GRID, tr(TR_SETTINGS_GENERAL_TILE_ROWS));
    char rows_opts[160];
    snprintf(rows_opts, sizeof(rows_opts), "1\n2\n3\n4\n%s\n%s", tr(TR_SETTINGS_GENERAL_ROWS_COLS_MINUS_1),
             tr(TR_SETTINGS_GENERAL_ROWS_COLS));
    dd = settings_dropdown(row, rows_opts);
    const char *rows_val = getConfig("dashboard_rows");
    lv_dropdown_set_selected(dd, 1); /* default: 2 */
    for (uint32_t i = 0; rows_val && i < ROWS_COUNT; i++) {
        if (strcmp(rows_val, ROWS_IDS[i]) == 0) lv_dropdown_set_selected(dd, i);
    }
    lv_obj_add_event_cb(dd, rows_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_icon_row(page, LI_REFRESH_CW, tr(TR_SETTINGS_HA_DASHBOARD_UPDATES));
    snprintf(opts, sizeof(opts), "%s\n%s", tr(TR_SETTINGS_HA_UPDATES_LIVE), tr(TR_SETTINGS_HA_UPDATES_SETTLED));
    dd = settings_dropdown(row, opts);
    const char *updates = getConfig("dashboard_updates");
    lv_dropdown_set_selected(dd, updates && strcmp(updates, "live") == 0 ? 0 : 1);
    lv_obj_add_event_cb(dd, updates_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *hint = lv_label_create(settings_row_create(page)); /* a row with one label: a hint */
    lv_label_set_text(hint, tr(TR_SETTINGS_HA_UPDATES_HINT));
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

static lv_obj_t *create_dashboard_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);
    create_dashboard_section(page);
    return page;
}

static const settings_tab_t dashboard_tab = {
    .name = "Dashboard",
    .create_page = create_dashboard_page,
    .name_key = TR_SETTINGS_DASHBOARD,
    .summary_key = TR_SETTINGS_DASHBOARD_SUMMARY,
    .line_icon = LI_LAYOUT_DASHBOARD,
};

void settings_dashboard_init(void)
{
    settings_register_tab(&dashboard_tab);
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
