#ifndef APP_CPP_MQTT_MQTTCONTROLLER_H
#define APP_CPP_MQTT_MQTTCONTROLLER_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../sensors/SensorReadings.h"
#include "HaDiscovery.h"
#include "IMqttTransport.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace app::mqtt {

struct MqttSettings {
    bool enabled = false;
    std::string host;
    uint16_t port = 1883;
    std::string username, password;
};

// Panel diagnostics published next to the sensors.
struct Diagnostics {
    int wifi_rssi_dbm = 0; // 0 = unknown / Ethernet
    uint64_t uptime_s = 0;
    uint32_t free_heap_kb = 0; // all of it, PSRAM included
    // Internal RAM, the scarce kind (Wi-Fi's DMA buffers need it; below
    // ~16 KB in one block the panel can crash). 0 = unknown (simulator).
    uint32_t internal_free_kb = 0;
    uint32_t internal_lowest_kb = 0;  // the lowest since the start
    uint32_t internal_largest_kb = 0; // the largest free block
    std::string reset_reason;         // why the panel last started ("panic": a crash)
};

// The panel as an MQTT device in Home Assistant (CPP_MIGRATION_PLAN.md
// §19.5/19.6, Track U U2): connects, announces availability (+ last will),
// publishes discovery for every entity (again whenever HA restarts), sends
// sensor values when they change (thresholds and minimum intervals keep the
// broker quiet) and handles commands (haptic test, restart). UI thread only.
class MqttController final : public IMqttSink {
public:
    using SettingsProvider = std::function<MqttSettings()>;
    using DiagnosticsProvider = std::function<Diagnostics()>;

    MqttController(IMqttTransport& transport, app::core::IMonotonicClock& clock, app::sensors::ISensorSource& source,
                   DeviceInfo device, SettingsProvider settings, DiagnosticsProvider diagnostics,
                   app::core::ILogger* logger = nullptr);

    // (Re)reads the settings and connects, or disconnects when disabled.
    void apply_settings();
    void poll(uint64_t now_ms);

    void set_on_restart(std::function<void()> cb) { on_restart_ = std::move(cb); }
    bool connected() const { return connected_; }
    const DeviceInfo& device() const { return device_; }
    const std::vector<EntityDef>& entities() const { return entities_; }

    // IMqttSink
    void on_mqtt_connected() override;
    void on_mqtt_disconnected(const char* reason) override;
    void on_mqtt_message(const std::string& topic, const std::string& payload) override;

private:
    struct Channel {
        std::string last;      // last published value
        float last_num = 0;
        bool has_last = false;
        uint64_t last_ms = 0;
        uint32_t min_interval_ms = 0; // not more often than this
        uint32_t max_interval_ms = 300000; // re-send at least this often
        float threshold = 0;   // numeric change needed (0 = any change)
    };

    void announce();
    void publish_value(const std::string& object_id, const std::string& value, uint64_t now, bool force = false);
    void publish_number(const std::string& object_id, float value, int decimals, uint64_t now);
    void log(app::core::LogLevel level, const std::string& line);

    IMqttTransport& transport_;
    app::core::IMonotonicClock& clock_;
    app::sensors::ISensorSource& source_;
    DeviceInfo device_;
    SettingsProvider settings_;
    DiagnosticsProvider diagnostics_;
    app::core::ILogger* logger_;
    std::function<void()> on_restart_;

    std::vector<EntityDef> entities_;
    std::map<std::string, Channel> channels_;
    bool started_ = false;
    bool connected_ = false;
    bool announced_ = false;
    uint64_t last_diag_ms_ = 0;
};

} // namespace app::mqtt

#endif // APP_CPP_MQTT_MQTTCONTROLLER_H
