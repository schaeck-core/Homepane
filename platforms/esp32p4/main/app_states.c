#include "lvgl/lvgl.h"
#include "config.h"
#include "views/views.h"
#include "views/settings/settings.h"
#include "network.h"
#include "esp_log.h"
#include <stdbool.h>


/* Shared loading overlay */
lv_obj_t *loadingOverlay = NULL;
bool isLoading = false;

static const char *TAG = "app_states";

/* ------------------------------------------------------------------
 * Splash-screen state
 * ------------------------------------------------------------------ */
typedef struct {
    lv_obj_t   *status_label;
    lv_obj_t   *error_btn;
    lv_timer_t *timer;
    int          ticks;
    bool         wait_wifi;
} splash_ctx_t;

static splash_ctx_t s_splash;

static void splash_open_settings_cb(lv_event_t *e)
{
    (void)e;
    settings_init();
    settings_open(lv_screen_active());
}

static void splash_poll_cb(lv_timer_t *t)
{
    splash_ctx_t *ctx = (splash_ctx_t *)lv_timer_get_user_data(t);
    ctx->ticks++;
    net_status_t st = network_get_status();

    if (st == NET_STATUS_CONNECTED) {
        lv_timer_delete(ctx->timer); ctx->timer = NULL;
        enter_complete_state();
        return;
    }
    if (!ctx->wait_wifi && ctx->ticks >= 8) {   /* no SSID: 1.6 s logo */
        lv_timer_delete(ctx->timer); ctx->timer = NULL;
        enter_complete_state();
        return;
    }
    if (ctx->wait_wifi && (st == NET_STATUS_ERROR || ctx->ticks >= 75)) {
        lv_timer_delete(ctx->timer); ctx->timer = NULL;
        lv_label_set_text(ctx->status_label,
            "WLAN-Verbindung fehlgeschlagen.\nNetzwerkeinstellungen pruefen.");
        lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(0xFF4444), 0);
        lv_obj_clear_flag(ctx->error_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (ctx->wait_wifi) {
        static const char *const dots[] = {
            "Verbinde.", "Verbinde..", "Verbinde...", "Verbinde"
        };
        lv_label_set_text(ctx->status_label, dots[ctx->ticks % 4]);
    }
}

void enter_splash_state(bool has_saved_ssid)
{
    ESP_LOGI(TAG, "=> splash state (WiFi wait=%d)", has_saved_ssid);
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *logo = lv_label_create(scr);
    lv_label_set_text(logo, "HOMEPANE");
    lv_obj_set_style_text_color(logo, lv_color_white(), 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *status_lbl = lv_label_create(scr);
    lv_label_set_text(status_lbl, has_saved_ssid ? "Verbinde." : "");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_lbl, LV_PCT(70));
    lv_obj_set_style_text_align(status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *err_btn = lv_button_create(scr);
    lv_obj_set_size(err_btn, 220, 48);
    lv_obj_set_style_bg_color(err_btn, lv_color_hex(0x2962FF), 0);
    lv_obj_set_style_radius(err_btn, 10, 0);
    lv_obj_align(err_btn, LV_ALIGN_CENTER, 0, 130);
    lv_obj_add_flag(err_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(err_btn, splash_open_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(err_btn);
    lv_label_set_text(btn_lbl, "Einstellungen oeffnen");
    lv_obj_center(btn_lbl);

    s_splash.status_label = status_lbl;
    s_splash.error_btn    = err_btn;
    s_splash.ticks        = 0;
    s_splash.wait_wifi    = has_saved_ssid;
    s_splash.timer = lv_timer_create(splash_poll_cb, 200, &s_splash);
}

/* enter_init_state()/enter_setup_state()/enter_complete_state() used to
 * live here too — moved to app/cpp/application/AppStateController.cpp
 * (CPP_MIGRATION_PLAN.md Phase 11), which now defines those same free
 * C-linkage symbol names via app_facade.cpp. enter_complete_state()
 * above (in splash_poll_cb()) and enter_splash_state() itself are
 * unaffected: the former still resolves to that shared implementation,
 * the latter is still called the same way, just from
 * AppStateController::enter_initial_state()'s splash hook instead of
 * the old render_content(). */
