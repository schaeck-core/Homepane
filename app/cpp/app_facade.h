#ifndef APP_CPP_APP_FACADE_H
#define APP_CPP_APP_FACADE_H

#include "lvgl/lvgl.h"

// Stable C ABI boundary between plain-C platform entry points
// (platforms/simulator/main.c, platforms/esp32p4/main/app_main.c)
// and the C++ Application (docs/ARCHITECTURE.md §8/§11). This
// facade is a permanent interop seam, not a temporary migration
// shim: platform entry/hardware init stays C, everything behind
// this boundary is C++.
#ifdef __cplusplus
extern "C" {
#endif

void app_cpp_create(void);
void app_cpp_start(void);
void app_cpp_poll(void);
void app_cpp_stop(void);

// C++ migration phase 11: the shared replacement for the
// platform-duplicated render_content() (platforms/simulator/main.c and
// platforms/esp32p4/main/app_entry.c used to each have their own).
// Loads no config itself — callers must have already called loadConfig()
// (and, on ESP32, network_auto_connect_async(), which depends on it) —
// but does initialize the error handler and shared fonts, then decides
// and enters the initial top-level state (Init/Setup/Complete, with
// ESP32 deferring to its own WiFi-wait splash step first). Call once,
// after app_cpp_create() and any platform HAL/display init, before
// app_cpp_start().
void app_cpp_enter_initial_state(void);

// Posts a callback onto the bounded UiDispatcher queue, to run on the UI
// thread during the next app_cpp_poll(). Introduced in Phase 2 as a
// drop-in replacement for ad-hoc lv_async_call() call sites in C code
// (C++ migration phase 2: "einen ungefährlichen bestehenden
// asynchronen Pfad testweise durch Queue leiten").
void app_cpp_post_to_ui(void (*fn)(void* ctx), void* ctx);

// The live cutover's entry point (replaces app/views/dashboard.c's old
// createWSConnectionNamed()+callback_getDashboard() content-building
// logic): connects to Home Assistant using the given ws_url/access_token,
// fetches the given dashboard's lovelace config, and builds the real
// widget tree under `parent`. on_ready fires once the dashboard's
// widgets are attached in-tree; on_error fires on auth failure or a
// failed/unparseable lovelace/config fetch, but only ever before the
// first successful render (see DashboardController's own contract) —
// dashboard.c keeps owning the window/header/loading-spinner chrome
// around this call, unchanged.
// ws_url may be the plain haas_url; it is converted like in the pairing call.
void app_cpp_dashboard_render(lv_obj_t* parent, const char* ws_url, const char* access_token,
                               const char* dashboard_url_path, void (*on_ready)(void), void (*on_error)(void));

// The "Token" consumer's entry point (C++ migration phase 10):
// stops the live HA connection app_cpp_dashboard_render() started,
// replacing the legacy websocket_client.c closeConnection() call that
// stopped affecting the dashboard the moment it cut over to this stack.
// A no-op if no connection has been started yet. Does not attempt a
// live reconnect — matches the settings screen's own "please restart
// the application to re-authenticate" flow.
void app_cpp_dashboard_disconnect(void);

// Shuts the built dashboard down before its LVGL parent is cleared for a
// rebuild (the HA connection stays).
void app_cpp_dashboard_release_ui(void);

// Views of the built dashboard (header title, page dots, view list). 0 /
// "" before the dashboard is built.
int app_cpp_dashboard_view_count(void);
int app_cpp_dashboard_view_current(void);
const char* app_cpp_dashboard_view_title(int index);
void app_cpp_dashboard_view_show(int index);
// The first view, scrolled to its top (no animation); every view scrolled up.
void app_cpp_dashboard_home(void);
// Loads the shown dashboard's config from HA again and rebuilds it (same
// view). Changes saved in HA's dashboard editor are picked up by themselves.
void app_cpp_dashboard_reload(void);

// The panel's own room temperature (its sensor), NAN without a reading.
float app_cpp_room_temperature(void);

// A single entry of the C++ Setup consumer's parsed dashboard list — see
// app_cpp_setup_fetch_panels() below. `title`/`url_path` point into
// memory owned by the callback invocation and are only valid for the
// duration of the on_success call; copy them if the receiver needs to
// keep them longer.
typedef struct {
    const char* title;
    const char* url_path;
} app_cpp_panel_option_t;

// The "Setup" consumer's entry point (C++ migration phase 10):
// replaces app/views/setup.c's own createWSConnectionNamed()+
// ws_send_message_named()+callback_getPanels() manual cJSON parsing.
// Connects to Home Assistant using the given ws_url/access_token,
// fetches the list of Lovelace dashboards via `get_panels`, and hands
// back the parsed list. on_success fires exactly once with the parsed
// list (`options`/`count`, possibly count==0 if the server truly has
// none — not itself an error); on_error fires on auth failure or a
// failed `get_panels` request, but only ever before the first
// successful fetch. Uses its own independent HA connection, entirely
// separate from app_cpp_dashboard_render()'s.
// ws_url may be the plain haas_url; it is converted like in the pairing call.
void app_cpp_setup_fetch_panels(const char* ws_url, const char* access_token,
                                 void (*on_success)(const app_cpp_panel_option_t* options, int count),
                                 void (*on_error)(void));

// The OAuth device-pairing flow's WebSocket half (C++ migration
// phase 10): replaces app/websocket_client.c's getLongLivedToken() /
// platforms/esp32p4/main/websocket_client_esp.c's getLongLivedToken().
// access_token is the short-lived OAuth access token already obtained by
// the caller's own HTTP exchange with HA's /auth/token endpoint (NOT the
// eventual long-lived token) — this function only does the WS half.
// ws_url may be the plain haas_url ("http(s)://host:port"); it is converted
// to HA's websocket endpoint ("ws(s)://host:port/api/websocket") here.
// client_name is passed verbatim as HA's auth/long_lived_access_token
// "client_name" field. on_success fires exactly once with the new
// long-lived token (valid only for the duration of the callback — copy it
// if you need it longer); on_error fires exactly once with a short
// diagnostic string otherwise. Neither callback persists the token or
// transitions app state — that remains the caller's job (mirrors
// app_cpp_setup_fetch_panels()).
//
// Must be called from the UI thread (i.e. from inside an
// app_cpp_post_to_ui() callback, or from app_cpp_poll() itself) — unlike
// app_cpp_setup_fetch_panels()/app_cpp_dashboard_render(), whose existing
// callers already happen to run on the UI thread, this flow's trigger
// (the platform webserver's HTTP handler) runs on its own worker
// thread/task on both platforms, and HaClient/WebSocketConnection have no
// internal synchronization against app_cpp_poll()'s single-threaded pump.
void app_cpp_pairing_exchange_long_lived_token(const char* ws_url, const char* access_token,
                                                const char* client_name,
                                                void (*on_success)(const char* long_lived_token),
                                                void (*on_error)(const char* message));

// Starts the persistent Music Assistant connection (docs/ARCHITECTURE.md
// §14: MaClient/MaCommandService foundation). Call once, after
// loadConfig(), with the values already read from
// musicassistant_url/username/password — this function does not touch
// config itself (matches every other app_cpp_* entry point's contract). A
// second call while a connection already exists is a no-op (logged, not
// an error) — this is meant to be started once for the process lifetime,
// not per-attempt like app_cpp_pairing_exchange_long_lived_token().
//
// cached_token: a previously-persisted session token (from an earlier
// on_token_issued call), or "" if none yet — tried first, with automatic
// silent fallback to username/password if it's stale (see MaClient's own
// auth contract). on_token_issued fires whenever a fresh session token is
// obtained, so the caller can persist it (e.g. under a new
// "musicassistant_token" config key) and skip sending the password next
// boot; the token is valid only for the duration of the callback — copy
// it if you need it longer.
//
// No on_ready/on_error callbacks: MaController follows the client's state
// itself. Add them when a consumer needs them.
void app_cpp_music_assistant_connect(const char* ws_url, const char* username, const char* password,
                                      const char* cached_token, void (*on_token_issued)(const char* token));

// ── WiFi (WifiController) ────────────────────────────────────────────────
// Shared by the setup screen and the settings "WiFi" tab. All callbacks
// fire on the UI thread (from app_cpp_poll()), so they may touch LVGL.

typedef struct {
    char ssid[33];
    int rssi;      // dBm
    bool secured;  // false = open network
} app_cpp_wifi_network_t;

typedef enum {
    APP_CPP_WIFI_DISCONNECTED = 0,
    APP_CPP_WIFI_CONNECTING,
    APP_CPP_WIFI_CONNECTED,
    APP_CPP_WIFI_FAILED,
} app_cpp_wifi_state_t;

typedef struct {
    app_cpp_wifi_state_t state;
    char ssid[33];
    char ip[16];
} app_cpp_wifi_status_t;

// Starts a background scan. Returns false (callback never fires) if a scan
// is already running. `networks` is only valid during the callback; it is
// sorted by signal strength, duplicates and hidden networks removed.
bool app_cpp_wifi_scan(void (*on_done)(const app_cpp_wifi_network_t* networks, int count, void* user), void* user);

// Connects/switches to a network. on_done fires exactly once (ok=false with
// a short user-facing message on failure/timeout). On success the
// credentials are saved as wifi_ssid/wifi_password.
void app_cpp_wifi_connect(const char* ssid, const char* password,
                          void (*on_done)(bool ok, const char* message, void* user), void* user);

void app_cpp_wifi_get_status(app_cpp_wifi_status_t* out);

// The dashboard's Home Assistant connection is up (authenticated).
bool app_cpp_ha_connected(void);

// ── Firmware updates (UpdateController, Track U Phase U1) ────────────────
// Source: HOMEPANE_OTA_MANIFEST_URL (app/cpp/update/UpdateSource.h, fixed
// at build time); config key ota_auto_check ("0" disables the daily check). The listener fires on the UI thread on every status change.

typedef enum {
    APP_CPP_UPDATE_IDLE = 0,
    APP_CPP_UPDATE_CHECKING,
    APP_CPP_UPDATE_UP_TO_DATE,
    APP_CPP_UPDATE_AVAILABLE,
    APP_CPP_UPDATE_INSTALLING,
    APP_CPP_UPDATE_RESTARTING,
    APP_CPP_UPDATE_ERROR,
} app_cpp_update_state_t;

typedef struct {
    app_cpp_update_state_t state;
    char installed_version[32];
    char coprocessor_version[32];
    char available_version[32];
    char message[128];
    int progress; // 0-100 while installing
} app_cpp_update_status_t;

void app_cpp_update_get_status(app_cpp_update_status_t* out);
bool app_cpp_update_check(void);   // false if busy or no source configured
bool app_cpp_update_install(void); // false unless an update is available
void app_cpp_update_set_listener(void (*on_change)(void* user), void* user); // NULL clears

// ── Voice assistant (VoiceController, docs/voice/VOICE_ASSISTANT_PLAN.md) ─
// Push-to-talk over the dashboard's HA connection. The listener fires on the
// UI thread on every state change; the level (0..1) is for animations and
// can be polled every frame.

typedef enum {
    APP_CPP_VOICE_IDLE = 0,
    APP_CPP_VOICE_LISTENING,
    APP_CPP_VOICE_PROCESSING,
    APP_CPP_VOICE_SPEAKING,
    APP_CPP_VOICE_DONE,
    APP_CPP_VOICE_ERROR,
} app_cpp_voice_state_t;

typedef struct {
    app_cpp_voice_state_t state;
    char transcript[256];
    char response[512];
    char error[128];
} app_cpp_voice_status_t;

void app_cpp_voice_button(void); // start / end of speech / cancel, by state
// enabled: assistant usable at all (settings: voice + microphone on);
// wake_word: "okay_nabu", "hey_jarvis" or "" (button only). Returns false if
// the wake word is unavailable on this platform.
bool app_cpp_voice_configure(bool enabled, const char* wake_word);
bool app_cpp_voice_is_enabled(void);
// Feedback sounds: "shimmer", "chime", "blip", "switch"; anything else = off.
void app_cpp_voice_set_sound(const char* name);
void app_cpp_voice_preview_sound(const char* name);
void app_cpp_voice_set_volume(int percent); // speaker, 0..100

// The assistants configured in Home Assistant (settings selection).
typedef enum {
    APP_CPP_VOICE_PIPELINES_LOADING = 0,
    APP_CPP_VOICE_PIPELINES_OK,
    APP_CPP_VOICE_PIPELINES_NONE,        // HA has no assistant configured
    APP_CPP_VOICE_PIPELINES_UNAVAILABLE, // not connected / request failed
} app_cpp_voice_pipelines_state_t;
void app_cpp_voice_fetch_pipelines(void (*done)(void* user), void* user); // done on the UI thread
app_cpp_voice_pipelines_state_t app_cpp_voice_pipelines_state(void);
int app_cpp_voice_pipeline_count(void);
const char* app_cpp_voice_pipeline_name(int i);
const char* app_cpp_voice_pipeline_id(int i);
int app_cpp_voice_pipeline_preferred(void); // index of HA's default, -1 if unknown

// ── Settings with dependencies (SettingRules) ───────────────────────────
// Sets an on/off config key ("1"/"0"), applies the dependency rules
// (e.g. the voice assistant needs the microphone), saves, and returns how
// many other keys changed; their names go comma-separated into
// side_effects so the UI can tell the user.
int app_cpp_setting_set(const char* key, bool on, char* side_effects, size_t len);

// ── MQTT device (MqttController; config keys mqtt_enabled, mqtt_host — default:
// the HA host — mqtt_port, mqtt_user, mqtt_password) ───────────────────────
void app_cpp_mqtt_apply_settings(void); // after changing those keys

// ── Room: a Home Assistant area (config room_area_id / room). The panel's
// MQTT device is put into it, so voice commands without a room target it.
// Doorbell: (re)reads doorbell_trigger / doorbell_camera / doorbell_unlock
// from the config and watches the trigger; test shows the overlay as if it
// rang. UI thread.
void app_cpp_doorbell_apply_settings(void);
void app_cpp_doorbell_test(void);

// Screensaver (with the HA Eventcalendar feed): shown after
// screensaver_after seconds without touch (default 120, 0 = off) over the
// dashboard. eventcalendar_enabled ("0" = off), eventcalendar_entity (the
// add-on's display sensor), screensaver_picture (an HA media content id;
// "" = the built-in picture). UI thread.
void app_cpp_screensaver_apply_settings(void);
// Seconds without touch until the screen goes off (screen_timeout, default
// 600; 0 = never), never shorter than the screensaver's time.
int app_cpp_screen_off_after_s(void);
void app_cpp_screensaver_show(void);
// Called when the screensaver comes after the time without touch (not
// when shown by hand), before it shows: the dashboard goes back to its
// start (settings, detail pages closed). The music player is closed here.
void app_cpp_screensaver_set_on_idle(void (*on_idle)(void));
// Shows the screensaver at this event (the settings' event list).
void app_cpp_screensaver_show_event(const char* id);
// The feed's events (for the settings' list).
int app_cpp_event_count(void);
const char* app_cpp_event_id(int i);
const char* app_cpp_event_title(int i);
const char* app_cpp_event_when(int i); // "Sa. 10. Okt. · 11:00–18:00 Uhr · 8,4 km entfernt"
// Whether the HA Eventcalendar add-on's display sensor exists: done(found,
// its entity id). UI thread.
void app_cpp_eventcalendar_detect(void (*done)(void* user, bool found, const char* entity), void* user);
// Pictures in HA's "My media" (or its folder "Homepane"). UI thread.
void app_cpp_media_pictures_fetch(void (*done)(void* user, bool ok), void* user);
int app_cpp_media_picture_count(void);
// Whether the last fetch found the folder "Homepane"; creating it uploads
// the built-in picture into it (admin users only): done(HTTP status: 200
// done, 401/403 not allowed, 0 no answer). UI thread.
bool app_cpp_media_folder_found(void);
void app_cpp_media_folder_create(void (*done)(void* user, int status), void* user);
const char* app_cpp_media_picture_id(int i);
const char* app_cpp_media_picture_title(int i);

// HA entities of some domains ("camera" or "event,input_button"), sorted by
// name, for pickers in the settings. UI thread; done(ok) when loaded.
void app_cpp_entities_fetch(const char* domains, void (*done)(void* user, bool ok), void* user);
// The same, only entities that have the attribute (e.g. "api_version" for
// the Eventcalendar feed sensor).
void app_cpp_entities_fetch_with(const char* domains, const char* attribute, void (*done)(void* user, bool ok),
                                 void* user);
int app_cpp_entity_count(void);
const char* app_cpp_entity_id(int i);
const char* app_cpp_entity_name(int i);

// Date and time format (time_format "24h"/"12h", date_format "eu"/"us"/
// "iso"): (re)read from the config; the clock text of now ("14:05" /
// "2:05 PM"); an example of the format for the settings.
void app_cpp_datetime_apply_settings(void);
void app_cpp_clock_text(char* buf, size_t size);
void app_cpp_datetime_example(char* buf, size_t size);

// The longest step of app_cpp_poll since the last reset (developer
// measurements): its name, *ms its duration.
const char* app_cpp_slowest_step(uint32_t* ms, bool reset);

// Music Assistant's player UI: the zone it is pulled down from (the
// dashboard header); open it directly.
void app_cpp_music_pull_zone(lv_obj_t *zone);
void app_cpp_music_open(void);
// Settings: connected and loaded; MA's players (the room's player is
// config ma_player_id, "" = automatic); the cover cache (SD card).
bool app_cpp_music_ready(void);
typedef enum {
    APP_CPP_MUSIC_OFF = 0,     // not set up (or stopped)
    APP_CPP_MUSIC_CONNECTING,
    APP_CPP_MUSIC_READY,
    APP_CPP_MUSIC_AUTH_FAILED, // user / password refused
} app_cpp_music_status_t;
int app_cpp_music_status(void);
/* the MA user the panel is logged in as ("" until known) */
const char* app_cpp_music_user(void);
// Connects anew with the config's URL / user / password (settings changed).
void app_cpp_music_reconnect(void);
// on_change: players / connection changed (the settings page); NULL clears.
void app_cpp_music_set_listener(void (*on_change)(void *user), void *user);
void app_cpp_music_refresh(void); // players anew from the server
int app_cpp_music_player_count(void);
const char *app_cpp_music_player_id(int i);
const char *app_cpp_music_player_name(int i);
void app_cpp_music_set_player(const char *id);
int app_cpp_music_cache_files(void);
void app_cpp_music_cache_clear(void);

// HA's Lovelace dashboards (get_panels over the dashboard's connection;
// the header's dashboard list, the settings). done fires on the UI thread.
void app_cpp_dashboards_fetch(void (*done)(void* user, bool ok), void* user);
int app_cpp_dashboards_count(void);
const char* app_cpp_dashboards_title(int i); // "" = HA's default dashboard
const char* app_cpp_dashboards_path(int i);
bool app_cpp_screensaver_open(void);

void app_cpp_rooms_fetch(void (*done)(void* user, bool ok), void* user); // HA's areas, UI thread
int app_cpp_room_count(void);
const char* app_cpp_room_name(int i);
const char* app_cpp_room_id(int i);
void app_cpp_room_set(const char* area_id, const char* name); // "" = none
/* Setting the device's area in HA was refused: the panel's HA user is no admin. */
bool app_cpp_room_denied(void);
bool app_cpp_mqtt_connected(void);
const char* app_cpp_mqtt_node_id(void);
void app_cpp_voice_cancel(void);
float app_cpp_voice_level(void);        // microphone, 0..1
float app_cpp_voice_output_level(void); // response audio, 0..1
void app_cpp_voice_bands(float out[3]);  // low/mid/high 0..1: mic while listening, else response
void app_cpp_voice_get_status(app_cpp_voice_status_t* out);
void app_cpp_voice_set_listener(void (*on_change)(void* user), void* user); // NULL clears

#ifdef __cplusplus
}
#endif

#endif // APP_CPP_APP_FACADE_H
