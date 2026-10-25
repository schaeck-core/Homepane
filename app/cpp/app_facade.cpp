#include "app_facade.h"

#include "application/AppStateController.h"
#include "application/Application.h"
#include "application/DashboardController.h"
#include "application/PairingController.h"
#include "application/SetupController.h"
#include "application/UpdateController.h"
#include "update/UpdateSource.h"
#include "voice/VoiceController.h"
#include "mqtt/MqttController.h"
#include "home_assistant/DeviceLink.h"
#include "sensors/SimulatedSensors.h"
#include "settings/SettingRules.h"
#include "application/WifiController.h"
#include "media/ImageLoader.h"
#include "doorbell/DoorbellController.h"
#include "ui/DoorbellOverlay.h"
#include "application/PanelListParser.h"
#include "ui/Screensaver.h"
#include "eventcalendar/EventFeed.h"
#include "home_assistant/HaUrl.h"
#include "core/DateText.h"
#include "core/UiDispatcher.h"
#include "music_assistant/MaClient.h"
#include "music_assistant/MaController.h"
#include "ui/music/MusicPlayer.h"
#include "ui/music/MusicPullGesture.h"
#include "ui/music/MusicTrackPopover.h"
#include "net/WebSocketConnection.h"
#include "platform/PortableDefaults.h"

// The one place that picks a concrete IWebSocketTransport per platform
// (mirrors PortableDefaults.cpp's own #ifdef ESP_PLATFORM branches) —
// DashboardController itself stays platform-agnostic, only ever seeing
// the abstract app::net::IWebSocketTransport interface.
#ifdef ESP_PLATFORM
#include "../../platforms/esp32p4/main/EspWebSocketTransport.h"
#include "../../platforms/esp32p4/main/EspUpdateAdapter.h"
#include "../../platforms/esp32p4/main/EspHttpFetcher.h"
#include "../../platforms/esp32p4/main/EspWifiAdapter.h"
#include "../../platforms/esp32p4/main/EspVoiceAudio.h"
#include "../../platforms/esp32p4/main/EspMqttTransport.h"
#include "../../platforms/esp32p4/main/EspSensors.h"
#include "../../platforms/esp32p4/main/EspTimeSync.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#else
#include "../../platforms/simulator/DemoHaTransport.h"
#include "../../platforms/simulator/LwsTransport.h"
#include "../../platforms/simulator/SimUpdateAdapter.h"
#include "../../platforms/simulator/SimHttpFetcher.h"
#include "../../platforms/simulator/SimWifiAdapter.h"
#endif

#include "../config.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>

#ifdef ESP_PLATFORM
// AppStateController's own splash_hook — platforms/esp32p4/main/
// app_states.c's enter_splash_state(), declared locally here (rather
// than pulling in app_states.c's own header, which doesn't exist) so
// only this ESP-only block references a symbol that isn't compiled into
// the simulator binary at all.
extern "C" void enter_splash_state(bool has_saved_ssid);
#endif

#include <algorithm>
#include <cctype>
#include <memory>

#include <cjson/cJSON.h>

namespace {

// Provisional capacity (ADR-0003 in docs/migration/adr/): to be
// recalibrated with real load data once Phase 5+ pushes actual entity
// events through this queue.
constexpr size_t kUiDispatcherCapacity = 64;

app::platform::PortableLogger g_logger;
app::platform::PortableMonotonicClock g_clock;
app::platform::PortableWallClock g_wallClock;
app::platform::PortableMemoryMetrics g_memoryMetrics;
app::core::UiDispatcher g_taskRunner(kUiDispatcherCapacity, &g_logger);

app::application::Application g_application(g_logger, g_clock, g_taskRunner, g_memoryMetrics);

std::unique_ptr<app::application::DashboardController> g_dashboardController;
std::unique_ptr<app::application::SetupController> g_setupController;
std::unique_ptr<app::application::PairingController> g_pairingController;
std::unique_ptr<app::application::AppStateController> g_appStateController;
// Unlike the above, not created in app_cpp_create() — config isn't loaded
// yet at that point on either platform (see app_cpp_music_assistant_connect()'s
// doc comment) — so this stays null until that function is called.
std::unique_ptr<app::music_assistant::MaClient> g_maClient;
std::unique_ptr<app::music_assistant::MaController> g_maController; // with g_maClient
std::unique_ptr<app::ui::MusicPlayer> g_musicPlayer;                // made when first opened
std::unique_ptr<app::ui::MusicTrackPopover> g_trackPopover;         // with g_maController
app::ui::MusicPullGesture g_musicPull;

std::string ma_image_base() {
    const char* url = getConfig("musicassistant_url");
    return app::music_assistant::ma_http_base(url ? url : "");
}

app::ui::MusicPlayer* music_player() {
    if (!g_maController) return nullptr;
    if (!g_musicPlayer) {
        g_musicPlayer = std::make_unique<app::ui::MusicPlayer>(*g_maController, ma_image_base());
    }
    return g_musicPlayer.get();
}

#ifdef ESP_PLATFORM
platform_esp32p4::EspWifiAdapter g_wifiAdapter;
platform_esp32p4::FreeRtosBackgroundRunner g_backgroundRunner;
#else
platform_simulator::SimWifiAdapter g_wifiAdapter;
platform_simulator::ThreadBackgroundRunner g_backgroundRunner;
#endif
app::application::WifiController g_wifiController(
    g_wifiAdapter, g_backgroundRunner, g_taskRunner, g_clock,
    [](const std::string& ssid, const std::string& password) {
        updateConfig("wifi_ssid", ssid.c_str());
        updateConfig("wifi_password", password.c_str());
        saveConfig();
    },
    &g_logger);

#ifdef ESP_PLATFORM
platform_esp32p4::EspUpdateAdapter g_updateAdapter;
// HTTPS (TLS handshake + certificate bundle) needs a larger stack than the
// default background job.
platform_esp32p4::FreeRtosBackgroundRunner g_updateRunner(16384);
#else
platform_simulator::SimUpdateAdapter g_updateAdapter;
platform_simulator::ThreadBackgroundRunner g_updateRunner;
#endif
app::application::UpdateController g_updateController(
    g_updateAdapter, g_updateRunner, g_taskRunner, g_clock,
    [] { return std::string(HOMEPANE_OTA_MANIFEST_URL); },
    [] {
        const char* v = getConfig("ota_auto_check");
        return !(v && std::strcmp(v, "0") == 0); // default: on
    },
    &g_logger);

// Pictures from Home Assistant (camera tiles): fetched and decoded off the
// UI thread; HTTPS needs the larger stack.
#ifdef ESP_PLATFORM
platform_esp32p4::EspHttpFetcher g_httpFetcher;
platform_esp32p4::FreeRtosBackgroundRunner g_imageRunner(16384, true); // stack in PSRAM, never inline (streams)
platform_esp32p4::FreeRtosWorkerPool g_imageWorkers(app::media::ImageLoader::kMaxParallel, 16384); // picture loads
app::media::FileImageCache g_coverCache("/sdcard/MACACHE", 2000); // covers on the SD card
#else
platform_simulator::SimHttpFetcher g_httpFetcher;
platform_simulator::ThreadBackgroundRunner g_imageRunner;
app::media::FileImageCache g_coverCache("macache", 2000); // covers next to config.ini
#endif
app::media::ImageLoader g_imageLoader(g_httpFetcher, g_imageRunner, g_taskRunner, [] {
    // read on the UI thread (load() is called there), like all config access
    const char* url = getConfig("haas_url");
    const char* token = getConfig("haas_token");
    return app::media::ImageLoader::Server{url ? url : "", token ? token : ""};
});

#ifdef ESP_PLATFORM
platform_esp32p4::EspVoiceAudio g_voiceAudio;
#else
// The simulator has no audio path yet: the voice assistant reports that
// instead of pretending to listen.
class NoVoiceAudio final : public app::voice::IVoiceAudio {
public:
    bool start_capture(CaptureSink) override { return false; }
    void stop_capture() override {}
    bool play_url(const std::string&, PlaybackDone) override { return false; }
    void stop_playback() override {}
} g_voiceAudio;
#endif
app::voice::VoiceController g_voiceController(
    g_voiceAudio, g_clock,
    [] { return g_dashboardController ? g_dashboardController->ha_client() : nullptr; },
    [] {
        std::string base = getConfig("haas_url") ? getConfig("haas_url") : "";
        while (!base.empty() && base.back() == '/') base.pop_back();
        return base;
    },
    [] {
        const char* id = getConfig("voice_pipeline");
        return std::string(id ? id : "");
    },
    &g_logger);

// ── MQTT device (Track U U2) with the panel's extra hardware: probed at
// boot on the device, simulated in the simulator (docs/hardware/SENSORS.md,
// docs/plans/HARDWARE_CAPABILITIES.md) ────────────────────────────────────
#ifdef ESP_PLATFORM
platform_esp32p4::EspSensors g_sensors;
#else
app::sensors::SimulatedSensors g_sensors;
#endif
#ifdef ESP_PLATFORM
platform_esp32p4::EspMqttTransport g_mqttTransport;
#else
class NoMqttTransport final : public app::mqtt::IMqttTransport {
public:
    void set_sink(app::mqtt::IMqttSink*) override {}
    void connect(const app::mqtt::MqttConfig&) override {}
    void disconnect() override {}
    bool publish(const std::string&, const std::string&, bool, int) override { return false; }
    bool subscribe(const std::string&, int) override { return false; }
} g_mqttTransport;
#endif

// Stable for the device's life (docs/ARCHITECTURE.md §13.4): HA would
// create a new device whenever it changed. "smartboard_" is the project's
// former name, kept so existing panels stay the same device in HA.
std::string mqtt_node_id() {
#ifdef ESP_PLATFORM
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    char id[32];
    std::snprintf(id, sizeof(id), "smartboard_%02x%02x%02x", mac[3], mac[4], mac[5]);
    return id;
#else
    return "smartboard_sim";
#endif
}

std::string config_or(const char* key, const char* fallback) {
    const char* v = getConfig(key);
    return v && v[0] ? v : fallback;
}

app::mqtt::DeviceInfo mqtt_device_info() {
    app::mqtt::DeviceInfo d;
    d.node_id = mqtt_node_id();
    d.name = config_or("device_name", "Homepane");
    d.sw_version = g_updateAdapter.installed_version();
    d.suggested_area = config_or("room", ""); // used by HA only when it creates the device
    return d;
}

#ifdef ESP_PLATFORM
// The screen, from Home Assistant's "Display" switch (app_main.c).
extern "C" void app_display_set_on(bool on);
extern "C" bool app_display_is_on(void);

// esp_reset_reason() as HA shows it ("panic": the panel crashed).
const char* reset_reason_text(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "restart";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_EXT: return "reset pin";
        default: return "other";
    }
}
#endif

app::mqtt::MqttController g_mqttController(
    g_mqttTransport, g_clock, g_sensors, mqtt_device_info(),
    [] {
        app::mqtt::MqttSettings s;
        s.enabled = config_or("mqtt_enabled", "0") == "1";
        // Default broker: the Home Assistant host (Mosquitto add-on).
        std::string host = config_or("mqtt_host", "");
        if (host.empty()) {
            std::string url = config_or("haas_url", "");
            const size_t scheme = url.find("://");
            if (scheme != std::string::npos) url = url.substr(scheme + 3);
            host = url.substr(0, url.find_first_of(":/"));
        }
        s.host = host;
        s.port = static_cast<uint16_t>(std::atoi(config_or("mqtt_port", "1883").c_str()));
        s.username = config_or("mqtt_user", "");
        s.password = config_or("mqtt_password", "");
        return s;
    },
    [] {
        app::mqtt::Diagnostics d;
#ifdef ESP_PLATFORM
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) d.wifi_rssi_dbm = ap.rssi;
        d.uptime_s = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
        d.free_heap_kb = esp_get_free_heap_size() / 1024;
        d.internal_free_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
        d.internal_lowest_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
        d.internal_largest_kb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
        d.reset_reason = reset_reason_text(esp_reset_reason());
#else
        d.uptime_s = g_clock.now_ms() / 1000;
#endif
        return d;
    },
    &g_logger);

// The MQTT device in HA: its id for voice requests, its area = the room.
app::home_assistant::DeviceLink g_deviceLink(
    [] { return g_dashboardController ? g_dashboardController->ha_client() : nullptr; },
    [] { return g_mqttController.device().node_id; }, [] { return g_mqttController.connected(); },
    [] { return config_or("room_area_id", ""); }, &g_logger);
std::vector<app::home_assistant::Area> g_areas;

struct PipelineCache {
    app_cpp_voice_pipelines_state_t state = APP_CPP_VOICE_PIPELINES_LOADING;
    app::voice::PipelineList list;
} g_pipelines;

#ifndef ESP_PLATFORM
// LwsTransport needs periodic service() pumping distinct from
// WebSocketConnection::poll() (see LwsTransport.h) — DashboardController
// only ever sees the abstract IWebSocketTransport it was constructed
// with, so this raw pointer (captured before ownership moves into
// DashboardController) is the only way app_cpp_poll() can still reach it.
platform_simulator::LwsTransport* g_lwsTransportRaw = nullptr;
// Same reasoning, for SetupController's own independent connection.
platform_simulator::LwsTransport* g_setupLwsTransportRaw = nullptr;
// Same reasoning, for the pairing flow's own independent, one-shot
// connection — reassigned on every app_cpp_pairing_exchange_long_lived_token()
// call, see there.
platform_simulator::LwsTransport* g_pairingLwsTransportRaw = nullptr;
// Same reasoning, for the persistent Music Assistant connection.
platform_simulator::LwsTransport* g_maLwsTransportRaw = nullptr;
#endif

} // namespace

void app_cpp_post_to_ui(void (*fn)(void* ctx), void* ctx) {
    g_taskRunner.post(fn, ctx);
}

void app_cpp_create(void) {
    app::media::set_image_loader(&g_imageLoader);
    g_imageLoader.set_file_cache(&g_coverCache);
    g_imageLoader.set_logger(&g_logger);
#ifdef ESP_PLATFORM
    g_imageLoader.set_load_runner(&g_imageWorkers);
#endif
    // Config key c6_updates: "0" keeps the Wi-Fi co-processor's firmware
    // (chosen in the web flasher or in Settings -> Update).
    g_updateController.set_coprocessor_updates([] {
        const char* v = getConfig("c6_updates");
        return !(v && std::strcmp(v, "0") == 0);
    });
    // g_application is already constructed via static initialization.
#ifdef ESP_PLATFORM
    auto transport = std::make_unique<platform_esp32p4::EspWebSocketTransport>();
#else
    // HOMEPANE_DEMO=1: a simulated HA with every tile type (platforms/simulator/demo).
    std::unique_ptr<app::net::IWebSocketTransport> transport;
    if (const char* demo = std::getenv("HOMEPANE_DEMO"); demo && demo[0] == '1') {
        transport = std::make_unique<platform_simulator::DemoHaTransport>(HOMEPANE_DEMO_DIR);
    } else {
        auto lws = std::make_unique<platform_simulator::LwsTransport>();
        g_lwsTransportRaw = lws.get();
        transport = std::move(lws);
    }
#endif
    g_dashboardController = std::make_unique<app::application::DashboardController>(g_clock, g_wallClock, &g_logger,
                                                                                     std::move(transport));
    // HA's editor saves every card edit at once: by default the dashboard
    // follows once editing ended (a minute without changes)
    g_dashboardController->set_change_delay([] { return config_or("dashboard_updates", "settled") == "live" ? 0u : 60000u; });

#ifdef ESP_PLATFORM
    auto setupTransport = std::make_unique<platform_esp32p4::EspWebSocketTransport>();
#else
    std::unique_ptr<app::net::IWebSocketTransport> setupTransport;
    if (const char* demo = std::getenv("HOMEPANE_DEMO"); demo && demo[0] == '1') {
        setupTransport = std::make_unique<platform_simulator::DemoHaTransport>(HOMEPANE_DEMO_DIR);
    } else {
        auto lws = std::make_unique<platform_simulator::LwsTransport>();
        g_setupLwsTransportRaw = lws.get();
        setupTransport = std::move(lws);
    }
#endif
    g_setupController =
        std::make_unique<app::application::SetupController>(g_clock, &g_logger, std::move(setupTransport));

#ifdef ESP_PLATFORM
    g_appStateController = std::make_unique<app::application::AppStateController>(true, enter_splash_state);
#else
    g_appStateController = std::make_unique<app::application::AppStateController>(false, nullptr);
#endif
}

void app_cpp_enter_initial_state(void) {
    if (g_appStateController) g_appStateController->enter_initial_state();
}

// Keeps the exact same free-function C symbol names the legacy code
// already calls at runtime, not just at boot (app/views/setup.c's
// "Continue" button, app/views/initialization.c's manual-token-save,
// both platforms' OAuth long-lived-token exchange, and
// platforms/esp32p4/main/app_states.c's own splash-screen timer) — see
// AppStateController.h's class comment. No locking here: these forward
// straight into AppStateController, matching the existing contract that
// callers already hold whatever lock they need (e.g.
// websocket_client_esp.c calls enter_setup_state() only after acquiring
// lvgl_port_lock() itself).
extern "C" void enter_init_state(void) {
    if (g_appStateController) g_appStateController->enter_init_state();
}

extern "C" void enter_setup_state(void) {
    if (g_appStateController) g_appStateController->enter_setup_state();
}

extern "C" void enter_complete_state(void) {
    if (g_appStateController) g_appStateController->enter_complete_state();
}

void app_cpp_start(void) {
    app_cpp_datetime_apply_settings();
#ifdef ESP_PLATFORM
    g_sensors.start(); // after the BSP's I2C bus is up (display/touch)
#else
    // HOMEPANE_HW=none / no-haptics,ld2410,...: the simulator without parts of the hardware
    if (const char* hw = std::getenv("HOMEPANE_HW"))
        g_sensors.set_capabilities(app::hardware::apply_override(app::sensors::SimulatedSensors::planned_hardware(), hw));
#endif
    g_application.start();
    g_mqttController.set_on_restart([] { g_updateAdapter.restart(); });
#ifdef ESP_PLATFORM
    g_mqttController.set_display([](bool on) { app_display_set_on(on); }, [] { return app_display_is_on(); });
#else
    static bool sim_display_on = true; // the simulator has no backlight
    g_mqttController.set_display([](bool on) { sim_display_on = on; }, [] { return sim_display_on; });
#endif
    g_voiceController.set_device_id_provider([] { return g_deviceLink.device_id(); });
    g_deviceLink.set_default_room([] { return config_or("haas_dashboard", ""); },
                                  [](const app::home_assistant::Area& a) {
                                      updateConfig("room_area_id", a.id.c_str());
                                      updateConfig("room", a.name.c_str());
                                      saveConfig();
                                  });
    // the device lookup, at most once a day (the device list takes seconds to parse)
    g_deviceLink.set_cache(
        [] {
            char today[16];
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            std::strftime(today, sizeof(today), "%Y-%m-%d", &tm);
            const bool valid = tm.tm_year > 120 && config_or("ha_device_checked", "") == today &&
                               config_or("ha_device_node", "") == g_mqttController.device().node_id;
            return valid ? app::home_assistant::DeviceLink::Cached{config_or("ha_device_id", ""),
                                                                   config_or("ha_device_area", "")}
                         : app::home_assistant::DeviceLink::Cached{};
        },
        [](const app::home_assistant::DeviceLink::Cached& c) {
            char today[16];
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            std::strftime(today, sizeof(today), "%Y-%m-%d", &tm);
            updateConfig("ha_device_id", c.device_id.c_str());
            updateConfig("ha_device_area", c.area_id.c_str());
            updateConfig("ha_device_node", g_mqttController.device().node_id.c_str());
            updateConfig("ha_device_checked", tm.tm_year > 120 ? today : ""); // no clock yet: not cached
            saveConfig();
        });
    g_mqttController.apply_settings();
}

namespace {
uint32_t g_slowestStepMs = 0;
const char* g_slowestStep = "";
} // namespace

const char* app_cpp_slowest_step(uint32_t* ms, bool reset) {
    const char* name = g_slowestStep;
    if (ms) *ms = g_slowestStepMs;
    if (reset) {
        g_slowestStepMs = 0;
        g_slowestStep = "";
    }
    return name;
}

void app_cpp_poll(void) {
    // A step that blocks the UI for long is logged by name (the task
    // watchdog only says that "main" did not yield).
    auto timed = [](const char* name, auto&& step) {
        const uint64_t t0 = g_clock.now_ms();
        step();
        const uint64_t ms = g_clock.now_ms() - t0;
        if (ms > g_slowestStepMs) {
            g_slowestStepMs = static_cast<uint32_t>(ms);
            g_slowestStep = name;
        }
        if (ms >= 200) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "slow UI step: %s took %llu ms", name, static_cast<unsigned long long>(ms));
            g_logger.log(app::core::LogLevel::Warn, msg);
        }
    };
    timed("application", [] { g_application.poll(); });
#ifndef ESP_PLATFORM
    if (g_lwsTransportRaw) g_lwsTransportRaw->service();
    if (g_setupLwsTransportRaw) g_setupLwsTransportRaw->service();
    if (g_pairingLwsTransportRaw) g_pairingLwsTransportRaw->service();
    if (g_maLwsTransportRaw) g_maLwsTransportRaw->service();
#endif
    timed("dashboard", [] { if (g_dashboardController) g_dashboardController->poll(g_clock.now_ms()); });
    timed("setup", [] { if (g_setupController) g_setupController->poll(g_clock.now_ms()); });
    timed("pairing", [] { if (g_pairingController) g_pairingController->poll(g_clock.now_ms()); });
    timed("music assistant", [] { if (g_maClient) g_maClient->poll(g_clock.now_ms()); });
    timed("wifi", [] { g_wifiController.poll(g_clock.now_ms()); });
    timed("update", [] { g_updateController.poll(g_clock.now_ms()); });
    timed("voice", [] { g_voiceController.poll(g_clock.now_ms()); });
#ifndef ESP_PLATFORM
    {
        // time of day for the simulated light curve
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        g_sensors.set_seconds_of_day(static_cast<uint32_t>(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec));
    }
#endif
    timed("mqtt", [] { g_mqttController.poll(g_clock.now_ms()); });
    timed("device link", [] { g_deviceLink.poll(g_clock.now_ms()); });
}

void app_cpp_stop(void) {
    g_application.stop();
}

void app_cpp_dashboard_render(lv_obj_t* parent, const char* ws_url, const char* access_token,
                               const char* dashboard_url_path, void (*on_ready)(void), void (*on_error)(void)) {
    if (!g_dashboardController) return;
    app::dashboard::DashboardSession::set_rows_setting(config_or("dashboard_rows", "2"));
    g_dashboardController->render(
        parent, app::home_assistant::to_websocket_url(ws_url ? ws_url : ""), access_token ? access_token : "",
        dashboard_url_path ? dashboard_url_path : "", [on_ready] { if (on_ready) on_ready(); },
        [on_error] { if (on_error) on_error(); });
    // the connection exists from here on: app-level features attach to it
    app_cpp_doorbell_apply_settings();
    app_cpp_screensaver_apply_settings();
}

int app_cpp_dashboard_view_count(void) {
    auto* s = g_dashboardController ? g_dashboardController->session() : nullptr;
    return s ? s->view_count() : 0;
}

int app_cpp_dashboard_view_current(void) {
    auto* s = g_dashboardController ? g_dashboardController->session() : nullptr;
    return s ? s->current_view() : 0;
}

const char* app_cpp_dashboard_view_title(int index) {
    auto* s = g_dashboardController ? g_dashboardController->session() : nullptr;
    return s && index >= 0 && index < s->view_count() ? s->view_title(index).c_str() : "";
}

void app_cpp_dashboard_view_show(int index) {
    auto* s = g_dashboardController ? g_dashboardController->session() : nullptr;
    if (s) s->show_view(index);
}

void app_cpp_dashboard_home(void) {
    auto* s = g_dashboardController ? g_dashboardController->session() : nullptr;
    if (s) s->show_home();
}

bool app_cpp_ha_connected(void) {
    auto* c = g_dashboardController ? g_dashboardController->ha_client() : nullptr;
    return c && c->state() == app::home_assistant::HaClientState::Ready;
}

float app_cpp_room_temperature(void) {
    const auto r = g_sensors.read(g_clock.now_ms());
    return r.temperature_c ? *r.temperature_c : NAN;
}

float app_cpp_room_humidity(void) {
    const auto r = g_sensors.read(g_clock.now_ms());
    return r.humidity_pct ? *r.humidity_pct : NAN;
}

bool app_cpp_hw_has(app_cpp_hw_t what) {
    const auto c = g_sensors.capabilities();
    switch (what) {
        case APP_CPP_HW_HAPTICS: return c.haptics;
        case APP_CPP_HW_PRESENCE: return c.presence;
        case APP_CPP_HW_CLIMATE: return c.temperature;
    }
    return false;
}

const char* app_cpp_hw_parts(void) {
    static std::string parts;
    parts = g_sensors.capabilities().parts;
    return parts.c_str();
}

void app_cpp_dashboard_reload(void) {
    if (g_dashboardController) g_dashboardController->reload();
}

void app_cpp_dashboard_release_ui(void) {
    if (g_dashboardController) g_dashboardController->release_ui();
}

void app_cpp_dashboard_disconnect(void) {
    if (g_dashboardController) g_dashboardController->disconnect();
}

void app_cpp_setup_fetch_panels(const char* ws_url, const char* access_token,
                                 void (*on_success)(const app_cpp_panel_option_t* options, int count),
                                 void (*on_error)(void)) {
    if (!g_setupController) return;
    g_setupController->fetch_panels(
        app::home_assistant::to_websocket_url(ws_url ? ws_url : ""), access_token ? access_token : "",
        [on_success](std::vector<app::application::PanelOption> parsed) {
            if (!on_success) return;
            // Bridges the C++ vector<PanelOption> to a flat C array for the
            // duration of this single callback invocation only (see the
            // app_cpp_panel_option_t doc comment in app_facade.h).
            std::vector<app_cpp_panel_option_t> options;
            options.reserve(parsed.size());
            for (const auto& option : parsed) {
                options.push_back(app_cpp_panel_option_t{option.title.c_str(), option.url_path.c_str()});
            }
            on_success(options.empty() ? nullptr : options.data(), static_cast<int>(options.size()));
        },
        [on_error] { if (on_error) on_error(); });
}

void app_cpp_pairing_exchange_long_lived_token(const char* ws_url, const char* access_token,
                                                const char* client_name,
                                                void (*on_success)(const char* long_lived_token),
                                                void (*on_error)(const char* message)) {
#ifdef ESP_PLATFORM
    auto transport = std::make_unique<platform_esp32p4::EspWebSocketTransport>();
#else
    auto transport = std::make_unique<platform_simulator::LwsTransport>();
    g_pairingLwsTransportRaw = transport.get();
#endif
    // A fresh controller per attempt (see PairingController.h) — replaces
    // (and thereby tears down) any previous attempt's connection.
    g_pairingController =
        std::make_unique<app::application::PairingController>(g_clock, &g_logger, std::move(transport));
    // Callers pass the user-facing haas_url ("http://host:8123"), not a
    // websocket URL — connecting to that directly fails the WS handshake.
    g_pairingController->exchange_long_lived_token(
        app::home_assistant::to_websocket_url(ws_url ? ws_url : ""), access_token ? access_token : "", client_name ? client_name : "",
        [on_success](const std::string& token) { if (on_success) on_success(token.c_str()); },
        [on_error](const std::string& message) { if (on_error) on_error(message.c_str()); });
}

namespace {
void (*g_maTokenIssued)(const char* token) = nullptr; // kept for a reconnect
}

void app_cpp_music_assistant_connect(const char* ws_url, const char* username, const char* password,
                                      const char* cached_token, void (*on_token_issued)(const char* token)) {
    g_maTokenIssued = on_token_issued;
    if (g_maClient) {
        g_logger.log(app::core::LogLevel::Warn,
                      "app_cpp_music_assistant_connect() called again — already connected, ignoring");
        return;
    }

#ifdef ESP_PLATFORM
    auto transport = std::make_unique<platform_esp32p4::EspWebSocketTransport>();
#else
    auto transport = std::make_unique<platform_simulator::LwsTransport>();
    g_maLwsTransportRaw = transport.get();
#endif

    app::music_assistant::MaCredentials credentials;
    credentials.username = username ? username : "";
    credentials.password = password ? password : "";
    credentials.device_name = "LVGL Panel";

    g_maClient = std::make_unique<app::music_assistant::MaClient>(
        // MA's replies are large (a search ~100 KB, a queue item ~5 KB): up to 1 MB (PSRAM)
        std::make_unique<app::net::WebSocketConnection>("music_assistant", std::move(transport), g_clock, 1024 * 1024,
                                                        32, app::net::BackoffPolicy(1000, 30000), &g_logger),
        g_clock,
        credentials, cached_token ? cached_token : "", &g_logger);
    g_maClient->set_on_token_issued(
        [on_token_issued](const std::string& token) { if (on_token_issued) on_token_issued(token.c_str()); });
    g_trackPopover.reset(); // it listens to the old controller
    g_maController = std::make_unique<app::music_assistant::MaController>(
        *g_maClient, [] { return static_cast<double>(std::time(nullptr)); });
    g_maController->set_player(config_or("ma_player_id", ""));
    g_maClient->set_on_state_change([](app::music_assistant::MaClientState st) { g_maController->on_state(st); });
    g_maClient->set_on_event([](const std::string& type, const std::string& id, const std::string& data) {
        g_maController->on_event(type, id, data);
    });
    g_trackPopover = std::make_unique<app::ui::MusicTrackPopover>(
        *g_maController, ma_image_base(),
        [] { return (g_musicPlayer && g_musicPlayer->is_open()) || app_cpp_screensaver_open(); },
        [] { app_cpp_music_open(); });
    g_musicPull.set_player([] { return music_player(); });
    g_musicPull.set_at_top([] {
        auto* session = g_dashboardController ? g_dashboardController->session() : nullptr;
        return session && session->at_top();
    });
    g_musicPull.set_on_pull_start([] {
        if (auto* session = g_dashboardController ? g_dashboardController->session() : nullptr) session->settle_top();
    });
    g_maClient->start(ws_url ? ws_url : "");
}

bool app_cpp_wifi_scan(void (*on_done)(const app_cpp_wifi_network_t* networks, int count, void* user), void* user) {
    return g_wifiController.scan([on_done, user](const std::vector<app::net::WifiNetwork>& nets) {
        if (!on_done) return;
        std::vector<app_cpp_wifi_network_t> out(nets.size());
        for (size_t i = 0; i < nets.size(); ++i) {
            std::snprintf(out[i].ssid, sizeof(out[i].ssid), "%s", nets[i].ssid.c_str());
            out[i].rssi = nets[i].rssi;
            out[i].secured = nets[i].secured;
        }
        on_done(out.data(), static_cast<int>(out.size()), user);
    });
}

void app_cpp_wifi_connect(const char* ssid, const char* password,
                          void (*on_done)(bool ok, const char* message, void* user), void* user) {
    g_wifiController.connect(ssid ? ssid : "", password ? password : "",
                             [on_done, user](bool ok, const std::string& message) {
                                 if (on_done) on_done(ok, message.c_str(), user);
                             });
}

void app_cpp_wifi_get_status(app_cpp_wifi_status_t* out) {
    if (!out) return;
    app::net::WifiLinkStatus st = g_wifiController.status();
    switch (st.state) {
        case app::net::WifiLinkState::Connecting: out->state = APP_CPP_WIFI_CONNECTING; break;
        case app::net::WifiLinkState::Connected: out->state = APP_CPP_WIFI_CONNECTED; break;
        case app::net::WifiLinkState::Failed: out->state = APP_CPP_WIFI_FAILED; break;
        default: out->state = APP_CPP_WIFI_DISCONNECTED; break;
    }
    std::snprintf(out->ssid, sizeof(out->ssid), "%s", st.ssid.c_str());
    std::snprintf(out->ip, sizeof(out->ip), "%s", st.ip.c_str());
}

void app_cpp_update_get_status(app_cpp_update_status_t* out) {
    if (!out) return;
    const app::application::UpdateStatus& st = g_updateController.status();
    switch (st.state) {
        case app::application::UpdateState::Checking: out->state = APP_CPP_UPDATE_CHECKING; break;
        case app::application::UpdateState::UpToDate: out->state = APP_CPP_UPDATE_UP_TO_DATE; break;
        case app::application::UpdateState::Available: out->state = APP_CPP_UPDATE_AVAILABLE; break;
        case app::application::UpdateState::Installing: out->state = APP_CPP_UPDATE_INSTALLING; break;
        case app::application::UpdateState::Restarting: out->state = APP_CPP_UPDATE_RESTARTING; break;
        case app::application::UpdateState::Error: out->state = APP_CPP_UPDATE_ERROR; break;
        default: out->state = APP_CPP_UPDATE_IDLE; break;
    }
    // Before the first poll()/check() the versions are not read yet.
    std::string installed = st.installed_version.empty() ? g_updateAdapter.installed_version() : st.installed_version;
    std::string coproc = st.coprocessor_version.empty() ? g_updateAdapter.coprocessor_version() : st.coprocessor_version;
    std::snprintf(out->installed_version, sizeof(out->installed_version), "%s", installed.c_str());
    std::snprintf(out->coprocessor_version, sizeof(out->coprocessor_version), "%s", coproc.c_str());
    std::snprintf(out->available_version, sizeof(out->available_version), "%s", st.available_version.c_str());
    std::snprintf(out->message, sizeof(out->message), "%s", st.message.c_str());
    out->progress = st.progress;
}

bool app_cpp_update_check(void) { return g_updateController.check(); }

bool app_cpp_update_install(void) { return g_updateController.install(); }

void app_cpp_update_set_listener(void (*on_change)(void* user), void* user) {
    if (!on_change) {
        g_updateController.set_listener(nullptr);
        return;
    }
    g_updateController.set_listener([on_change, user](const app::application::UpdateStatus&) { on_change(user); });
}

void app_cpp_voice_button(void) { g_voiceController.button(); }

bool app_cpp_voice_configure(bool enabled, const char* wake_word) {
    g_voiceController.set_enabled(enabled);
    const std::string model = enabled && wake_word ? wake_word : "";
    return g_voiceAudio.set_wake_word(model, [] { g_voiceController.request_start(); });
}

bool app_cpp_voice_is_enabled(void) { return g_voiceController.enabled(); }

void app_cpp_voice_set_sound(const char* name) { g_voiceController.set_earcon_style(earcon_style_from_name(name)); }

void app_cpp_voice_preview_sound(const char* name) { g_voiceController.preview_earcon(earcon_style_from_name(name)); }

void app_cpp_voice_set_volume(int percent) { g_voiceAudio.set_volume(percent); }

void app_cpp_voice_fetch_pipelines(void (*done)(void* user), void* user) {
    g_pipelines.state = APP_CPP_VOICE_PIPELINES_LOADING;
    g_voiceController.fetch_pipelines(
        [done, user](bool ok, const app::voice::PipelineList& list, const std::string&) {
            g_pipelines.list = list;
            if (!ok) g_pipelines.state = APP_CPP_VOICE_PIPELINES_UNAVAILABLE;
            else g_pipelines.state = list.pipelines.empty() ? APP_CPP_VOICE_PIPELINES_NONE : APP_CPP_VOICE_PIPELINES_OK;
            if (done) done(user);
        });
}

app_cpp_voice_pipelines_state_t app_cpp_voice_pipelines_state(void) { return g_pipelines.state; }

int app_cpp_voice_pipeline_count(void) { return static_cast<int>(g_pipelines.list.pipelines.size()); }

const char* app_cpp_voice_pipeline_name(int i) {
    if (i < 0 || i >= app_cpp_voice_pipeline_count()) return "";
    return g_pipelines.list.pipelines[i].name.c_str();
}

const char* app_cpp_voice_pipeline_id(int i) {
    if (i < 0 || i >= app_cpp_voice_pipeline_count()) return "";
    return g_pipelines.list.pipelines[i].id.c_str();
}

int app_cpp_voice_pipeline_preferred(void) {
    for (int i = 0; i < app_cpp_voice_pipeline_count(); ++i) {
        if (g_pipelines.list.pipelines[i].id == g_pipelines.list.preferred_id) return i;
    }
    return -1;
}

int app_cpp_setting_set(const char* key, bool on, char* side_effects, size_t len) {
    if (side_effects && len) side_effects[0] = '\0';
    if (!key) return 0;
    static const app::settings::SettingRules rules(app::settings::panel_dependencies());
    auto changed = rules.set(
        key, on,
        [](const std::string& k) {
            const char* v = getConfig(k.c_str());
            return v && std::strcmp(v, "1") == 0;
        },
        [](const std::string& k, bool value) { updateConfig(k.c_str(), value ? "1" : "0"); });
    saveConfig();
    std::string joined;
    for (const auto& k : changed) joined += (joined.empty() ? "" : ",") + k;
    if (side_effects && len) std::snprintf(side_effects, len, "%s", joined.c_str());
    return static_cast<int>(changed.size());
}

void app_cpp_voice_cancel(void) { g_voiceController.cancel(); }

float app_cpp_voice_level(void) { return g_voiceController.level(); }

float app_cpp_voice_output_level(void) { return g_voiceController.output_level(); }

void app_cpp_voice_bands(float out[3]) { g_voiceController.bands(out); }

void app_cpp_voice_get_status(app_cpp_voice_status_t* out) {
    if (!out) return;
    const app::voice::VoiceStatus& st = g_voiceController.status();
    out->state = static_cast<app_cpp_voice_state_t>(st.state);
    std::snprintf(out->transcript, sizeof(out->transcript), "%s", st.transcript.c_str());
    std::snprintf(out->response, sizeof(out->response), "%s", st.response.c_str());
    std::snprintf(out->error, sizeof(out->error), "%s", st.error.c_str());
}

void app_cpp_voice_set_listener(void (*on_change)(void* user), void* user) {
    if (!on_change) {
        g_voiceController.set_listener(nullptr);
        return;
    }
    g_voiceController.set_listener([on_change, user](const app::voice::VoiceStatus&) { on_change(user); });
}

void app_cpp_mqtt_apply_settings(void) { g_mqttController.apply_settings(); }

bool app_cpp_mqtt_connected(void) { return g_mqttController.connected(); }

const char* app_cpp_mqtt_node_id(void) { return g_mqttController.device().node_id.c_str(); }

namespace {
std::vector<app::application::PanelOption> g_dashboards;
app::core::CancellationSource g_dashboardsFetch;
} // namespace

void app_cpp_factory_reset(void) {
    app_cpp_dashboard_disconnect();
    // in memory first: nothing can write the old values back before the restart
    releaseConfig();
#ifdef ESP_PLATFORM
    std::remove(get_config_path());
#else
    std::remove(CONFIG_FILE_PATH);
#endif
#ifdef ESP_PLATFORM
    esp_wifi_restore();
#endif
}

#ifdef ESP_PLATFORM
extern "C" void app_display_apply_brightness(void);
#endif

void app_cpp_display_brightness_apply(void) {
#ifdef ESP_PLATFORM
    app_display_apply_brightness();
#endif
}

bool app_cpp_restart(void) {
#ifdef ESP_PLATFORM
    g_updateAdapter.restart();
    return true;
#else
    return false;
#endif
}

void app_cpp_ha_revoke_token(void (*done)(bool ok, void* user), void* user) {
    auto* client = g_dashboardController ? g_dashboardController->ha_client() : nullptr;
    if (!client || client->state() != app::home_assistant::HaClientState::Ready) {
        if (done) done(false, user);
        return;
    }
    // The token list marks the one this connection signed in with.
    g_dashboardsFetch = client->send_command(R"({"type":"auth/refresh_tokens"})",
                                             [client, done, user](const app::home_assistant::ResultMessage& r) {
        std::string id;
        if (r.success) {
            cJSON* list = cJSON_Parse(r.result_json.c_str());
            const cJSON* t = nullptr;
            cJSON_ArrayForEach(t, list) {
                const cJSON* current = cJSON_GetObjectItemCaseSensitive(t, "is_current");
                const cJSON* tid = cJSON_GetObjectItemCaseSensitive(t, "id");
                if (cJSON_IsTrue(current) && cJSON_IsString(tid)) id = tid->valuestring;
            }
            cJSON_Delete(list);
        }
        if (id.empty()) {
            if (done) done(false, user);
            return;
        }
        cJSON* req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "type", "auth/delete_refresh_token");
        cJSON_AddStringToObject(req, "refresh_token_id", id.c_str());
        char* text = cJSON_PrintUnformatted(req);
        const std::string json = text ? text : "";
        cJSON_free(text);
        cJSON_Delete(req);
        g_dashboardsFetch = client->send_command(json, [done, user](const app::home_assistant::ResultMessage& d) {
            if (done) done(d.success, user);
        });
    });
}

void app_cpp_dashboards_fetch(void (*done)(void* user, bool ok), void* user) {
    auto* client = g_dashboardController ? g_dashboardController->ha_client() : nullptr;
    if (!client) {
        if (done) done(user, false);
        return;
    }
    g_dashboardsFetch = client->send_command(R"({"type":"get_panels"})",
                                             [done, user](const app::home_assistant::ResultMessage& r) {
        if (r.success) g_dashboards = app::application::PanelListParser::parse(r.result_json);
        if (done) done(user, r.success);
    });
}

int app_cpp_dashboards_count(void) { return static_cast<int>(g_dashboards.size()); }

const char* app_cpp_dashboards_title(int i) {
    return i >= 0 && i < app_cpp_dashboards_count() ? g_dashboards[i].title.c_str() : "";
}

const char* app_cpp_dashboards_path(int i) {
    return i >= 0 && i < app_cpp_dashboards_count() ? g_dashboards[i].url_path.c_str() : "";
}

void app_cpp_rooms_fetch(void (*done)(void* user, bool ok), void* user) {
    g_deviceLink.fetch_areas([done, user](bool ok, const std::vector<app::home_assistant::Area>& areas) {
        g_areas = areas;
        if (done) done(user, ok);
    });
}

int app_cpp_room_count(void) { return static_cast<int>(g_areas.size()); }

const char* app_cpp_room_name(int i) { return i >= 0 && i < app_cpp_room_count() ? g_areas[i].name.c_str() : ""; }

bool app_cpp_room_denied(void) { return g_deviceLink.area_denied(); }

const char* app_cpp_room_id(int i) { return i >= 0 && i < app_cpp_room_count() ? g_areas[i].id.c_str() : ""; }

void app_cpp_room_set(const char* area_id, const char* name) {
    // Copies first: callers may pass getConfig() pointers, which updateConfig() invalidates.
    const std::string id = area_id && area_id[0] ? area_id : app::home_assistant::DeviceLink::kNoRoom;
    const std::string room = name ? name : "";
    updateConfig("room_area_id", id.c_str());
    updateConfig("room", room.c_str());
    saveConfig();
    g_deviceLink.room_changed();
}

// ── Doorbell (app/cpp/doorbell, Doorbell artboard) ──

namespace {
app::doorbell::DoorbellController g_doorbell;
// Created on first use and never destroyed: its LVGL objects must not be
// deleted from a static destructor after LVGL is gone.
app::ui::DoorbellOverlay& doorbell_overlay() {
    static auto* overlay = new app::ui::DoorbellOverlay();
    return *overlay;
}

void doorbell_ring() {
    const auto& st = g_doorbell.settings();
    app::ui::DoorbellOverlay::OpenFn open;
    if (!app::doorbell::DoorbellController::opener_service(st.opener).empty()) {
        open = [](std::function<void(bool)> done) { g_doorbell.open(std::move(done)); };
    }
    const bool already = doorbell_overlay().is_open();
    doorbell_overlay().show(st.camera, std::move(open));
    if (!already) app_cpp_voice_preview_sound("chime");
}

std::string config_text(const char* key) {
    const char* v = getConfig(key);
    return v ? v : "";
}

struct EntityChoice {
    std::string id, name;
};
std::vector<EntityChoice> g_entityChoices;
app::core::CancellationSource g_entityFetch;
} // namespace

void app_cpp_doorbell_apply_settings(void) {
    app::doorbell::DoorbellController::Settings st{config_text("doorbell_trigger"), config_text("doorbell_camera"),
                                                   config_text("doorbell_unlock")};
    g_doorbell.attach(g_dashboardController ? g_dashboardController->coordinator() : nullptr,
                      g_dashboardController ? g_dashboardController->commands() : nullptr, std::move(st),
                      doorbell_ring);
}

void app_cpp_doorbell_test(void) { doorbell_ring(); }

void app_cpp_entities_fetch(const char* domains, void (*done)(void* user, bool ok), void* user) {
    app_cpp_entities_fetch_with(domains, nullptr, done, user);
}

void app_cpp_entities_fetch_with(const char* domains, const char* attribute, void (*done)(void* user, bool ok),
                                 void* user) {
    auto* client = g_dashboardController ? g_dashboardController->ha_client() : nullptr;
    if (!client) {
        if (done) done(user, false);
        return;
    }
    // "event,input_button,button" -> {"event.", "input_button.", "button."}
    std::vector<std::string> prefixes;
    std::string list = domains ? domains : "";
    for (size_t start = 0; start <= list.size();) {
        const size_t comma = list.find(',', start);
        const std::string d = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!d.empty()) prefixes.push_back(d + ".");
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    g_entityFetch.cancel();
    const std::string needed = attribute ? attribute : "";
    g_entityFetch = client->send_command(R"({"type":"get_states"})", [prefixes, needed, done, user](
                                                                         const app::home_assistant::ResultMessage& r) {
        g_entityChoices.clear();
        if (r.success) {
            cJSON* root = cJSON_Parse(r.result_json.c_str());
            const cJSON* st = nullptr;
            cJSON_ArrayForEach(st, root) {
                const cJSON* id = cJSON_GetObjectItemCaseSensitive(st, "entity_id");
                if (!cJSON_IsString(id)) continue;
                const std::string eid = id->valuestring;
                bool match = false;
                for (const auto& p : prefixes) match = match || eid.rfind(p, 0) == 0;
                if (!match) continue;
                const cJSON* attrs = cJSON_GetObjectItemCaseSensitive(st, "attributes");
                if (!needed.empty() && !cJSON_GetObjectItemCaseSensitive(attrs, needed.c_str())) continue;
                const cJSON* name = cJSON_GetObjectItemCaseSensitive(attrs, "friendly_name");
                g_entityChoices.push_back({eid, cJSON_IsString(name) ? name->valuestring : eid});
            }
            cJSON_Delete(root);
            std::sort(g_entityChoices.begin(), g_entityChoices.end(),
                      [](const EntityChoice& a, const EntityChoice& b) { return a.name < b.name; });
        }
        if (done) done(user, r.success);
    }, 15000);
}

int app_cpp_entity_count(void) { return static_cast<int>(g_entityChoices.size()); }
const char* app_cpp_entity_id(int i) { return i >= 0 && i < app_cpp_entity_count() ? g_entityChoices[i].id.c_str() : ""; }
const char* app_cpp_entity_name(int i) {
    return i >= 0 && i < app_cpp_entity_count() ? g_entityChoices[i].name.c_str() : "";
}

// ── Screensaver with the HA Eventcalendar (app/cpp/ui/Screensaver) ──

extern "C" const unsigned char builtin_screensaver_default[];
extern "C" const size_t builtin_screensaver_default_size;

namespace {
constexpr const char* kDefaultEventSensor = "sensor.ha_eventcalendar_kalenderanzeige";
constexpr const char* kBuiltinPicture = "builtin:screensaver_default";
app::eventcalendar::EventFeed g_eventFeed;
lv_timer_t* g_screensaverTimer = nullptr;
void (*g_screensaverOnIdle)(void) = nullptr;
app::core::CancellationSource g_backgroundResolve;

// the weather of the dashboard's first weather card, for the screensaver
std::string g_weatherEntity;
app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle g_weatherHandle;
app::ui::Screensaver::Weather g_weather;

struct MediaChoice {
    std::string id, title;
};
std::vector<MediaChoice> g_media;
bool g_mediaFolderFound = false;
app::core::CancellationSource g_mediaFetch;

app::ui::Screensaver& screensaver() {
    static auto* s = new app::ui::Screensaver(); // never destroyed, like the doorbell overlay
    return *s;
}

int screensaver_after_s() {
    const char* v = getConfig("screensaver_after");
    return v && v[0] ? std::atoi(v) : 120; // design guide: two minutes without presence
}

int screen_off_setting_s() {
    const char* v = getConfig("screen_timeout");
    return v && v[0] ? std::atoi(v) : 600;
}

bool events_enabled() { return config_or("eventcalendar_enabled", "1") != "0"; }

void watch_weather() {
    auto* session = g_dashboardController ? g_dashboardController->session() : nullptr;
    auto* coordinator = g_dashboardController ? g_dashboardController->coordinator() : nullptr;
    if (!session || !coordinator) return;
    std::string found;
    for (const auto& view : session->model().views) {
        auto take = [&found](const app::dashboard::DashboardCard& c) {
            if (found.empty() && c.entity_id.rfind("weather.", 0) == 0) found = c.entity_id;
        };
        for (const auto& section : view.sections) for (const auto& c : section.cards) take(c);
        for (const auto& c : view.cards) take(c);
    }
    if (found == g_weatherEntity) return;
    g_weatherEntity = found;
    g_weather = {};
    g_weatherHandle.reset();
    if (found.empty()) return;
    g_weatherHandle = coordinator->observe(found, [](const app::home_assistant::EntityState& st) {
        auto t = st.attributes.find("temperature");
        g_weather.known = st.availability == app::home_assistant::Availability::available && t != st.attributes.end();
        g_weather.condition = st.state;
        g_weather.temperature = t != st.attributes.end() ? std::strtod(t->second.c_str(), nullptr) : 0;
    });
    coordinator->deliver_current(found, g_weatherHandle);
}

// The background: the chosen HA media picture (resolved to a signed URL
// each time; valid a day), else the built-in one.
void resolve_background() {
    const std::string chosen = config_or("screensaver_picture", "");
    auto* client = g_dashboardController ? g_dashboardController->ha_client() : nullptr;
    g_backgroundResolve.cancel();
    if (chosen.empty() || !client) {
        screensaver().set_background(kBuiltinPicture);
        return;
    }
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "media_source/resolve_media");
    cJSON_AddStringToObject(req, "media_content_id", chosen.c_str());
    char* text = cJSON_PrintUnformatted(req);
    const std::string json = text ? text : "";
    cJSON_free(text);
    cJSON_Delete(req);
    g_backgroundResolve = client->send_command(json, [](const app::home_assistant::ResultMessage& r) {
        std::string url;
        if (r.success) {
            cJSON* root = cJSON_Parse(r.result_json.c_str());
            const cJSON* u = cJSON_GetObjectItemCaseSensitive(root, "url");
            if (cJSON_IsString(u)) url = u->valuestring;
            cJSON_Delete(root);
        }
        screensaver().set_background(url.empty() ? kBuiltinPicture : url); // gone from HA: the built-in one
    });
}

void screensaver_check(lv_timer_t*) {
    const int after = screensaver_after_s();
    if (after <= 0 || screensaver().is_open() || doorbell_overlay().is_open()) return;
    if (!g_dashboardController || !g_dashboardController->session()) return; // only over the dashboard
    if (lv_display_get_inactive_time(nullptr) < static_cast<uint32_t>(after) * 1000) return;
    // back to the start under it: what was open is closed when the panel is used again
    if (g_musicPlayer) g_musicPlayer->close_now();
    if (g_screensaverOnIdle) g_screensaverOnIdle();
    app_cpp_screensaver_show();
}

// The room's player's track while it plays.
app::ui::Screensaver::NowPlaying now_playing() {
    app::ui::Screensaver::NowPlaying np;
    const auto* q = g_maController && g_maController->ready() ? g_maController->queue() : nullptr;
    if (!q || q->state != "playing" || !q->current) return np;
    np.playing = true;
    np.title = q->current->title;
    np.artist = q->current->artist;
    np.cover_url = app::music_assistant::ma_image_url(ma_image_base(), q->current->image, 56);
    return np;
}

void show_screensaver(const std::string& start_event) {
    watch_weather();
    app::ui::Screensaver::Sources src;
    src.feed = &g_eventFeed;
    src.events_enabled = events_enabled();
    src.room_temperature = [] { return app_cpp_room_temperature(); };
    src.weather = [] { return g_weather; };
    src.start_event = start_event;
    src.now_playing = now_playing;
    const bool own = !config_or("screensaver_picture", "").empty();
    src.background = own ? std::string() : kBuiltinPicture; // an HA picture follows when resolved
    screensaver().show(std::move(src));
    if (own) resolve_background();
}
} // namespace

void app_cpp_screensaver_apply_settings(void) {
    static bool registered = false;
    if (!registered) {
        app::media::register_builtin_picture("screensaver_default", builtin_screensaver_default,
                                             builtin_screensaver_default_size);
        registered = true;
    }
    const char* sensor = getConfig("eventcalendar_entity");
    const std::string entity = !events_enabled() ? "" : sensor ? sensor : kDefaultEventSensor;
    g_eventFeed.attach(g_dashboardController ? g_dashboardController->coordinator() : nullptr,
                       g_dashboardController ? g_dashboardController->ha_client() : nullptr, entity,
                       [] { screensaver().feed_changed(); });
    if (!g_screensaverTimer) g_screensaverTimer = lv_timer_create(screensaver_check, 1000, nullptr);
}

void app_cpp_screensaver_show(void) { show_screensaver(""); }
void app_cpp_screensaver_set_on_idle(void (*on_idle)(void)) { g_screensaverOnIdle = on_idle; }
bool app_cpp_screensaver_open(void) { return screensaver().is_open(); }
void app_cpp_screensaver_show_event(const char* id) { show_screensaver(id ? id : ""); }

int app_cpp_event_count(void) { return static_cast<int>(g_eventFeed.feed().events.size()); }
const char* app_cpp_event_id(int i) {
    return i >= 0 && i < app_cpp_event_count() ? g_eventFeed.feed().events[i].id.c_str() : "";
}
const char* app_cpp_event_title(int i) {
    return i >= 0 && i < app_cpp_event_count() ? g_eventFeed.feed().events[i].title.c_str() : "";
}
const char* app_cpp_event_when(int i) {
    static std::string text;
    if (i < 0 || i >= app_cpp_event_count()) return "";
    const auto& e = g_eventFeed.feed().events[i];
    text = app::ui::Screensaver::chip_text(e) + " · " + app::ui::Screensaver::time_text(e);
    const std::string d = app::ui::Screensaver::distance_text(e);
    if (!d.empty()) text += " · " + d;
    return text.c_str();
}

void app_cpp_media_pictures_fetch(void (*done)(void* user, bool ok), void* user) {
    auto* client = g_dashboardController ? g_dashboardController->ha_client() : nullptr;
    g_media.clear();
    g_mediaFolderFound = false;
    if (!client) {
        if (done) done(user, false);
        return;
    }
    // "My media"; a folder "Homepane" in it (or "Homepane", the project's
    // former name), when there is one, instead
    struct Step {
        static std::string browse(const std::string& id) {
            return R"({"type":"media_source/browse_media","media_content_id":")" + id + R"("})";
        }
    };
    g_mediaFetch.cancel();
    g_mediaFetch = client->send_command(
        Step::browse("media-source://media_source/local/."), [done, user, client](const app::home_assistant::ResultMessage& r) {
            if (!r.success) {
                if (done) done(user, false);
                return;
            }
            auto collect = [](const std::string& json, std::string* folder) {
                cJSON* root = cJSON_Parse(json.c_str());
                const cJSON* child = nullptr;
                cJSON_ArrayForEach(child, cJSON_GetObjectItemCaseSensitive(root, "children"))
                {
                    const cJSON* cls = cJSON_GetObjectItemCaseSensitive(child, "media_class");
                    const cJSON* id = cJSON_GetObjectItemCaseSensitive(child, "media_content_id");
                    const cJSON* title = cJSON_GetObjectItemCaseSensitive(child, "title");
                    if (!cJSON_IsString(cls) || !cJSON_IsString(id) || !cJSON_IsString(title)) continue;
                    std::string t = title->valuestring;
                    std::string lower;
                    for (unsigned char c : t) lower += static_cast<char>(std::tolower(c));
                    if (folder && std::string(cls->valuestring) == "directory" &&
                        (lower == "homepane" || (lower == "smartboard" && folder->empty()))) {
                        *folder = id->valuestring;
                    } else if (std::string(cls->valuestring) == "image") {
                        g_media.push_back({id->valuestring, t});
                    }
                }
                cJSON_Delete(root);
            };
            std::string folder;
            collect(r.result_json, &folder);
            if (folder.empty()) {
                if (done) done(user, true);
                return;
            }
            g_mediaFolderFound = true;
            g_media.clear(); // the folder's pictures only
            g_mediaFetch = client->send_command(Step::browse(folder), [done, user, collect](
                                                                          const app::home_assistant::ResultMessage& r2) {
                if (r2.success) collect(r2.result_json, nullptr);
                if (done) done(user, r2.success);
            });
        });
}

bool app_cpp_media_folder_found(void) { return g_mediaFolderFound; }

void app_cpp_media_folder_create(void (*done)(void* user, int status), void* user) {
    // HA has no call for an empty folder, but its upload creates the target
    // folder: the folder starts with the built-in picture.
    app::media::image_loader()->upload(
        "media-source://media_source/local/Homepane", "homepane-default.jpg",
        std::string(reinterpret_cast<const char*>(builtin_screensaver_default), builtin_screensaver_default_size),
        [done, user](int status) {
            if (done) done(user, status);
        });
}

int app_cpp_media_picture_count(void) { return static_cast<int>(g_media.size()); }
const char* app_cpp_media_picture_id(int i) { return i >= 0 && i < app_cpp_media_picture_count() ? g_media[i].id.c_str() : ""; }
const char* app_cpp_media_picture_title(int i) {
    return i >= 0 && i < app_cpp_media_picture_count() ? g_media[i].title.c_str() : "";
}

// Is the HA Eventcalendar add-on there? Its display sensor (the chosen one,
// else the default name) is watched directly — cheap; only when it does not
// show up within 3 s (renamed) the large state list is searched for a
// sensor with "api_version".
namespace {
app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle g_detectHandle;
lv_timer_t* g_detectTimer = nullptr;
std::function<void(bool, const std::string&)> g_detectDone;

void detect_finish(bool found, const std::string& entity) {
    g_detectHandle.reset();
    if (g_detectTimer) {
        lv_timer_delete(g_detectTimer);
        g_detectTimer = nullptr;
    }
    auto done = std::move(g_detectDone);
    g_detectDone = nullptr;
    if (done) done(found, entity);
}
} // namespace

void app_cpp_eventcalendar_detect(void (*done)(void* user, bool found, const char* entity), void* user) {
    auto* coordinator = g_dashboardController ? g_dashboardController->coordinator() : nullptr;
    // a detection still running (its page is gone) ends quietly, without its callback
    g_detectDone = nullptr;
    g_detectHandle.reset();
    if (g_detectTimer) {
        lv_timer_delete(g_detectTimer);
        g_detectTimer = nullptr;
    }
    g_detectDone = [done, user](bool found, const std::string& entity) {
        if (done) done(user, found, entity.c_str());
    };
    if (!coordinator) {
        detect_finish(false, "");
        return;
    }
    const std::string entity = config_or("eventcalendar_entity", kDefaultEventSensor);
    g_detectHandle = coordinator->observe(entity, [](const app::home_assistant::EntityState& st) {
        if (st.availability == app::home_assistant::Availability::loading) return;
        // finished after this callback: the subscription must not end inside it
        if (st.attributes.count("api_version")) lv_async_call([](void*) { detect_finish(true, config_or("eventcalendar_entity", kDefaultEventSensor)); }, nullptr);
    });
    coordinator->deliver_current(entity, g_detectHandle);
    g_detectTimer = lv_timer_create(
        [](lv_timer_t*) {
            lv_timer_delete(g_detectTimer);
            g_detectTimer = nullptr;
            g_detectHandle.reset();
            // not under its name: search all sensors (seconds on the panel, rare)
            app_cpp_entities_fetch_with("sensor", "api_version", [](void*, bool ok) {
                const bool found = ok && app_cpp_entity_count() > 0;
                detect_finish(found, found ? app_cpp_entity_id(0) : "");
            }, nullptr);
        },
        3000, nullptr);
}

// ── Date and time format ──

void app_cpp_datetime_apply_settings(void) {
    app::core::set_datetime_format(app::core::datetime_format_from(getConfig("time_format"), getConfig("date_format")));
}

int app_cpp_time_source(char* server, size_t size) {
#ifdef ESP_PLATFORM
    return esp_time_sync_source(server, size);
#else
    (void)server;
    (void)size;
    return -1; // the computer keeps its own time
#endif
}

void app_cpp_time_sync_now(void) {
#ifdef ESP_PLATFORM
    esp_time_sync_start();
#endif
}

void app_cpp_clock_text(char* buf, size_t size) {
    if (!buf || !size) return;
    const std::string t = app::core::time_text(static_cast<double>(time(nullptr)));
    std::snprintf(buf, size, "%s", t.c_str());
}

void app_cpp_datetime_example(char* buf, size_t size) {
    if (!buf || !size) return;
    const struct tm now = app::core::local_tm(static_cast<double>(time(nullptr)));
    const std::string t = app::core::date_text(now, app::core::DateStyle::weekday_day_month) + " · " +
                          app::core::date_text(now, app::core::DateStyle::numeric) + " · " +
                          app::core::time_text(now);
    std::snprintf(buf, size, "%s", t.c_str());
}

int app_cpp_screen_off_after_s(void) {
    const int off = screen_off_setting_s();
    if (off <= 0) return 0;
    const int saver = screensaver_after_s();
    return saver > off ? saver : off; // never before the screensaver
}

// ── Music Assistant: player UI and settings ──

void app_cpp_music_pull_zone(lv_obj_t* zone) {
    g_musicPull.set_zone(zone);
    if (auto* p = music_player()) p->prepare(); // the first pull starts at once
}

void app_cpp_music_open(void) {
    if (auto* p = music_player()) p->open();
}

bool app_cpp_music_ready(void) { return g_maController && g_maController->ready(); }

int app_cpp_music_player_count(void) {
    return g_maController ? static_cast<int>(g_maController->store().players().size()) : 0;
}

const char* app_cpp_music_player_id(int i) {
    return i >= 0 && i < app_cpp_music_player_count() ? g_maController->store().players()[i].id.c_str() : "";
}

const char* app_cpp_music_player_name(int i) {
    return i >= 0 && i < app_cpp_music_player_count() ? g_maController->store().players()[i].name.c_str() : "";
}

void app_cpp_music_set_player(const char* id) {
    updateConfig("ma_player_id", id ? id : "");
    saveConfig();
    if (g_maController) g_maController->set_player(id ? id : "");
}

int app_cpp_music_cache_files(void) { return static_cast<int>(g_coverCache.files()); }

void app_cpp_music_cache_clear(void) {
    g_imageRunner.run([] { g_coverCache.clear(); }); // files on the SD card: not on the UI thread
}

int app_cpp_music_status(void) {
    if (!g_maClient) return APP_CPP_MUSIC_OFF;
    switch (g_maClient->state()) {
        case app::music_assistant::MaClientState::Ready:
            return APP_CPP_MUSIC_READY;
        case app::music_assistant::MaClientState::AuthFailed:
            return APP_CPP_MUSIC_AUTH_FAILED;
        case app::music_assistant::MaClientState::Stopped:
            return APP_CPP_MUSIC_OFF;
        default:
            return APP_CPP_MUSIC_CONNECTING;
    }
}

// New credentials: the player UI, the controller and the connection are made anew.
void app_cpp_music_reconnect(void) {
    g_musicPlayer.reset();
    g_trackPopover.reset();
    if (g_maClient) g_maClient->stop();
    g_maController.reset();
    g_maClient.reset();
    const std::string url = config_or("musicassistant_url", ""), user = config_or("musicassistant_username", ""),
                      pass = config_or("musicassistant_password", "");
    if (url.empty() || user.empty() || pass.empty()) return;
    updateConfig("musicassistant_token", ""); // the old session is for the old credentials
    app_cpp_music_assistant_connect(url.c_str(), user.c_str(), pass.c_str(), "", g_maTokenIssued);
}

const char* app_cpp_music_user(void) {
    return g_maController ? g_maController->user().c_str() : "";
}

void app_cpp_music_refresh(void) {
    if (g_maController) g_maController->refresh();
}

void app_cpp_music_set_listener(void (*on_change)(void* user), void* user) {
    static int id = 0;
    if (g_maController && id) g_maController->remove_listener(id);
    id = 0;
    if (g_maController && on_change) id = g_maController->add_listener([on_change, user] { on_change(user); });
}
