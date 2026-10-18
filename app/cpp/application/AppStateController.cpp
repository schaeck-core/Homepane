#include "AppStateController.h"

#include "lvgl/lvgl.h"

#include "../../config.h"
#include "../../error_handler.h"
#include "../../fonts.h"
#include "../../views/views.h"

#ifdef ESP_PLATFORM
#include "../../../platforms/esp32p4/main/webserver_esp32.h"
#else
#include "../../../platforms/simulator/webserver.h"
#endif

#include <cstring>

namespace app::application {

AppStateController::AppStateController(bool needs_pre_complete_splash, void (*splash_hook)(bool has_saved_ssid))
    : needs_pre_complete_splash_(needs_pre_complete_splash), splash_hook_(splash_hook) {}

void AppStateController::enter_initial_state() {
    // loadConfig() is deliberately NOT called here: it runs earlier, in
    // each platform's own entry point, before app_cpp_enter_initial_state()
    // — ESP32's network_auto_connect_async() (still platform-side, not
    // part of this shared class) reads config values set by it and must
    // run between loadConfig() and this call.
    error_handler_init();
    fonts_init();

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);

    const char* token = getConfig("haas_token");
    const char* dashboard = getConfig("haas_dashboard");
    if (!token || std::strlen(token) == 0) {
        enter_init_state();
    } else if (!dashboard || std::strlen(dashboard) == 0) {
        enter_setup_state();
    } else if (needs_pre_complete_splash_ && splash_hook_) {
        const char* ssid = getConfig("wifi_ssid");
        bool has_ssid = ssid && std::strlen(ssid) > 0;
        splash_hook_(has_ssid);
    } else {
        enter_complete_state();
    }
}

void AppStateController::enter_init_state() {
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);
    if (startServer()) {
        initialization_render(lv_screen_active());
    }
}

void AppStateController::enter_setup_state() {
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);
    setup_render(lv_screen_active());
}

void AppStateController::enter_complete_state() {
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x111111), LV_PART_MAIN);
    dashboard_render(lv_screen_active());
}

} // namespace app::application
