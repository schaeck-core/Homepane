#include "views.h"
#include "setup_frame.h"
#include "wifi_picker.h"
#include "../ui_kit.h"
#include "../network.h"
#include "../config.h"
#include "lvgl/lvgl.h"
#include "../i18n.h"
#include <stdio.h>
#include <string.h>

// Setup step "network" (initialization flow): connection status plus the
// shared WiFi picker (wifi_picker.c — scan list, password dialog,
// connecting overlay; WifiController behind it). "Continue" is enabled
// while a connection is up; a connection made through the picker continues
// on its own.

static lv_obj_t *s_status_bubble;
static lv_obj_t *s_status_label;
static lv_obj_t *s_status_detail;
static lv_obj_t *s_next_btn;

static void (*s_on_continue)(void);
static void (*s_on_back)(void);

static lv_timer_t *s_net_info_timer;

static void refresh_network_display(void)
{
    network_info_t info = network_get_info();

    const char *status_text = tr(TR_SETTINGS_NOT_CONNECTED);
    ui_look_t look = UI_LOOK_OFF;
    switch (info.status) {
        case NET_STATUS_CONNECTED:
            status_text = tr(TR_SETTINGS_CONNECTED);
            look = UI_LOOK_ON;
            break;
        case NET_STATUS_CONNECTING:
            status_text = tr(TR_SETTINGS_CONNECTING);
            look = UI_LOOK_INFO;
            break;
        case NET_STATUS_ERROR:
            status_text = tr(TR_SETUP_CONNECTION_ERROR);
            look = UI_LOOK_WARN;
            break;
        default:
            break;
    }
    const bool ethernet = info.type == NET_TYPE_ETHERNET;
    if (s_status_bubble) ui_bubble_set(s_status_bubble, ethernet ? LI_ETHERNET_PORT : LI_WIFI, look, 0);
    if (s_status_label) lv_label_set_text(s_status_label, status_text);
    if (s_status_detail) {
        const char *detail = "";
        if (info.status == NET_STATUS_CONNECTED) {
            detail = ethernet ? "Ethernet" : (info.ssid[0] ? info.ssid : tr(TR_SETUP_WIFI));
        }
        lv_label_set_text(s_status_detail, detail);
    }
    if (s_next_btn) setup_button_set_enabled(s_next_btn, info.status == NET_STATUS_CONNECTED);
}

static void network_setup_cleanup(void)
{
    if (s_net_info_timer) {
        lv_timer_delete(s_net_info_timer);
        s_net_info_timer = NULL;
    }
    s_status_bubble = NULL;
    s_status_label = NULL;
    s_status_detail = NULL;
    s_next_btn = NULL;
}

static void update_network_info_timer(lv_timer_t *timer)
{
    (void)timer;
    refresh_network_display();
}

static void on_wifi_connected(void *user)
{
    (void)user;
    // WifiController already saved the credentials (only on success).
    network_setup_cleanup();
    if (s_on_continue) s_on_continue();
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    network_setup_cleanup();
    if (s_on_continue) s_on_continue();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    network_setup_cleanup();
    if (s_on_back) s_on_back();
}

void network_setup_render(lv_obj_t *parent, void (*continue_callback)(void), void (*back_callback)(void),
                          bool autoskip)
{
    network_setup_cleanup();
    s_on_continue = continue_callback;
    s_on_back = back_callback;

    network_init();

    setup_frame_t f = setup_frame_create(parent, SETUP_STEP_NETWORK, tr(TR_SETUP_NETWORK), tr(TR_SETUP_NETWORK_HINT),
                                         back_callback ? back_cb : NULL);

    // status: icon circle, state, network name
    lv_obj_t *status = ui_box(f.content);
    lv_obj_set_size(status, LV_PCT(100), 88);
    lv_obj_set_style_radius(status, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(status, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_pad_hor(status, 16, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 16, 0);
    s_status_bubble = ui_bubble(status, 56, UI_ICON_BUTTON, LI_WIFI, UI_LOOK_OFF);
    s_status_label = ui_label(status, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_set_flex_grow(s_status_label, 1);
    s_status_detail = ui_label(status, UI_FONT_LABEL, UI_TEXT2, "");

    lv_obj_t *picker = wifi_picker_create(f.content, on_wifi_connected, NULL);
    lv_obj_set_flex_grow(picker, 1);

    s_next_btn = setup_button(f.footer, NULL, tr(TR_SETUP_CONTINUE), true, next_cb, NULL);

    network_ethernet_init();
    s_net_info_timer = lv_timer_create(update_network_info_timer, 1000, NULL);
    refresh_network_display();

    // Ethernet already up: nothing to choose
    network_info_t initial = network_get_info();
    if (autoskip && initial.status == NET_STATUS_CONNECTED && initial.type == NET_TYPE_ETHERNET && s_on_continue) {
        network_setup_cleanup();
        s_on_continue();
        return;
    }

#ifdef ESP_PLATFORM
    // WiFi with saved credentials was already started non-blocking at boot
    // (network_auto_connect_async()). Wait for it; if it fails, the picker
    // closes its overlay and shows the scanned networks instead.
    if (autoskip) {
        const char *saved_ssid = getConfig("wifi_ssid");
        if (saved_ssid && strlen(saved_ssid) > 0) {
            wifi_picker_await_saved(saved_ssid);
        }
    }
#endif
}
