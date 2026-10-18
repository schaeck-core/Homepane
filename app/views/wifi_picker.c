#include "wifi_picker.h"
#include "../ui_theme.h"
#include "../i18n.h"

#include "../cpp/app_facade.h"
#include "../fonts.h"
#include "../ui_kit.h"
#include "keyboard_style.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_NETWORKS 20
#define AWAIT_TIMEOUT_TICKS 50 // × 500 ms = 25 s

// ── State (one picker at a time, see header) ─────────────────────────────────
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_refresh_btn = NULL;
static uint32_t s_instance = 0; // bumped per picker; async results carry it
static wifi_picker_connected_cb s_on_connected = NULL;
static void *s_user = NULL;

static app_cpp_wifi_network_t s_nets[MAX_NETWORKS];
static int s_net_count = 0;

// Password dialog (on lv_layer_top)
static lv_obj_t *s_dialog_overlay = NULL;
static lv_obj_t *s_ssid_ta = NULL;
static lv_obj_t *s_pass_ta = NULL;
static lv_obj_t *s_kb = NULL;
static lv_obj_t *s_eye_lbl = NULL;
static char s_dialog_ssid[33];

// Connecting overlay (on lv_layer_top)
static lv_obj_t *s_conn_overlay = NULL;
static lv_obj_t *s_conn_spinner = NULL;
static lv_obj_t *s_conn_label = NULL;
static lv_obj_t *s_conn_ok_btn = NULL;
static lv_timer_t *s_await_timer = NULL;
static int s_await_ticks = 0;
static char s_await_ssid[33];

static void render_list(bool scanning);
static void show_password_dialog(const char *ssid, bool is_other);
static void start_connect(const char *ssid, const char *password);

static void *instance_token(void) { return (void *)(uintptr_t)s_instance; }
static bool token_is_current(void *user) { return s_root && (uintptr_t)user == s_instance; }

// ── Small style helpers ──────────────────────────────────────────────────────
static lv_obj_t *make_overlay(void)
{
    lv_obj_t *ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ov, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ov, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    return ov;
}

static lv_obj_t *make_box(lv_obj_t *parent, lv_coord_t width_pct)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_PCT(width_pct), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(box, UI_R_TILE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_row(box, 14, LV_PART_MAIN);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_coord_t w, bool primary, lv_event_cb_t cb)
{
    // design guide: sand with black text (primary), "active" surface otherwise
    lv_obj_t *btn = ui_box(parent);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(btn, w, UI_TOUCH_MIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? UI_ACCENT : UI_ACTIVE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? UI_AUTO : UI_SURFACE2), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, UI_R_CONTROL, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_label(btn, UI_FONT_NAME, primary ? UI_BG : UI_TEXT, text));
    return btn;
}

static lv_obj_t *make_textarea(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 60);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UI_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, lv_color_hex(UI_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 15, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(UI_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(UI_TEXT3), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, lv_color_hex(UI_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, LV_PART_MAIN);
    return ta;
}

static void make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
}

// ── Connecting overlay ───────────────────────────────────────────────────────
static void conn_overlay_close(void)
{
    if (s_await_timer) {
        lv_timer_del(s_await_timer);
        s_await_timer = NULL;
    }
    if (s_conn_overlay) {
        lv_obj_del(s_conn_overlay);
        s_conn_overlay = NULL;
        s_conn_spinner = NULL;
        s_conn_label = NULL;
        s_conn_ok_btn = NULL;
    }
}

static void conn_ok_event_cb(lv_event_t *e)
{
    (void)e;
    conn_overlay_close();
}

static void conn_overlay_show(const char *ssid)
{
    conn_overlay_close();
    s_conn_overlay = make_overlay();
    lv_obj_t *box = make_box(s_conn_overlay, 70);
    lv_obj_center(box);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_conn_spinner = lv_spinner_create(box);
    lv_obj_set_size(s_conn_spinner, 60, 60);
    lv_obj_set_style_arc_color(s_conn_spinner, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_conn_spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_conn_spinner, lv_color_hex(UI_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_conn_spinner, 5, LV_PART_MAIN);

    char name[40];
    snprintf(name, sizeof(name), "%.32s", ssid ? ssid : "");
    const char *args[] = {name};
    char buf[96];
    tr_format(buf, sizeof(buf), TR_WIFI_CONNECTING_TO, args, 1);
    s_conn_label = lv_label_create(box);
    lv_label_set_text(s_conn_label, buf);
    lv_obj_set_style_text_font(s_conn_label, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_conn_label, lv_color_hex(UI_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_conn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_conn_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_conn_label, LV_PCT(100));
}

// Turns the spinner overlay into an error message the user dismisses (the
// old setup screen auto-hid it after 2 s and left an empty list behind).
static void conn_overlay_show_error(const char *message)
{
    if (!s_conn_overlay) return;
    if (s_conn_spinner) lv_obj_add_flag(s_conn_spinner, LV_OBJ_FLAG_HIDDEN);
    if (s_conn_label) {
        lv_label_set_text(s_conn_label, message && message[0] ? message : tr(TR_WIFI_FAILED));
        lv_obj_set_style_text_color(s_conn_label, lv_color_hex(UI_WARN), LV_PART_MAIN);
    }
    if (!s_conn_ok_btn) s_conn_ok_btn = make_button(lv_obj_get_child(s_conn_overlay, 0), tr(TR_BUTTON_OK), 120, true, conn_ok_event_cb);
}

static void notify_connected(void)
{
    conn_overlay_close();
    render_list(false); // refresh the "connected" marker
    if (s_on_connected) s_on_connected(s_user);
}

static void connect_done_cb(bool ok, const char *message, void *user)
{
    if (!token_is_current(user)) return;
    if (ok) {
        notify_connected();
    } else {
        conn_overlay_show_error(message);
    }
}

static void start_connect(const char *ssid, const char *password)
{
    conn_overlay_show(ssid);
    app_cpp_wifi_connect(ssid, password, connect_done_cb, instance_token());
}

// ── Awaiting an already started connection (saved credentials) ───────────────
static void await_timer_cb(lv_timer_t *t)
{
    (void)t;
    app_cpp_wifi_status_t st;
    app_cpp_wifi_get_status(&st);
    if (st.state == APP_CPP_WIFI_CONNECTED) {
        notify_connected();
        return;
    }
    if (st.state == APP_CPP_WIFI_FAILED || ++s_await_ticks >= AWAIT_TIMEOUT_TICKS) {
        const char *args[] = {s_await_ssid};
        char msg[160];
        tr_format(msg, sizeof(msg), TR_WIFI_SAVED_FAILED, args, 1);
        if (s_await_timer) {
            lv_timer_del(s_await_timer);
            s_await_timer = NULL;
        }
        conn_overlay_show_error(msg);
        wifi_picker_scan();
    }
}

void wifi_picker_await_saved(const char *ssid)
{
    if (!s_root) return;
    snprintf(s_await_ssid, sizeof(s_await_ssid), "%s", ssid ? ssid : "");
    conn_overlay_show(s_await_ssid);
    s_await_ticks = 0;
    s_await_timer = lv_timer_create(await_timer_cb, 500, NULL);
}

// ── Password dialog ──────────────────────────────────────────────────────────
static void dialog_close(void)
{
    if (s_dialog_overlay) {
        lv_obj_del(s_dialog_overlay);
        s_dialog_overlay = NULL;
        s_ssid_ta = NULL;
        s_pass_ta = NULL;
        s_kb = NULL;
        s_eye_lbl = NULL;
    }
}

static void dialog_connect(void)
{
    char ssid[33];
    char pass[65];
    snprintf(ssid, sizeof(ssid), "%s", s_ssid_ta ? lv_textarea_get_text(s_ssid_ta) : s_dialog_ssid);
    snprintf(pass, sizeof(pass), "%s", s_pass_ta ? lv_textarea_get_text(s_pass_ta) : "");
    if (!ssid[0]) return;
    dialog_close();
    start_connect(ssid, pass);
    memset(pass, 0, sizeof(pass));
}

static void dialog_cancel_event_cb(lv_event_t *e) { (void)e; dialog_close(); }
static void dialog_connect_event_cb(lv_event_t *e) { (void)e; dialog_connect(); }

static void dialog_eye_event_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) return;
    bool hidden = lv_textarea_get_password_mode(s_pass_ta);
    lv_textarea_set_password_mode(s_pass_ta, !hidden);
    if (s_eye_lbl) lv_label_set_text(s_eye_lbl, hidden ? LI_EYE_OFF : LI_EYE);
}

static void dialog_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (!s_kb) return;
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_READY) {
        // Keyboard's checkmark key: connect right away.
        dialog_connect();
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_password_dialog(const char *ssid, bool is_other)
{
    dialog_close();
    snprintf(s_dialog_ssid, sizeof(s_dialog_ssid), "%s", ssid ? ssid : "");

    s_dialog_overlay = make_overlay();
    lv_obj_t *box = make_box(s_dialog_overlay, 82);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 30); // leave room for the keyboard below

    char title[48];
    snprintf(title, sizeof(title), "%.32s", is_other ? tr(TR_WIFI_OTHER_TITLE) : s_dialog_ssid);
    make_label(box, title, UI_FONT_TITLE, UI_TEXT);

    if (is_other) {
        make_label(box, tr(TR_WIFI_SSID), UI_FONT_LABEL, UI_TEXT2);
        s_ssid_ta = make_textarea(box, tr(TR_WIFI_ENTER_SSID));
        lv_textarea_set_max_length(s_ssid_ta, 32);
        lv_obj_add_event_cb(s_ssid_ta, dialog_ta_event_cb, LV_EVENT_ALL, NULL);
    }

    // Password label + show/hide toggle
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pw_lbl = lv_label_create(row);
    lv_label_set_text(pw_lbl, is_other ? tr(TR_WIFI_PASSWORD_OPEN) : tr(TR_SETTINGS_PASSWORD));
    lv_obj_set_style_text_font(pw_lbl, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(pw_lbl, lv_color_hex(UI_TEXT2), LV_PART_MAIN);
    lv_obj_set_flex_grow(pw_lbl, 1);
    lv_obj_t *eye = ui_box(row);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(eye, 56, 56);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye, lv_color_hex(UI_ACTIVE), LV_PART_MAIN);
    lv_obj_set_style_radius(eye, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(eye, dialog_eye_event_cb, LV_EVENT_CLICKED, NULL);
    s_eye_lbl = ui_label(eye, UI_ICON_BUTTON, UI_TEXT2, LI_EYE);
    lv_obj_center(s_eye_lbl);

    s_pass_ta = make_textarea(box, tr(TR_SETTINGS_ENTER_PASSWORD));
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_max_length(s_pass_ta, 64);
    lv_obj_add_event_cb(s_pass_ta, dialog_ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btns = lv_obj_create(box);
    lv_obj_remove_style_all(btns);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btns, 12, LV_PART_MAIN);
    make_button(btns, tr(TR_BUTTON_CANCEL), 170, false, dialog_cancel_event_cb);
    make_button(btns, tr(TR_BUTTON_CONNECT), 190, true, dialog_connect_event_cb);

    s_kb = lv_keyboard_create(s_dialog_overlay);
    ui_style_keyboard(s_kb);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // Focus the first field so the keyboard is up right away.
    lv_obj_t *first = s_ssid_ta ? s_ssid_ta : s_pass_ta;
    lv_obj_add_state(first, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(s_kb, first);
    lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

// ── List ─────────────────────────────────────────────────────────────────────
static void item_event_cb(lv_event_t *e)
{
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    if (idx < 0) {
        show_password_dialog("", true);
        return;
    }
    if (idx >= s_net_count) return;
    const app_cpp_wifi_network_t *n = &s_nets[idx];
    if (n->secured) {
        show_password_dialog(n->ssid, false);
    } else {
        start_connect(n->ssid, "");
    }
}

static lv_obj_t *make_item(intptr_t idx)
{
    lv_obj_t *btn = lv_obj_create(s_list);
    lv_obj_set_size(btn, LV_PCT(100), 72);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_ACTIVE), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, item_event_cb, LV_EVENT_CLICKED, (void *)idx);
    return btn;
}

static void item_icon(lv_obj_t *item, const char *icon, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_font(lbl, UI_ICON_BUTTON, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
}

static void item_text(lv_obj_t *item, const char *text, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, 0);
    lv_obj_set_flex_grow(lbl, 1);
}

static void render_list(bool scanning)
{
    if (!s_list) return;
    lv_obj_clean(s_list);

    app_cpp_wifi_status_t st;
    app_cpp_wifi_get_status(&st);

    for (int i = 0; i < s_net_count; i++) {
        const app_cpp_wifi_network_t *n = &s_nets[i];
        lv_obj_t *item = make_item(i);
        const char *sig = n->rssi > -50 ? LI_WIFI
                        : n->rssi > -60 ? LI_WIFI_HIGH
                        : n->rssi > -70 ? LI_WIFI_LOW
                                        : LI_WIFI_ZERO;
        item_icon(item, sig, UI_TEXT);
        bool current = st.state == APP_CPP_WIFI_CONNECTED && strcmp(st.ssid, n->ssid) == 0;
        item_text(item, n->ssid, current ? UI_OK : UI_TEXT);
        if (current) item_icon(item, LI_CIRCLE_CHECK, UI_OK);
        if (n->secured) item_icon(item, LI_LOCK, UI_TEXT3);
    }

    lv_obj_t *other = make_item(-1);
    item_icon(other, LI_PLUS, UI_TEXT3);
    item_text(other, tr(TR_WIFI_OTHER), UI_TEXT2);

    if (scanning || s_net_count == 0) {
        lv_obj_t *hint = lv_label_create(s_list);
        lv_label_set_text(hint, scanning ? tr(TR_WIFI_SCANNING) : tr(TR_WIFI_NONE));
        lv_obj_set_style_text_font(hint, UI_FONT_LABEL, LV_PART_MAIN);
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT2), LV_PART_MAIN);
        lv_obj_set_style_pad_all(hint, 16, LV_PART_MAIN);
    }
}

static void scan_done_cb(const app_cpp_wifi_network_t *networks, int count, void *user)
{
    if (!token_is_current(user)) return;
    s_net_count = count > MAX_NETWORKS ? MAX_NETWORKS : count;
    if (s_net_count > 0) memcpy(s_nets, networks, (size_t)s_net_count * sizeof(s_nets[0]));
    if (s_refresh_btn) lv_obj_remove_state(s_refresh_btn, LV_STATE_DISABLED);
    render_list(false);
}

void wifi_picker_scan(void)
{
    if (!s_root) return;
    if (app_cpp_wifi_scan(scan_done_cb, instance_token())) {
        if (s_refresh_btn) lv_obj_add_state(s_refresh_btn, LV_STATE_DISABLED);
        render_list(true);
    }
    // else: a scan is already running; its result will refresh the list
}

static void refresh_event_cb(lv_event_t *e)
{
    (void)e;
    wifi_picker_scan();
}

static void root_delete_cb(lv_event_t *e)
{
    (void)e;
    // Overlays live on lv_layer_top(), not inside the picker: remove them
    // explicitly so they don't outlive the screen they belong to.
    dialog_close();
    conn_overlay_close();
    s_root = NULL;
    s_list = NULL;
    s_refresh_btn = NULL;
    s_on_connected = NULL;
    s_user = NULL;
    ++s_instance; // late async results are ignored
}

lv_obj_t *wifi_picker_create(lv_obj_t *parent, wifi_picker_connected_cb on_connected, void *user)
{
    if (s_root) lv_obj_del(s_root); // runs root_delete_cb

    ++s_instance;
    s_on_connected = on_connected;
    s_user = user;
    s_net_count = 0;

    s_root = lv_obj_create(parent);
    lv_obj_set_width(s_root, LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(UI_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_root, UI_R_TILE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_root, true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_delete_cb, LV_EVENT_DELETE, NULL);

    // Header: title + refresh
    lv_obj_t *hdr = lv_obj_create(s_root);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(hdr, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_right(hdr, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hdr, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, tr(TR_WIFI_AVAILABLE));
    lv_obj_set_style_text_font(title, UI_FONT_NAME, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT), LV_PART_MAIN);
    s_refresh_btn = ui_box(hdr);
    lv_obj_add_flag(s_refresh_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_refresh_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_size(s_refresh_btn, 56, 56);
    lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(UI_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(UI_ACTIVE), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_opa(s_refresh_btn, LV_OPA_50, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(s_refresh_btn, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(s_refresh_btn, refresh_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_label(s_refresh_btn, UI_ICON_BUTTON, UI_TEXT, LI_REFRESH_CW));

    // Scrollable list (plain column — lv_list's internal buttons blocked
    // touch events on this LVGL build, see network_setup.c history)
    s_list = lv_obj_create(s_root);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);

    wifi_picker_scan();
    return s_root;
}
