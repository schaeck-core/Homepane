#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_lvgl_port.h"
#define LVGL_LOCK()   lvgl_port_lock(0)
#define LVGL_UNLOCK() lvgl_port_unlock()
#else
#define LVGL_LOCK()   ((void)0)
#define LVGL_UNLOCK() ((void)0)
#endif
#include "lvgl/lvgl.h"
#include "../i18n.h"
#include "../config.h"
#include "../ui_kit.h"
#include "../cpp/app_facade.h"
#include "setup_frame.h"
#include "views.h"

// Setup step "dashboard": which Lovelace dashboard the panel shows, then
// the dashboard mode (config key dashboard_mode: one dashboard with its
// views in the header, or several dashboards to switch between). The list
// comes from SetupController (get_panels) through app_facade.h; this file
// owns only the UI.

extern void enter_init_state(void);

typedef struct {
    char *label;
    char *value;
} dashboard_item_t;

static dashboard_item_t *s_items;
static int s_count;
static int s_selected;
static lv_obj_t *s_parent;
static lv_obj_t *s_list;
static lv_obj_t *s_done_btn;

static void free_items(void)
{
    for (int i = 0; i < s_count; i++) {
        free(s_items[i].label);
        free(s_items[i].value);
    }
    free(s_items);
    s_items = NULL;
    s_count = 0;
}

static void row_cb(lv_event_t *e)
{
    s_selected = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < (int)lv_obj_get_child_count(s_list); i++) {
        setup_choice_row_set_selected(lv_obj_get_child(s_list, i), i == s_selected);
    }
}

static setup_frame_t frame(void);
static void show_dashboards(void);

/* ── Mode ── */

static int s_mode; /* 0: single, 1: multi */
static lv_obj_t *s_mode_rows[2];

static void mode_row_cb(lv_event_t *e)
{
    s_mode = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 2; i++) setup_choice_row_set_selected(s_mode_rows[i], i == s_mode);
}

static void finish_cb(lv_event_t *e)
{
    (void)e;
    updateConfig("haas_dashboard", s_items[s_selected].value);
    updateConfig("dashboard_mode", s_mode == 1 ? "multi" : "single");
    saveConfig();
    free_items();
    enter_complete_state();
}

static void mode_back_cb(lv_event_t *e)
{
    (void)e;
    show_dashboards();
}

static void show_mode(void)
{
    const char *mode = getConfig("dashboard_mode");
    s_mode = mode && strcmp(mode, "multi") == 0 ? 1 : 0;
    setup_frame_t f = setup_frame_create(s_parent, SETUP_STEP_DASHBOARD, tr(TR_DASHBOARD_MODE_TITLE),
                                         tr(TR_DASHBOARD_MODE_LATER), mode_back_cb);
    s_mode_rows[0] = setup_choice_row(f.content, LI_LIST, tr(TR_DASHBOARD_MODE_SINGLE), tr(TR_DASHBOARD_MODE_SINGLE_HINT),
                                      s_mode == 0, mode_row_cb, (void *)(intptr_t)0);
    s_mode_rows[1] = setup_choice_row(f.content, LI_LAYOUT_DASHBOARD, tr(TR_DASHBOARD_MODE_MULTI),
                                      tr(TR_DASHBOARD_MODE_MULTI_HINT), s_mode == 1, mode_row_cb, (void *)(intptr_t)1);
    setup_button(f.footer, LI_CHECK, tr(TR_SETUP_FINISH), true, finish_cb, NULL);
}

static void done_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected < 0 || s_selected >= s_count) return;
    show_mode();
}

static void change_login_cb(lv_event_t *e)
{
    (void)e;
    free_items();
    updateConfig("haas_token", "");
    saveConfig();
    enter_init_state();
}

static void retry_cb(lv_event_t *e)
{
    (void)e;
    setup_render(s_parent);
}

static setup_frame_t frame(void)
{
    return setup_frame_create(s_parent, SETUP_STEP_DASHBOARD, tr(TR_SETUP_SELECT_DASHBOARD),
                              tr(TR_SETUP_CHOOSE_DASHBOARD), NULL);
}

static void callback_getPanels(const app_cpp_panel_option_t *options, int count)
{
    // copy out of the callback-scoped `options` (app_facade.h)
    free_items();
    s_items = count > 0 ? calloc((size_t)count, sizeof(dashboard_item_t)) : NULL;
    s_count = s_items ? count : 0;
    for (int i = 0; i < s_count; i++) {
        s_items[i].label = strdup(options[i].title[0] ? options[i].title : tr(TR_SETUP_DEFAULT_DASHBOARD));
        s_items[i].value = strdup(options[i].url_path);
    }
    s_selected = 0;
    // preselect the saved one (setup again after a reset)
    const char *saved = getConfig("haas_dashboard");
    for (int i = 0; i < s_count; i++) {
        if (saved && strcmp(saved, s_items[i].value) == 0) s_selected = i;
    }

    LVGL_LOCK();
    show_dashboards();
    LVGL_UNLOCK();
}

static void show_dashboards(void)
{
    setup_frame_t f = frame();
    if (s_count == 0) {
        setup_message(f.content, tr(TR_SETUP_NO_DASHBOARDS), true);
        setup_button(f.footer, NULL, tr(TR_SETUP_CHANGE_LOGIN), false, change_login_cb, NULL);
        setup_button(f.footer, LI_REFRESH_CW, tr(TR_SETUP_RETRY), true, retry_cb, NULL);
        return;
    }
    s_list = ui_box(f.content);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, UI_GAP_LIST, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_count; i++) {
        setup_choice_row(s_list, LI_LAYOUT_DASHBOARD, s_items[i].label, NULL, i == s_selected, row_cb,
                         (void *)(intptr_t)i);
    }
    s_done_btn = setup_button(f.footer, LI_ARROW_RIGHT, tr(TR_SETUP_CONTINUE), true, done_cb, NULL);
}

static void callback_ws_error(void)
{
    LVGL_LOCK();
    setup_frame_t f = frame();
    setup_message(f.content, tr(TR_SETUP_SERVER_FAILED), true);
    setup_button(f.footer, NULL, tr(TR_SETUP_CHANGE_LOGIN), false, change_login_cb, NULL);
    setup_button(f.footer, LI_REFRESH_CW, tr(TR_SETUP_RETRY), true, retry_cb, NULL);
    LVGL_UNLOCK();
}

void setup_render(lv_obj_t *parent)
{
    s_parent = parent;
    s_list = NULL;
    s_done_btn = NULL;

    setup_frame_t f = frame();
    lv_obj_set_flex_align(f.content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_loader(f.content, 20);
    ui_label(f.content, UI_FONT_LABEL, UI_TEXT2, tr(TR_SETUP_LOADING_DASHBOARDS));

    const char *ha_url = getConfig("haas_url");
    const char *token = getConfig("haas_token");
    // the facade turns haas_url into ws(s)://host:port/api/websocket
    app_cpp_setup_fetch_panels(ha_url ? ha_url : "", token ? token : "", callback_getPanels, callback_ws_error);
}
