#ifndef APP_CPP_APPLICATION_APPSTATECONTROLLER_H
#define APP_CPP_APPLICATION_APPSTATECONTROLLER_H

namespace app::application {

// C++ migration phase 11: the one place that decides which
// top-level screen is showing (Init/Setup/Complete), replacing the
// duplicated render_content()/enter_*_state() logic that used to live
// separately in platforms/simulator/main.c and
// platforms/esp32p4/main/app_entry.c + app_states.c.
//
// enter_init_state()/enter_setup_state()/enter_complete_state() are not
// boot-only: the legacy C views call them again at runtime (dashboard
// picked in app/views/setup.c, manual token saved in
// app/views/initialization.c, both platforms' OAuth long-lived-token
// exchange in app/websocket_client.c / platforms/esp32p4/main/
// websocket_client_esp.c — the ESP32 one specifically while already
// holding lvgl_port_lock()). Callers reach these through the *same*
// free C function names as before (enter_init_state/enter_setup_state/
// enter_complete_state, now thin extern "C" shims in app_facade.cpp
// forwarding here) — this class itself does no locking of its own,
// matching that existing contract exactly.
class AppStateController {
public:
    // needs_pre_complete_splash: true only for ESP32 (a WiFi-wait splash
    // screen belongs between Setup and Complete there; the simulator has
    // no such wait). splash_hook: the platform-provided callback invoked
    // by enter_initial_state() instead of rendering the dashboard
    // directly when needs_pre_complete_splash is true — nullptr on the
    // simulator, where it is never called. Passed as a function pointer
    // (set only from app_facade.cpp's own #ifdef ESP_PLATFORM block,
    // mirroring how it already picks a concrete IWebSocketTransport per
    // platform) rather than this class calling platforms/esp32p4/main/
    // app_states.c's enter_splash_state() directly, which would fail to
    // link on the simulator build (that function isn't compiled there).
    AppStateController(bool needs_pre_complete_splash, void (*splash_hook)(bool has_saved_ssid));

    // Boot-time only: loads config, initializes the error handler and
    // shared fonts, then decides and enters the initial state by reading
    // "haas_token"/"haas_dashboard". Matches the legacy render_content()
    // exactly, including deferring to splash_hook_ instead of rendering
    // the dashboard directly when needs_pre_complete_splash_ is set (the
    // splash step's own internals — including calling back into
    // enter_complete_state() once ready — are untouched by this class).
    void enter_initial_state();

    // Re-entrant, callable at any time after boot too — see the runtime
    // call sites in the class comment above. Each does a plain "clean
    // screen + set background + render", no splash branch (matches the
    // legacy app_states.c's own enter_*_state() functions exactly).
    void enter_init_state();
    void enter_setup_state();
    void enter_complete_state();

private:
    bool needs_pre_complete_splash_;
    void (*splash_hook_)(bool);
};

} // namespace app::application

#endif // APP_CPP_APPLICATION_APPSTATECONTROLLER_H
