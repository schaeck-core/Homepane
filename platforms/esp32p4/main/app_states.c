#include "lvgl/lvgl.h"
#include "config.h"
#include "views/views.h"
#include "views/boot_screen.h"
#include "ui_theme.h"
#include "i18n.h"
#include "views/settings/settings.h"
#include "network.h"
#include "esp_log.h"
#include <stdbool.h>


/* Shared loading overlay */
lv_obj_t *loadingOverlay = NULL;
bool isLoading = false;

static const char *TAG = "app_states";

/* ------------------------------------------------------------------
 * Splash state: the start screen (app/views/boot_screen.c) while the
 * saved Wi-Fi connects; it stays up for the dashboard after it.
 * ------------------------------------------------------------------ */
typedef struct {
    lv_timer_t *timer;
    int          ticks;
    bool         wait_wifi;
} splash_ctx_t;

static splash_ctx_t s_splash;

static void splash_open_settings(void)
{
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
        boot_screen_error(tr(TR_BOOT_WIFI_FAILED), tr(TR_BOOT_OPEN_SETTINGS), splash_open_settings);
    }
}

void enter_splash_state(bool has_saved_ssid)
{
    ESP_LOGI(TAG, "=> splash state (WiFi wait=%d)", has_saved_ssid);
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(UI_BG), LV_PART_MAIN);
    boot_screen_show();
    if (has_saved_ssid) boot_screen_step(BOOT_STEP_WIFI);

    s_splash.ticks        = 0;
    s_splash.wait_wifi    = has_saved_ssid;
    s_splash.timer = lv_timer_create(splash_poll_cb, 200, &s_splash);
}

/* enter_init_state()/enter_setup_state()/enter_complete_state() used to
 * live here too — moved to app/cpp/application/AppStateController.cpp
 * (C++ migration phase 11), which now defines those same free
 * C-linkage symbol names via app_facade.cpp. enter_complete_state()
 * above (in splash_poll_cb()) and enter_splash_state() itself are
 * unaffected: the former still resolves to that shared implementation,
 * the latter is still called the same way, just from
 * AppStateController::enter_initial_state()'s splash hook instead of
 * the old render_content(). */
