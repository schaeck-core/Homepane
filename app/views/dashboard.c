#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_lvgl_port.h"
#define LVGL_LOCK()   lvgl_port_lock(0)
#define LVGL_UNLOCK() lvgl_port_unlock()
#else
#define LVGL_LOCK()   ((void)0)
#define LVGL_UNLOCK() ((void)0)
#endif
#include "lvgl/lvgl.h"
#include "../addons/addons.h"
#include "../i18n.h"
#include "../fonts.h"
#include "../ui_kit.h"
#include "../config.h"
#include "../utils.h"
#include "../cpp/app_facade.h"
#include "views.h"
#include "boot_screen.h"
#include "settings/settings.h"

lv_obj_t *menu = NULL;
lv_obj_t *menuContent = NULL;

/* ── Header (docs/design/DESIGN_GUIDE.md, Main artboard) ──
 * Left: a title with a chevron and page dots. Config key dashboard_mode:
 * "single" (default) shows the view's title and lists the views; "multi"
 * shows the dashboard's title and lists HA's dashboards (the views are
 * swiped). Right: the panel's room temperature and the settings button. */

static lv_obj_t *s_title;
static lv_obj_t *s_dots;
static lv_obj_t *s_temp;
static lv_obj_t *s_hum_icon, *s_hum;
static lv_obj_t *s_mic;
static lv_timer_t *s_header_timer;
static lv_obj_t *s_picker; /* the views' / dashboards' list while open */
static int s_shown_view = -1, s_shown_views = -1;

/* Another dashboard picked in the header: shown until the screensaver or a
 * restart, then the default one (haas_dashboard) again. "" = the default. */
static char s_active[128];

static bool multi_mode(void)
{
    const char *m = getConfig("dashboard_mode");
    return m && strcmp(m, "multi") == 0;
}

static const char *current_dashboard(void)
{
    if (s_active[0]) return s_active;
    const char *d = getConfig("haas_dashboard");
    return d ? d : "";
}

static void rebuild_cb(void *arg)
{
    (void)arg;
    dashboard_render(lv_screen_active());
}

void dashboard_show_default(void)
{
    s_active[0] = '\0';
    lv_async_call(rebuild_cb, NULL);
}

/* The screensaver comes after the time without touch: under it the panel
 * goes back to its start (the default dashboard's first view, scrolled up;
 * lists, settings and detail pages closed). */
static void go_home_cb(void)
{
    if (s_picker) lv_obj_delete(s_picker);
    if (settings_is_open()) settings_close();
    ui_detail_close_all();
    if (s_active[0]) dashboard_show_default(); /* rebuilt at its first view */
    else app_cpp_dashboard_home();
}

static bool config_on(const char *key)
{
    const char *v = getConfig(key);
    return v && strcmp(v, "1") == 0;
}

static void mic_clicked_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_cpp_voice_button();
}

static void settings_icon_clicked_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_open(lv_screen_active());
}

/* The first view shows the room the panel is in (Settings > Home Assistant >
 * Room) when set, the others their HA title. */
static const char *view_title(int index)
{
    const char *room = getConfig("room");
    if (index == 0 && room && room[0] && !multi_mode()) return room;
    const char *t = app_cpp_dashboard_view_title(index);
    if (t && t[0]) return t;
    const char *name = getConfig("device_name");
    return name && name[0] ? name : "Homepane";
}

/* Multi mode: the dashboard's HA title (the view's until the list is in). */
static const char *dashboard_title(void)
{
    const char *path = current_dashboard();
    for (int i = 0; i < app_cpp_dashboards_count(); ++i) {
        if (strcmp(app_cpp_dashboards_path(i), path) != 0) continue;
        const char *t = app_cpp_dashboards_title(i);
        return t[0] ? t : tr(TR_SETUP_DEFAULT_DASHBOARD);
    }
    return view_title(app_cpp_dashboard_view_current());
}

static void header_refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_title) return;
    if (s_active[0] && app_cpp_screensaver_open()) {
        dashboard_show_default(); /* rebuilt under the screensaver */
        return;
    }
    const int count = app_cpp_dashboard_view_count();
    const int current = app_cpp_dashboard_view_current();
    const char *text = multi_mode() ? dashboard_title() : view_title(current);
    if (strcmp(lv_label_get_text(s_title), text) != 0) lv_label_set_text(s_title, text);

    if (count != s_shown_views || current != s_shown_view) {
        s_shown_views = count;
        s_shown_view = current;
        lv_obj_clean(s_dots);
        for (int i = 0; count > 1 && i < count; ++i) {
            lv_obj_t *d = ui_box(s_dots);
            lv_obj_set_size(d, i == current ? 18 : 6, 6);
            lv_obj_set_style_radius(d, 3, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, lv_color_hex(i == current ? UI_TEXT : UI_INACTIVE), 0);
        }
    }

    const bool voice = config_on("voice_enabled") && config_on("microphone_enabled");
    if (voice) lv_obj_remove_flag(s_mic, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_mic, LV_OBJ_FLAG_HIDDEN);

    /* room temperature and humidity: only with the panel's own sensor */
    const float temp = app_cpp_room_temperature();
    lv_obj_t *temp_box = lv_obj_get_parent(s_temp);
    if (temp != temp) {
        if (!lv_obj_has_flag(temp_box, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(temp_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_obj_has_flag(temp_box, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(temp_box, LV_OBJ_FLAG_HIDDEN);
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f°", (double)temp);
    for (char *c = buf; *c; ++c) if (*c == '.') *c = ','; /* German decimal comma */
    if (strcmp(lv_label_get_text(s_temp), buf) != 0) lv_label_set_text(s_temp, buf);

    const float hum = app_cpp_room_humidity();
    const bool show_hum = hum == hum;
    if (show_hum == lv_obj_has_flag(s_hum, LV_OBJ_FLAG_HIDDEN)) {
        if (show_hum) {
            lv_obj_remove_flag(s_hum_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_hum, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_hum_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_hum, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!show_hum) return;
    snprintf(buf, sizeof(buf), "%.0f %%", (double)hum);
    if (strcmp(lv_label_get_text(s_hum), buf) != 0) lv_label_set_text(s_hum, buf);
}

static void header_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_header_timer) lv_timer_delete(s_header_timer);
    s_header_timer = NULL;
    s_title = s_dots = s_temp = s_hum_icon = s_hum = s_mic = NULL;
    s_shown_view = s_shown_views = -1;
}

/* ── View list ("Ansichten") ── */

static void views_close_cb(lv_event_t *e)
{
    lv_obj_delete(lv_event_get_user_data(e));
}

static void view_pick_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_current_target(e)));
    app_cpp_dashboard_view_show((int)(intptr_t)lv_event_get_user_data(e));
    lv_obj_delete_async(overlay);
}

static void reload_cb(lv_event_t *e)
{
    lv_obj_delete_async(lv_event_get_user_data(e));
    app_cpp_dashboard_reload();
    ui_toast(tr(TR_DASHBOARD_RELOADING));
}

static lv_obj_t *head_button(lv_obj_t *head, const char *icon, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_box(head);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(b, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(b, UI_ICON_BUTTON, UI_TEXT, icon));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

/* Full-screen list (views or dashboards): a title, reload and close, then the tiles
 * the caller adds to the returned grid. */
static void picker_deleted_cb(lv_event_t *e)
{
    if (s_picker == lv_event_get_target(e)) s_picker = NULL;
}

static lv_obj_t *picker_overlay(const char *title)
{
    if (s_picker) lv_obj_delete(s_picker);
    lv_obj_t *ov = ui_box(lv_layer_top());
    s_picker = ov;
    lv_obj_add_event_cb(ov, picker_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE); /* blocks the dashboard below */
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ov, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ov, UI_EDGE, 0);
    lv_obj_set_flex_flow(ov, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ov, UI_EDGE, 0);

    lv_obj_t *head = ui_box(ov);
    lv_obj_set_size(head, LV_PCT(100), UI_HEADER_H);
    ui_label(head, UI_FONT_TITLE, UI_TEXT, title);
    lv_obj_align(lv_obj_get_child(head, 0), LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *close = head_button(head, LI_X, views_close_cb, ov);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);
    /* reload the dashboard from HA (after changing it there) */
    lv_obj_t *reload = head_button(head, LI_REFRESH_CW, reload_cb, ov);
    lv_obj_align(reload, LV_ALIGN_RIGHT_MID, -(UI_TOUCH_MIN + UI_GAP), 0);

    lv_obj_t *grid = ui_box(ov);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, UI_GAP, 0);
    lv_obj_set_style_pad_column(grid, UI_GAP, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    return grid;
}

static void picker_item(lv_obj_t *grid, const char *icon, const char *text, bool current, lv_event_cb_t cb,
                        void *user)
{
    lv_obj_t *b = ui_box(grid);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, (720 - 2 * UI_EDGE - UI_GAP) / 2, 112);
    lv_obj_set_style_radius(b, UI_R_TILE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(current ? UI_SURFACE2 : UI_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b, 20, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 16, 0);
    ui_bubble(b, 64, UI_ICON_M, icon, current ? UI_LOOK_ON : UI_LOOK_OFF);
    lv_obj_t *l = ui_label_line(b, UI_FONT_NAME, UI_TEXT, text);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_width(l, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
}

static void open_views(void)
{
    const int count = app_cpp_dashboard_view_count();
    lv_obj_t *grid = picker_overlay(tr(TR_DASHBOARD_VIEWS));
    const int current = app_cpp_dashboard_view_current();
    for (int i = 0; i < count; ++i) {
        picker_item(grid, i == 0 ? LI_HOUSE : LI_LIST, view_title(i), i == current, view_pick_cb, (void *)(intptr_t)i);
    }
}

static void dashboard_pick_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_current_target(e)));
    lv_obj_delete_async(overlay);
    const char *path = app_cpp_dashboards_path((int)(intptr_t)lv_event_get_user_data(e));
    if (strcmp(path, current_dashboard()) == 0) return;
    const char *def = getConfig("haas_dashboard");
    snprintf(s_active, sizeof(s_active), "%s", def && strcmp(path, def) == 0 ? "" : path);
    lv_async_call(rebuild_cb, NULL);
}

static void open_dashboards(void)
{
    lv_obj_t *grid = picker_overlay(tr(TR_DASHBOARD_DASHBOARDS));
    const char *current = current_dashboard();
    const char *def = getConfig("haas_dashboard");
    for (int i = 0; i < app_cpp_dashboards_count(); ++i) {
        const char *path = app_cpp_dashboards_path(i);
        const char *title = app_cpp_dashboards_title(i);
        picker_item(grid, def && strcmp(path, def) == 0 ? LI_HOUSE : LI_LAYOUT_DASHBOARD,
                    title[0] ? title : tr(TR_SETUP_DEFAULT_DASHBOARD), strcmp(path, current) == 0,
                    dashboard_pick_cb, (void *)(intptr_t)i);
    }
}

static void dashboards_loaded_cb(void *user, bool ok)
{
    if (user && ok && s_title) open_dashboards(); /* opened from the header */
}

static void open_views_cb(lv_event_t *e)
{
    (void)e;
    if (!multi_mode()) {
        open_views();
    } else if (app_cpp_dashboards_count() > 0) {
        open_dashboards();
    } else {
        app_cpp_dashboards_fetch(dashboards_loaded_cb, (void *)1);
    }
}

static void render_header_content(lv_obj_t *header)
{
    lv_obj_add_event_cb(header, header_delete_cb, LV_EVENT_DELETE, NULL);

    /* view title + chevron, page dots below */
    lv_obj_t *left = ui_box(header);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(left, 320 - 8, UI_HEADER_H);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(left, 4, 0);
    lv_obj_set_style_pad_row(left, 8, 0);
    lv_obj_add_event_cb(left, open_views_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title_row = ui_box(left);
    lv_obj_set_size(title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_row, 8, 0);
    s_title = ui_label(title_row, UI_FONT_TITLE, UI_TEXT, "");
    lv_obj_set_style_max_width(s_title, 270, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_MODE_DOTS);
    ui_label(title_row, UI_ICON_S, UI_TEXT3, LI_CHEVRON_DOWN);
    s_dots = ui_box(left);
    lv_obj_set_size(s_dots, LV_SIZE_CONTENT, 6);
    lv_obj_set_flex_flow(s_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_dots, 6, 0);

    /* room temperature + settings */
    lv_obj_t *right = ui_box(header);
    lv_obj_set_size(right, 360, UI_HEADER_H); /* fixed: content width was taken before the text existed */
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 8, 0);
    lv_obj_t *temp = ui_box(right);
    lv_obj_set_size(temp, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_pad_hor(temp, 16, 0);
    lv_obj_set_flex_flow(temp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(temp, 8, 0);
    ui_label(temp, UI_ICON_BUTTON, UI_TEXT2, LI_HOUSE);
    s_temp = ui_label(temp, UI_FONT_BODY, UI_TEXT, "–");
    s_hum_icon = ui_label(temp, UI_ICON_BUTTON, UI_TEXT2, LI_DROPLET);
    lv_obj_set_style_pad_left(s_hum_icon, 8, 0); /* a little apart from the temperature */
    s_hum = ui_label(temp, UI_FONT_BODY, UI_TEXT, "–");
    lv_obj_add_flag(s_hum_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hum, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(temp, LV_OBJ_FLAG_HIDDEN); /* until there is a reading */

    /* push-to-talk (voice assistant), only when it is enabled */
    s_mic = ui_box(right);
    lv_obj_remove_flag(s_mic, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_mic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_mic, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(s_mic, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(s_mic, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_mic, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(s_mic, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(s_mic, UI_ICON_M, UI_ACCENT, LI_MIC));
    lv_obj_add_event_cb(s_mic, mic_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_btn = ui_box(right);
    lv_obj_remove_flag(settings_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(settings_btn, UI_TOUCH_MIN, UI_TOUCH_MIN);
    lv_obj_set_style_radius(settings_btn, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(settings_btn, UI_ICON_M, UI_TEXT2, LI_SETTINGS));
    lv_obj_add_event_cb(settings_btn, settings_icon_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_header_timer = lv_timer_create(header_refresh_cb, 250, NULL);
    header_refresh_cb(NULL);
}

static void on_dashboard_ready(void)
{
    LVGL_LOCK();
    if (multi_mode()) app_cpp_dashboards_fetch(dashboards_loaded_cb, NULL); /* the header's title */
    isLoading = false;
    if (loadingOverlay) {
        loading_spinner_delete(loadingOverlay);
        loadingOverlay = NULL;
    }
    boot_screen_finish();
    LVGL_UNLOCK();
}

/* ── No connection: a page with a way out (the settings: another address,
 * or resetting the panel when its token is no longer valid) ── */

static void error_retry_async(void *arg)
{
    (void)arg;
    dashboard_render(lv_screen_active());
}

static void error_retry_cb(lv_event_t *e)
{
    lv_obj_delete_async(lv_event_get_user_data(e));
    lv_async_call(error_retry_async, NULL); /* after this event: it rebuilds the screen */
}

static void error_settings_cb(lv_event_t *e)
{
    lv_obj_delete_async(lv_event_get_user_data(e));
    settings_open(lv_screen_active());
}

static void error_screen_show(void)
{
    lv_obj_t *page = ui_box(lv_screen_active());
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_pad_all(page, UI_EDGE, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *body = ui_box(page);
    lv_obj_set_size(body, LV_PCT(100), 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 20, 0);
    ui_bubble(body, 96, UI_ICON_BUTTON, LI_TRIANGLE_ALERT, UI_LOOK_WARN);
    lv_obj_t *text = ui_label(body, UI_FONT_BODY, UI_TEXT2, tr(TR_SETUP_SERVER_FAILED));
    lv_obj_set_width(text, 560);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *buttons = ui_box(page);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, UI_GAP, 0);
    ui_action_button(buttons, tr(TR_SETTINGS_TITLE), UI_LOOK_INFO, error_settings_cb, page);
    ui_action_button(buttons, tr(TR_SETUP_RETRY), UI_LOOK_ON, error_retry_cb, page);
}

static void callback_ws_error(void)
{
    isLoading = false;
    LVGL_LOCK();
    if(loadingOverlay) {
        loading_spinner_delete(loadingOverlay);
        loadingOverlay = NULL;
    }
    boot_screen_hide();
    error_screen_show();
    LVGL_UNLOCK();
}

void dashboard_render(lv_obj_t *parent)
{
    /* Clear any leftover splash / error content before we start; a
     * dashboard built before is shut down first (it holds its objects). */
    app_cpp_dashboard_release_ui();
    lv_obj_clean(parent);

    isLoading = true;
    if (boot_screen_active()) { /* the first dashboard since the start */
        boot_screen_show();
        boot_screen_step(BOOT_STEP_HA);
    } else {
        loadingOverlay = loading_spinner_create(lv_screen_active(), NULL);
    }

    /* Screen: true black, 16 edge; header 64, then the tiles (gap 12). */
    lv_obj_set_style_bg_color(parent, lv_color_hex(UI_BG), LV_PART_MAIN);
    menu = ui_box(parent);
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(menu, UI_EDGE, 0);
    lv_obj_set_style_pad_hor(menu, UI_EDGE, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(menu, UI_GAP, 0);

    lv_obj_t *header = ui_box(menu);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(header, LV_PCT(100), UI_HEADER_H);
    render_header_content(header);
    app_cpp_screensaver_set_on_idle(go_home_cb);
    app_cpp_music_pull_zone(header); /* pulled down from here: the music player */
    addons_init(); /* Music Assistant connects now, not only once the settings were opened */

    menuContent = ui_box(menu);
    lv_obj_remove_flag(menuContent, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_width(menuContent, LV_PCT(100));
    lv_obj_set_flex_grow(menuContent, 1);

    // Live cutover (C++ migration): the actual card-rendering
    // content used to be built here directly via cJSON parsing + manual
    // grid math + renderWidgetByType() (see git history) — that is now
    // entirely the new C++ stack's job (WidgetFactory/DashboardSession/
    // DashboardController), reached through the stable app_facade.h C
    // ABI boundary. This file keeps owning only what it always owned
    // outside that content: the window chrome, header, and loading/error
    // overlays.
    const char *ha_url = getConfig("haas_url");
    const char *token = getConfig("haas_token");
    const char *dashboard_path = current_dashboard();

    // the facade turns it into ws(s)://host:port/api/websocket (https too)
    app_cpp_dashboard_render(menuContent, ha_url ? ha_url : "", token ? token : "", dashboard_path ? dashboard_path : "",
                              on_dashboard_ready, callback_ws_error);
}
