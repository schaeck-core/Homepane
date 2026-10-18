#include "MqttController.h"

#include <cmath>
#include <cstdio>

using app::core::LogLevel;

namespace app::mqtt {

namespace {

EntityDef sensor(const char* id, const char* name, const char* device_class, const char* unit, int precision,
                 const char* icon = "")
{
    EntityDef e;
    e.component = "sensor";
    e.object_id = id;
    e.name = name;
    e.device_class = device_class;
    e.unit = unit;
    e.state_class = "measurement";
    e.precision = precision;
    e.icon = icon;
    return e;
}

EntityDef diagnostic(EntityDef e)
{
    e.entity_category = "diagnostic";
    return e;
}

} // namespace

MqttController::MqttController(IMqttTransport& transport, app::core::IMonotonicClock& clock,
                               app::sensors::ISensorSource& source, DeviceInfo device, SettingsProvider settings,
                               DiagnosticsProvider diagnostics, app::core::ILogger* logger)
    : transport_(transport), clock_(clock), source_(source), device_(std::move(device)), settings_(std::move(settings)),
      diagnostics_(std::move(diagnostics)), logger_(logger)
{
    EntityDef presence;
    presence.component = "binary_sensor";
    presence.object_id = "presence";
    presence.name = "Presence";
    presence.device_class = "occupancy";
    entities_.push_back(presence);
    entities_.push_back(sensor("target_count", "Targets", "", "", 0, "mdi:account-multiple"));
    for (const char* axis : {"x", "y", "z"}) {
        EntityDef e = sensor((std::string("target_1_") + axis).c_str(), (std::string("Target 1 ") + char(axis[0] - 32)).c_str(),
                             "distance", "m", 2);
        e.enabled_by_default = false;
        entities_.push_back(e);
    }
    entities_.push_back(sensor("distance", "Distance", "distance", "m", 1));
    {
        EntityDef e = sensor("speed", "Speed", "speed", "m/s", 1);
        e.enabled_by_default = false;
        entities_.push_back(e);
    }
    entities_.push_back(sensor("temperature", "Temperature", "temperature", "°C", 1));
    entities_.push_back(sensor("humidity", "Humidity", "humidity", "%", 0));
    entities_.push_back(sensor("illuminance", "Illuminance", "illuminance", "lx", 0));
    entities_.push_back(sensor("press_force", "Press force", "", "%", 0, "mdi:gesture-tap"));

    EntityDef press;
    press.component = "event";
    press.object_id = "press";
    press.name = "Press";
    press.device_class = "button";
    press.event_types = {"press", "long_press"};
    entities_.push_back(press);

    EntityDef haptic;
    haptic.component = "button";
    haptic.object_id = "haptic_test";
    haptic.name = "Haptic test";
    haptic.icon = "mdi:vibrate";
    haptic.entity_category = "config";
    entities_.push_back(haptic);

    EntityDef restart;
    restart.component = "button";
    restart.object_id = "restart";
    restart.name = "Restart";
    restart.device_class = "restart";
    restart.entity_category = "config";
    entities_.push_back(restart);

    EntityDef display;
    display.component = "switch";
    display.object_id = "display";
    display.name = "Display";
    display.icon = "mdi:monitor";
    entities_.push_back(display);

    EntityDef simulated;
    simulated.component = "binary_sensor";
    simulated.object_id = "simulated_data";
    simulated.name = "Simulated sensor data";
    simulated.icon = "mdi:test-tube";
    simulated.entity_category = "diagnostic";
    entities_.push_back(simulated);

    entities_.push_back(diagnostic(sensor("wifi_signal", "WiFi signal", "signal_strength", "dBm", 0)));
    entities_.push_back(diagnostic(sensor("uptime", "Uptime", "duration", "s", 0)));
    entities_.push_back(diagnostic(sensor("free_heap", "Free memory", "data_size", "kB", 0)));
    entities_.push_back(diagnostic(sensor("internal_free", "Internal RAM free", "data_size", "kB", 0, "mdi:memory")));
    entities_.push_back(
        diagnostic(sensor("internal_lowest", "Internal RAM lowest", "data_size", "kB", 0, "mdi:memory")));
    entities_.push_back(
        diagnostic(sensor("internal_largest", "Internal RAM largest block", "data_size", "kB", 0, "mdi:memory")));
    EntityDef reset;
    reset.component = "sensor";
    reset.object_id = "reset_reason";
    reset.name = "Last restart reason";
    reset.icon = "mdi:restart-alert";
    reset.entity_category = "diagnostic";
    entities_.push_back(reset);

    // How often values may/must go out: position data at most once a second
    // and only after 10 cm, climate rarely; every value at least every 5
    // minutes. (HA records every state change.)
    auto set = [this](const char* id, uint32_t min_ms, float threshold) {
        channels_[id].min_interval_ms = min_ms;
        channels_[id].threshold = threshold;
    };
    set("target_1_x", 1000, 0.1f);
    set("target_1_y", 1000, 0.1f);
    set("target_1_z", 1000, 0.1f);
    set("distance", 1000, 0.1f);
    set("speed", 1000, 0.1f);
    set("temperature", 10000, 0.1f);
    set("humidity", 10000, 1.0f);
    set("illuminance", 5000, 5.0f);
    set("press_force", 200, 5.0f);
}

void MqttController::log(LogLevel level, const std::string& line)
{
    if (logger_) logger_->log(level, ("MQTT: " + line).c_str());
}

void MqttController::apply_settings()
{
    const MqttSettings s = settings_ ? settings_() : MqttSettings{};
    if (started_) {
        transport_.disconnect();
        started_ = connected_ = false;
    }
    if (!s.enabled || s.host.empty()) {
        log(LogLevel::Info, "disabled");
        return;
    }
    MqttConfig cfg;
    cfg.host = s.host;
    cfg.port = s.port;
    cfg.username = s.username;
    cfg.password = s.password;
    cfg.client_id = device_.node_id;
    cfg.will_topic = availability_topic(device_.node_id);
    cfg.will_payload = "offline";
    transport_.set_sink(this);
    transport_.connect(cfg);
    started_ = true;
    log(LogLevel::Info, "connecting to " + s.host + ":" + std::to_string(s.port) + " as " + device_.node_id);
}

void MqttController::announce()
{
    for (const auto& e : entities_) {
        transport_.publish(discovery_topic(device_.node_id, e), discovery_payload(device_, e), true, 1);
    }
    transport_.publish(availability_topic(device_.node_id), "online", true, 1);
    // Fresh values for everything after (re)announcing.
    for (auto& [id, ch] : channels_) ch.has_last = false;
    last_diag_ms_ = 0;
    announced_ = true;
    log(LogLevel::Info, "announced " + std::to_string(entities_.size()) + " entities");
}

void MqttController::on_mqtt_connected()
{
    connected_ = true;
    transport_.subscribe("homepane/" + device_.node_id + "/+/set");
    transport_.subscribe("homeassistant/status"); // HA's birth message: announce again
    announce();
}

void MqttController::on_mqtt_disconnected(const char* reason)
{
    connected_ = false;
    log(LogLevel::Warn, std::string("disconnected: ") + (reason ? reason : ""));
}

void MqttController::on_mqtt_message(const std::string& topic, const std::string& payload)
{
    if (topic == "homeassistant/status") {
        if (payload == "online") announce(); // HA restarted: it forgot non-retained state
        return;
    }
    if (topic == command_topic(device_.node_id, "haptic_test")) {
        log(LogLevel::Info, "haptic test");
        source_.haptic(1); // DRV2605L effect 1: strong click
    } else if (topic == command_topic(device_.node_id, "display")) {
        const bool on = payload == "ON";
        log(LogLevel::Info, std::string("display ") + (on ? "on" : "off") + " from Home Assistant");
        if (on_display_) on_display_(on);
        if (display_on_) publish_value("display", display_on_() ? "ON" : "OFF", clock_.now_ms(), true);
    } else if (topic == command_topic(device_.node_id, "restart")) {
        log(LogLevel::Warn, "restart requested from Home Assistant");
        if (on_restart_) on_restart_();
    }
}

void MqttController::publish_value(const std::string& object_id, const std::string& value, uint64_t now, bool force)
{
    Channel& ch = channels_[object_id];
    const bool due = !ch.has_last || now - ch.last_ms >= ch.max_interval_ms;
    if (!force && !due && (value == ch.last || now - ch.last_ms < ch.min_interval_ms)) return;
    if (transport_.publish(state_topic(device_.node_id, object_id), value, true)) {
        ch.last = value;
        ch.has_last = true;
        ch.last_ms = now;
    }
}

void MqttController::publish_number(const std::string& object_id, float value, int decimals, uint64_t now)
{
    Channel& ch = channels_[object_id];
    const bool due = !ch.has_last || now - ch.last_ms >= ch.max_interval_ms;
    if (!due) {
        if (now - ch.last_ms < ch.min_interval_ms) return;
        if (std::fabs(value - ch.last_num) < ch.threshold) return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    if (transport_.publish(state_topic(device_.node_id, object_id), buf, true)) {
        ch.last = buf;
        ch.last_num = value;
        ch.has_last = true;
        ch.last_ms = now;
    }
}

void MqttController::poll(uint64_t now_ms)
{
    transport_.dispatch_pending();
    if (!connected_ || !announced_) return;

    const app::sensors::SensorReadings r = source_.read(now_ms);
    if (r.presence) publish_value("presence", *r.presence ? "ON" : "OFF", now_ms);
    if (r.target_count) publish_value("target_count", std::to_string(*r.target_count), now_ms);
    if (r.target_x_m) publish_number("target_1_x", *r.target_x_m, 2, now_ms);
    if (r.target_y_m) publish_number("target_1_y", *r.target_y_m, 2, now_ms);
    if (r.target_z_m) publish_number("target_1_z", *r.target_z_m, 2, now_ms);
    if (r.distance_m) publish_number("distance", *r.distance_m, 2, now_ms);
    if (r.speed_mps) publish_number("speed", *r.speed_mps, 2, now_ms);
    if (r.presence && !*r.presence) {
        // Nobody there: positions are unknown rather than the last value.
        for (const char* id : {"target_1_x", "target_1_y", "target_1_z", "distance"}) {
            Channel& ch = channels_[id];
            if (!ch.has_last || ch.last != "None") {
                if (transport_.publish(state_topic(device_.node_id, id), "None", true)) {
                    ch.last = "None";
                    ch.has_last = true;
                    ch.last_ms = now_ms;
                }
            }
        }
    }
    if (r.temperature_c) publish_number("temperature", *r.temperature_c, 1, now_ms);
    if (r.humidity_pct) publish_number("humidity", *r.humidity_pct, 0, now_ms);
    if (r.illuminance_lx) publish_number("illuminance", *r.illuminance_lx, 0, now_ms);
    if (r.press_force_pct) publish_number("press_force", *r.press_force_pct, 0, now_ms);
    source_.take_gestures([this](app::sensors::PressGesture g) {
        const char* type = g == app::sensors::PressGesture::LongPress ? "long_press" : "press";
        transport_.publish(state_topic(device_.node_id, "press"), std::string(R"({"event_type":")") + type + "\"}",
                           false);
    });
    publish_value("simulated_data", source_.simulated() ? "ON" : "OFF", now_ms);
    if (display_on_) publish_value("display", display_on_() ? "ON" : "OFF", now_ms);

    if (diagnostics_ && (last_diag_ms_ == 0 || now_ms - last_diag_ms_ >= 60000)) {
        last_diag_ms_ = now_ms;
        const Diagnostics d = diagnostics_();
        if (d.wifi_rssi_dbm) publish_value("wifi_signal", std::to_string(d.wifi_rssi_dbm), now_ms, true);
        publish_value("uptime", std::to_string(d.uptime_s), now_ms, true);
        publish_value("free_heap", std::to_string(d.free_heap_kb), now_ms, true);
        if (d.internal_free_kb) {
            publish_value("internal_free", std::to_string(d.internal_free_kb), now_ms, true);
            publish_value("internal_lowest", std::to_string(d.internal_lowest_kb), now_ms, true);
            publish_value("internal_largest", std::to_string(d.internal_largest_kb), now_ms, true);
        }
        if (!d.reset_reason.empty()) publish_value("reset_reason", d.reset_reason, now_ms, true);
    }
}

} // namespace app::mqtt
