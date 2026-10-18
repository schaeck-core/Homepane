#ifndef APP_VIEWS_WIFI_PICKER_H
#define APP_VIEWS_WIFI_PICKER_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi network picker shared by the setup screen (network_setup.c) and the
// settings "WiFi" tab: scan list with signal/lock icons, password dialog
// with on-screen keyboard, and a connecting overlay. Scanning/connecting
// runs through WifiController (app_cpp_wifi_* in app/cpp/app_facade.h) —
// never blocks the UI thread, saves credentials only on success.
//
// One picker at a time (setup and settings are never shown together). It
// tears itself down with its container; results arriving after that are
// ignored.

typedef void (*wifi_picker_connected_cb)(void *user);

// Creates the picker inside `parent` (fills the parent's width; give it a
// height or flex-grow) and starts a scan. on_connected fires (on the UI
// thread) after a successful connection made through the picker or
// awaited via wifi_picker_await_saved().
lv_obj_t *wifi_picker_create(lv_obj_t *parent, wifi_picker_connected_cb on_connected, void *user);

// Re-scans and refreshes the list.
void wifi_picker_scan(void);

// For a network that is already being connected to (saved credentials,
// started at boot): shows the connecting overlay until it is up
// (-> on_connected) or fails/times out (-> overlay closes, list is scanned).
void wifi_picker_await_saved(const char *ssid);

#ifdef __cplusplus
}
#endif

#endif // APP_VIEWS_WIFI_PICKER_H
