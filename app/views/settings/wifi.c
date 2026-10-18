/**
 * @file wifi.c
 * @brief WiFi settings tab
 *
 * Shows the current WiFi connection and lets the user scan for networks
 * and switch to another one (wifi_picker, shared with the setup screen).
 * Credentials are saved by WifiController only after a successful
 * connection.
 */

#include <stdio.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "wifi.h"
#include "settings_tab.h"
#include "settings.h"
#include "../wifi_picker.h"
#include "../../cpp/app_facade.h"
#include "../../fonts.h"
#include "../../lucide_icons.h"

static lv_obj_t *s_status_icon = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_status_detail = NULL;
static lv_timer_t *s_status_timer = NULL;

static void refresh_status(void)
{
    if (!s_status_label) return;
    app_cpp_wifi_status_t st;
    app_cpp_wifi_get_status(&st);

    char line[64];
    char detail[48] = "";
    uint32_t color = 0xFF6B6B;
    switch (st.state) {
        case APP_CPP_WIFI_CONNECTED:
            tr_format(line, sizeof(line), TR_SETTINGS_WIFI_CONNECTED_TO, (const char *[]){st.ssid}, 1);
            tr_format(detail, sizeof(detail), TR_SETTINGS_WIFI_IP, (const char *[]){st.ip}, 1);
            color = UI_OK;
            break;
        case APP_CPP_WIFI_CONNECTING:
            tr_format(line, sizeof(line), TR_SETTINGS_WIFI_CONNECTING_TO, (const char *[]){st.ssid}, 1);
            color = UI_ACCENT;
            break;
        case APP_CPP_WIFI_FAILED:
            tr_format(line, sizeof(line), TR_SETTINGS_WIFI_FAILED_TO, (const char *[]){st.ssid}, 1);
            break;
        default:
            snprintf(line, sizeof(line), "%s", tr(TR_SETTINGS_NOT_CONNECTED));
            break;
    }
    lv_label_set_text(s_status_label, line);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_icon, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(s_status_detail, detail);
    // An empty second line would push the title off-center next to the icon.
    if (detail[0]) lv_obj_remove_flag(s_status_detail, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_status_detail, LV_OBJ_FLAG_HIDDEN);
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_status();
}

static void on_connected(void *user)
{
    (void)user;
    refresh_status();
}

static void page_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_status_timer) {
        lv_timer_del(s_status_timer);
        s_status_timer = NULL;
    }
    s_status_icon = NULL;
    s_status_label = NULL;
    s_status_detail = NULL;
}

static lv_obj_t *create_wifi_page(lv_obj_t *menu)
{
    lv_obj_t *page = settings_page_create(menu);
    lv_obj_set_style_pad_row(page, 12, 0);
    lv_obj_add_event_cb(page, page_delete_cb, LV_EVENT_DELETE, NULL);

    // Current connection
    lv_obj_t *status = settings_row_create(page);
    s_status_icon = lv_label_create(status);
    lv_label_set_text(s_status_icon, LI_WIFI);
    lv_obj_set_style_text_font(s_status_icon, UI_ICON_BUTTON, 0);

    lv_obj_t *col = lv_obj_create(status);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    s_status_label = lv_label_create(col);
    lv_obj_set_style_text_font(s_status_label, font_manrope_600_22, 0);
    s_status_detail = lv_label_create(col);
    lv_obj_set_style_text_font(s_status_detail, font_manrope_500_18, 0);
    lv_obj_set_style_text_color(s_status_detail, lv_color_hex(UI_TEXT2), 0);

    // Network picker (scans on creation): fills the rest of the page.
    lv_obj_t *picker = wifi_picker_create(page, on_connected, NULL);
    lv_obj_set_flex_grow(picker, 1);
    lv_obj_set_style_min_height(picker, 320, 0);

    refresh_status();
    if (s_status_timer) lv_timer_del(s_status_timer);
    s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    return page;
}

/* Overview tile: "Verbunden · <SSID>" with the green dot. */
static const char *wifi_status(bool *ok)
{
    static char buf[64];
    app_cpp_wifi_status_t st;
    app_cpp_wifi_get_status(&st);
    *ok = st.state == APP_CPP_WIFI_CONNECTED;
    if (*ok) {
        const char *args[] = {st.ssid};
        tr_format(buf, sizeof(buf), TR_SETTINGS_CONNECTED_TO, args, 1);
    } else {
        snprintf(buf, sizeof(buf), "%s", tr(st.state == APP_CPP_WIFI_CONNECTING ? TR_SETTINGS_CONNECTING : TR_SETTINGS_NOT_CONNECTED));
    }
    return buf;
}

static const settings_tab_t wifi_tab = {
    .name = "WiFi",
    .create_page = create_wifi_page,
    .name_key = TR_SETTINGS_WIFI,
    .line_icon = LI_WIFI,
    .status = wifi_status,
};

void settings_wifi_init(void)
{
    settings_register_tab(&wifi_tab);
}
