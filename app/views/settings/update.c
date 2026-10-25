/**
 * @file update.c
 * @brief System settings tab (firmware updates and the device itself)
 *
 * Installed versions (app + WiFi co-processor), automatic daily check,
 * co-processor updates on/off, then system information, resetting the
 * panel and (developer builds) the setup's QR code;
 * tr(TR_SETTINGS_UPDATE_CHECK) / tr(TR_BUTTON_INSTALL). The update source is fixed at build
 * time (app/cpp/update/UpdateSource.h). The work is done by
 * UpdateController (app_cpp_update_* in app/cpp/app_facade.h): signed
 * manifest, download into the inactive slot, restart.
 *
 * New UI: uses the design guide (docs/design/DESIGN_GUIDE.md) for its own
 * elements — sand primary button, sand progress on a dark track.
 */

#include "../../lucide_icons.h"
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../../i18n.h"
#include "../../ui_theme.h"
#include "update.h"
#include "settings.h"
#include "settings_tab.h"
#include "../../config.h"
#include "../../cpp/app_facade.h"
#include "../../fonts.h"
#include "../views.h"
#include "factory_reset.h"

/* Design guide tokens used here */
#define DG_SAND        0xD6B98A
#define DG_ACTIVE      0x2B2A27
#define DG_SURFACE_2   0x1F1E1C
#define DG_TEXT        0xECE9E3
#define DG_TEXT_2      0xA39F97
#define DG_WARN        0xD98A73
#define DG_OK          0x8FA88A

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_installed = NULL;
static lv_obj_t *s_coproc = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_check_btn = NULL;
static lv_obj_t *s_install_btn = NULL;
static lv_obj_t *s_install_lbl = NULL;
static lv_obj_t *s_bar = NULL;

static void set_enabled(lv_obj_t *btn, bool on)
{
    if (!btn) return;
    if (on) lv_obj_remove_state(btn, LV_STATE_DISABLED);
    else lv_obj_add_state(btn, LV_STATE_DISABLED);
}

static void refresh(void)
{
    if (!s_page) return;
    app_cpp_update_status_t st;
    app_cpp_update_get_status(&st);

    lv_label_set_text(s_installed, st.installed_version[0] ? st.installed_version : tr(TR_SETTINGS_UNKNOWN));
    lv_label_set_text(s_coproc, st.coprocessor_version[0] ? st.coprocessor_version : tr(TR_SETTINGS_UNKNOWN));

    const char *msg = st.message;
    uint32_t color = DG_TEXT_2;
    switch (st.state) {
        case APP_CPP_UPDATE_IDLE: msg = tr(TR_SETTINGS_UPDATE_NOT_CHECKED); break;
        case APP_CPP_UPDATE_CHECKING: msg = tr(TR_SETTINGS_UPDATE_CHECKING); break;
        case APP_CPP_UPDATE_UP_TO_DATE: color = DG_OK; msg = tr(TR_SETTINGS_UPDATE_UP_TO_DATE); break;
        case APP_CPP_UPDATE_RESTARTING: msg = tr(TR_SETTINGS_UPDATE_RESTARTING); break;
        case APP_CPP_UPDATE_AVAILABLE: color = DG_SAND; break;
        case APP_CPP_UPDATE_ERROR: color = DG_WARN; break;
        default: break;
    }
    lv_label_set_text(s_status, msg && msg[0] ? msg : "");
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);

    bool busy = st.state == APP_CPP_UPDATE_CHECKING || st.state == APP_CPP_UPDATE_INSTALLING ||
                st.state == APP_CPP_UPDATE_RESTARTING;
    set_enabled(s_check_btn, !busy);

    if (st.state == APP_CPP_UPDATE_AVAILABLE) {
        char buf[48];
        tr_format(buf, sizeof(buf), TR_SETTINGS_UPDATE_INSTALL_VERSION, (const char *[]){st.available_version}, 1);
        lv_label_set_text(s_install_lbl, buf);
        lv_obj_remove_flag(s_install_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_install_btn, LV_OBJ_FLAG_HIDDEN);
    }

    if (st.state == APP_CPP_UPDATE_INSTALLING || st.state == APP_CPP_UPDATE_RESTARTING) {
        lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_bar, st.progress, LV_ANIM_OFF);
        if (st.state == APP_CPP_UPDATE_INSTALLING) {
            char buf[48];
            char pct[8];
            snprintf(pct, sizeof(pct), "%d", st.progress);
            tr_format(buf, sizeof(buf), TR_SETTINGS_UPDATE_INSTALLING, (const char *[]){pct}, 1);
            lv_label_set_text(s_status, buf);
        }
    } else {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_status_change(void *user)
{
    (void)user;
    refresh();
}

static void check_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_update_check();
    refresh();
}

static void install_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_update_install();
    refresh();
}

static void auto_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    updateConfig("ota_auto_check", lv_obj_has_state(sw, LV_STATE_CHECKED) ? "1" : "0");
    saveConfig();
}

/* c6_updates: "0" leaves the co-processor's firmware alone; applies to the
 * next firmware update (UpdateController leaves out the C6 image). */
static void coproc_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    updateConfig("c6_updates", lv_obj_has_state(sw, LV_STATE_CHECKED) ? "1" : "0");
    saveConfig();
}

static void page_delete_cb(lv_event_t *e)
{
    (void)e;
    app_cpp_update_set_listener(NULL, NULL);
    s_page = s_installed = s_coproc = s_status = s_check_btn = s_install_btn = s_install_lbl = s_bar = NULL;
}

static lv_obj_t *row_label(lv_obj_t *row, const char *icon, const char *text)
{
    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, UI_ICON_BUTTON, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(UI_TEXT), 0);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_flex_grow(lbl, 1);
    return lbl;
}

static lv_obj_t *row_value(lv_obj_t *row)
{
    lv_obj_t *v = lv_label_create(row);
    lv_obj_set_style_text_font(v, font_manrope_500_22, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(DG_TEXT_2), 0);
    return v;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb, lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_height(btn, 64);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? DG_SAND : DG_ACTIVE), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font_manrope_600_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(primary ? 0x000000 : DG_TEXT), 0);
    lv_obj_center(lbl);
    if (label_out) *label_out = lbl;
    return btn;
}

static void create_system_rows(lv_obj_t *page);

static lv_obj_t *create_update_page(lv_obj_t *parent)
{
    lv_obj_t *page = settings_page_create(parent);
    s_page = page;
    lv_obj_add_event_cb(page, page_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *row = settings_row_create(page);
    row_label(row, LI_CPU, tr(TR_SETTINGS_UPDATE_FIRMWARE));
    s_installed = row_value(row);

    row = settings_row_create(page);
    row_label(row, LI_WIFI, tr(TR_SETTINGS_UPDATE_WIFI_MODULE));
    s_coproc = row_value(row);

    row = settings_row_create(page);
    row_label(row, LI_CLOCK, tr(TR_SETTINGS_UPDATE_DAILY));
    lv_obj_t *sw = lv_switch_create(row);
    const char *ac = getConfig("ota_auto_check");
    if (!(ac && strcmp(ac, "0") == 0)) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(DG_SAND), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, auto_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = settings_row_create(page);
    row_label(row, LI_REFRESH_CW, tr(TR_SETTINGS_UPDATE_WIFI_MODULE_UPDATES));
    sw = lv_switch_create(row);
    const char *c6 = getConfig("c6_updates");
    if (!(c6 && strcmp(c6, "0") == 0)) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(DG_SAND), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, coproc_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Status + progress (plain, under the cards) */
    lv_obj_t *status_box = lv_obj_create(page);
    lv_obj_remove_style_all(status_box);
    lv_obj_set_size(status_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(status_box, 8, 0);
    lv_obj_set_style_pad_ver(status_box, 6, 0);
    lv_obj_set_style_pad_row(status_box, 10, 0);
    lv_obj_set_flex_flow(status_box, LV_FLEX_FLOW_COLUMN);
    s_status = lv_label_create(status_box);
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status, font_manrope_500_18, 0);
    s_bar = lv_bar_create(status_box);
    lv_obj_set_size(s_bar, LV_PCT(100), 12);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_radius(s_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(DG_SURFACE_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(DG_SAND), LV_PART_INDICATOR);

    /* Buttons */
    lv_obj_t *btns = lv_obj_create(page);
    lv_obj_remove_style_all(btns);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(btns, 12, 0);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    s_check_btn = button(btns, tr(TR_SETTINGS_UPDATE_CHECK), false, check_cb, NULL);
    s_install_btn = button(btns, tr(TR_BUTTON_INSTALL), true, install_cb, &s_install_lbl);

    app_cpp_update_set_listener(on_status_change, NULL);
    refresh();
    create_system_rows(page);
    return page;
}

/* Overview tile: installed version, or the available one. */
static const char *update_status(bool *ok)
{
    static char buf[64];
    app_cpp_update_status_t st;
    app_cpp_update_get_status(&st);
    *ok = false;
    const char *args[] = {st.state == APP_CPP_UPDATE_AVAILABLE ? st.available_version
                          : st.installed_version[0]          ? st.installed_version
                                                             : "–"};
    tr_format(buf, sizeof(buf), st.state == APP_CPP_UPDATE_AVAILABLE ? TR_SETTINGS_UPDATE_AVAILABLE : TR_SETTINGS_VERSION,
              args, 1);
    return buf;
}

/* ── System: information, reset, developer rows ── */

/* Developer builds (and the simulator): the setup's QR code without a reset */
#if defined(HOMEPANE_AUDIO_DEBUG) || !defined(ESP_PLATFORM)
#define HOMEPANE_DEVELOPER_ROWS 1
static void qr_preview_cb(lv_event_t *e)
{
    (void)e;
    initialization_qr_preview();
}
#endif

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
    const char *parts = app_cpp_hw_parts();
    const char *args[] = {device && device[0] ? device : "Homepane",
                          st.installed_version[0] ? st.installed_version : tr(TR_SETTINGS_UNKNOWN), lvgl_version, build,
                          parts[0] ? parts : tr(TR_SETTINGS_GENERAL_NO_HARDWARE)};
    char info_text[512];
    tr_format(info_text, sizeof(info_text), TR_SETTINGS_GENERAL_SYSINFO, args, 5);

    lv_obj_t *msgbox = lv_msgbox_create(NULL);
    lv_obj_set_width(msgbox, 520);
    /* the app's font: LVGL's default lacks umlauts */
    lv_obj_set_style_text_font(lv_msgbox_add_title(msgbox, tr(TR_SETTINGS_GENERAL_SYSTEM_INFO)), UI_FONT_NAME, 0);
    lv_obj_set_style_text_font(lv_msgbox_add_text(msgbox, info_text), UI_FONT_LABEL, 0);
    lv_msgbox_add_close_button(msgbox);
    lv_obj_add_event_cb(msgbox, close_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(msgbox);
}

static void reset_panel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    factory_reset_ask();
}

/* After the update rows: system information, resetting the panel and, in
 * developer builds, the setup's QR code. */
static void create_system_rows(lv_obj_t *page)
{
    settings_link_row(page, LI_INFO, tr(TR_SETTINGS_GENERAL_SYSTEM_INFO), show_system_info_cb);

    lv_obj_t *reset = settings_icon_row(page, LI_REFRESH_CW, tr(TR_SETTINGS_HA_RESET));
    lv_obj_add_flag(reset, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(reset, lv_color_hex(UI_WARN_BG), 0); // destructive: warning colours
    lv_obj_set_style_bg_opa(reset, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(reset, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(reset, 1), lv_color_hex(UI_WARN), 0);
    lv_obj_add_event_cb(reset, reset_panel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hint = lv_label_create(settings_row_create(page)); /* a row with one label: a hint */
    lv_label_set_text(hint, tr(TR_SETTINGS_HA_RESET_HINT));

#ifdef HOMEPANE_DEVELOPER_ROWS
    settings_link_row(page, LI_QR_CODE, tr(TR_SETTINGS_GENERAL_QR_PREVIEW), qr_preview_cb);
#endif
}

static const settings_tab_t update_tab = {
    .name = "System",
    .create_page = create_update_page,
    .name_key = TR_SETTINGS_SYSTEM,
    .line_icon = LI_CPU,
    .status = update_status,
};

void settings_update_init(void)
{
    settings_register_tab(&update_tab);
}
