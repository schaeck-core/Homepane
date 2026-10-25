// MQTT side of the panel: HA discovery payloads, MqttController
// (announce, change-based publishing, commands) and the simulated sensors.

#include "../../app/cpp/mqtt/HaDiscovery.h"
#include "../../app/cpp/mqtt/MqttController.h"
#include "../../app/cpp/sensors/NullSensors.h"
#include "../../app/cpp/sensors/SimulatedSensors.h"

#include <cjson/cJSON.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace app::mqtt;
using app::sensors::NullSensors;
using app::sensors::SimulatedSensors;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

class FakeClock final : public app::core::IMonotonicClock {
public:
    uint64_t now_ms() const override { return now; }
    uint64_t now = 1000;
};

struct Publish {
    std::string topic, payload;
    bool retain;
};

class FakeTransport final : public IMqttTransport {
public:
    void set_sink(IMqttSink* s) override { sink = s; }
    void connect(const MqttConfig& c) override { config = c; ++connects; }
    void disconnect() override { ++disconnects; }
    bool publish(const std::string& t, const std::string& p, bool retain, int) override
    {
        published.push_back({t, p, retain});
        return true;
    }
    bool subscribe(const std::string& f, int) override
    {
        subscriptions.push_back(f);
        return true;
    }
    size_t count(const std::string& prefix) const
    {
        size_t n = 0;
        for (const auto& p : published) n += p.topic.rfind(prefix, 0) == 0;
        return n;
    }
    // The last discovery config of an entity: "" = removed, "-" = none sent.
    std::string discovery(const std::string& component, const std::string& id) const
    {
        std::string v = "-";
        for (const auto& p : published) {
            if (p.topic == "homeassistant/" + component + "/homepane_a1b2c3/" + id + "/config") v = p.payload;
        }
        return v;
    }
    IMqttSink* sink = nullptr;
    MqttConfig config;
    int connects = 0, disconnects = 0;
    std::vector<Publish> published;
    std::vector<std::string> subscriptions;
};

DeviceInfo device()
{
    DeviceInfo d;
    d.node_id = "homepane_a1b2c3";
    d.name = "Homepane Living room";
    d.sw_version = "0.1.0.42";
    d.suggested_area = "Wohnzimmer";
    return d;
}

void test_discovery_payload()
{
    EntityDef e;
    e.component = "sensor";
    e.object_id = "temperature";
    e.name = "Temperature";
    e.device_class = "temperature";
    e.unit = "°C";
    e.state_class = "measurement";
    e.precision = 1;
    const std::string json = discovery_payload(device(), e);
    cJSON* root = cJSON_Parse(json.c_str());
    expect(root != nullptr, "the discovery payload is valid JSON");
    auto str = [&](cJSON* o, const char* k) {
        cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
        return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
    };
    expect(str(root, "unique_id") == "homepane_a1b2c3_temperature", "unique_id = node id + object id");
    expect(str(root, "state_topic") == "homepane/homepane_a1b2c3/temperature/state", "state topic");
    expect(str(root, "availability_topic") == "homepane/homepane_a1b2c3/status", "availability topic");
    expect(str(root, "unit_of_measurement") == "°C" && str(root, "device_class") == "temperature", "unit and class");
    cJSON* dev = cJSON_GetObjectItemCaseSensitive(root, "device");
    expect(str(dev, "suggested_area") == "Wohnzimmer" && str(dev, "sw_version") == "0.1.0.42",
           "device block: area for HA to assign, firmware version");
    cJSON* ids = cJSON_GetObjectItemCaseSensitive(dev, "identifiers");
    expect(cJSON_GetArraySize(ids) == 1 && std::string(cJSON_GetArrayItem(ids, 0)->valuestring) == "homepane_a1b2c3",
           "one device for all entities (identifier = node id)");
    cJSON_Delete(root);
    expect(discovery_topic("n", e) == "homeassistant/sensor/n/temperature/config", "discovery topic");
}

struct Fixture {
    FakeClock clock;
    FakeTransport transport;
    SimulatedSensors sensors{7};
    MqttSettings settings;
    bool restarted = false;
    MqttController mqtt{transport, clock, sensors, device(), [this] { return settings; },
                        [] { return Diagnostics{-55, 3600, 512}; }};
    Fixture()
    {
        settings.enabled = true;
        settings.host = "192.0.2.20";
        settings.username = "homepane";
        mqtt.set_on_restart([this] { restarted = true; });
    }
};

void test_connect_and_announce()
{
    Fixture fx;
    fx.mqtt.apply_settings();
    expect(fx.transport.connects == 1 && fx.transport.config.client_id == "homepane_a1b2c3",
           "connects with the node id as client id");
    expect(fx.transport.config.will_topic == "homepane/homepane_a1b2c3/status" &&
               fx.transport.config.will_payload == "offline",
           "last will marks the device offline when the connection drops");
    fx.transport.sink->on_mqtt_connected();
    expect(fx.transport.count("homeassistant/") == fx.mqtt.entities().size(), "one retained discovery config per entity");
    expect(fx.transport.discovery("sensor", "temperature").size() > 1 &&
               fx.transport.discovery("binary_sensor", "presence").size() > 1,
           "the simulated hardware's sensors are announced");
    expect(fx.transport.discovery("sensor", "illuminance").empty(), "no light sensor yet: illuminance is removed");
    bool online = false;
    for (const auto& p : fx.transport.published) online |= p.topic == "homepane/homepane_a1b2c3/status" && p.payload == "online" && p.retain;
    expect(online, "availability: online (retained)");
    expect(fx.transport.subscriptions.size() == 2, "subscribes to its command topics and HA's status");

    const size_t before = fx.transport.count("homeassistant/");
    fx.transport.sink->on_mqtt_message("homeassistant/status", "online");
    expect(fx.transport.count("homeassistant/") == 2 * before, "HA restarted: discovery is sent again");
}

void test_change_based_publishing()
{
    Fixture fx;
    fx.mqtt.apply_settings();
    fx.transport.sink->on_mqtt_connected();
    fx.transport.published.clear();
    fx.mqtt.poll(fx.clock.now);
    const size_t first = fx.transport.published.size();
    expect(first > 8, "first poll sends every value");
    fx.transport.published.clear();
    fx.clock.now += 100;
    fx.mqtt.poll(fx.clock.now);
    expect(fx.transport.count("homepane/homepane_a1b2c3/temperature") == 0,
           "100 ms later: temperature is not sent again");
    // 10 minutes of polling every 100 ms: far fewer messages than polls x values.
    size_t n = 0;
    for (int i = 0; i < 6000; ++i) {
        fx.clock.now += 100;
        fx.transport.published.clear();
        fx.mqtt.poll(fx.clock.now);
        n += fx.transport.published.size();
    }
    expect(n < 6000 * 4, "values go out on change, not on every poll (" + std::to_string(n) + " messages in 10 min)");
}

void test_display_switch()
{
    Fixture fx;
    bool on = true;
    fx.mqtt.set_display([&on](bool v) { on = v; }, [&on] { return on; });
    fx.mqtt.apply_settings();
    fx.transport.sink->on_mqtt_connected();
    bool has_switch = false;
    for (const auto& p : fx.transport.published) {
        has_switch |= p.topic == "homeassistant/switch/homepane_a1b2c3/display/config" &&
                      p.payload.find("\"command_topic\":\"homepane/homepane_a1b2c3/display/set\"") != std::string::npos &&
                      p.payload.find("\"state_topic\":\"homepane/homepane_a1b2c3/display/state\"") != std::string::npos;
    }
    expect(has_switch, "a Display switch with command and state topic");
    fx.transport.published.clear();
    fx.transport.sink->on_mqtt_message("homepane/homepane_a1b2c3/display/set", "OFF");
    bool reported = false;
    for (const auto& p : fx.transport.published) reported |= p.topic == "homepane/homepane_a1b2c3/display/state" && p.payload == "OFF";
    expect(!on && reported, "OFF turns the screen off and reports it");
    fx.transport.sink->on_mqtt_message("homepane/homepane_a1b2c3/display/set", "ON");
    expect(on, "ON turns it on again");
}

void test_internal_ram_diagnostics()
{
    FakeClock clock;
    FakeTransport transport;
    SimulatedSensors sensors{7};
    MqttSettings settings;
    settings.enabled = true;
    settings.host = "192.0.2.20";
    Diagnostics d{-55, 3600, 17000};
    d.internal_free_kb = 102;
    d.internal_lowest_kb = 88;
    d.internal_largest_kb = 36;
    d.reset_reason = "panic";
    MqttController mqtt{transport, clock, sensors, device(), [&] { return settings; }, [&] { return d; }};
    mqtt.apply_settings();
    transport.sink->on_mqtt_connected();
    mqtt.poll(clock.now);
    auto value = [&](const std::string& id) {
        std::string v;
        for (const auto& p : transport.published) {
            if (p.topic == "homepane/homepane_a1b2c3/" + id + "/state") v = p.payload;
        }
        return v;
    };
    expect(value("internal_free") == "102" && value("internal_lowest") == "88" && value("internal_largest") == "36",
           "internal RAM: free, lowest since the start, largest block (kB)");
    expect(value("reset_reason") == "panic", "why the panel last started (a crash: panic)");

    Fixture sim; // the simulator knows no internal RAM: nothing is sent for it
    sim.mqtt.apply_settings();
    sim.transport.sink->on_mqtt_connected();
    sim.mqtt.poll(sim.clock.now);
    expect(sim.transport.count("homepane/homepane_a1b2c3/internal_free/state") == 0, "unknown values are not sent");
}

void test_commands_and_disable()
{
    Fixture fx;
    fx.mqtt.apply_settings();
    fx.transport.sink->on_mqtt_connected();
    fx.transport.sink->on_mqtt_message("homepane/homepane_a1b2c3/haptic_test/set", "PRESS");
    expect(fx.sensors.last_haptic() == 1, "the haptic test button triggers a haptic effect");
    fx.transport.sink->on_mqtt_message("homepane/homepane_a1b2c3/restart/set", "PRESS");
    expect(fx.restarted, "the restart button restarts the panel");

    fx.settings.enabled = false;
    fx.mqtt.apply_settings();
    expect(fx.transport.disconnects == 1, "disabling MQTT disconnects");
}

struct EmptyFixture {
    FakeClock clock;
    FakeTransport transport;
    NullSensors sensors;
    MqttSettings settings;
    MqttController mqtt{transport, clock, sensors, device(), [this] { return settings; },
                        [] { return Diagnostics{-55, 3600, 512}; }};
    EmptyFixture()
    {
        settings.enabled = true;
        settings.host = "192.0.2.20";
        mqtt.apply_settings();
        transport.sink->on_mqtt_connected();
    }
};

void test_without_extra_hardware()
{
    EmptyFixture fx;
    for (const char* id : {"temperature", "humidity", "illuminance", "press_force", "target_count", "distance"}) {
        expect(fx.transport.discovery("sensor", id).empty(), std::string("no hardware: ") + id + " is removed from HA");
    }
    expect(fx.transport.discovery("binary_sensor", "presence").empty() &&
               fx.transport.discovery("event", "press").empty() && fx.transport.discovery("button", "haptic_test").empty(),
           "no hardware: presence, press events and the haptic test are removed");
    expect(fx.transport.discovery("binary_sensor", "simulated_data").empty(), "real data: no 'simulated' flag");
    expect(fx.transport.discovery("button", "restart").size() > 1 && fx.transport.discovery("sensor", "uptime").size() > 1,
           "the panel's own entities stay");
    fx.transport.published.clear();
    fx.mqtt.poll(fx.clock.now);
    expect(fx.transport.count("homepane/homepane_a1b2c3/temperature") == 0 &&
               fx.transport.count("homepane/homepane_a1b2c3/simulated_data") == 0,
           "no sensor values are sent");
}

void test_hardware_found_after_connect()
{
    Fixture fx;
    app::hardware::Capabilities probing; // still probing: nothing yet
    fx.sensors.set_capabilities(probing);
    fx.mqtt.apply_settings();
    fx.transport.sink->on_mqtt_connected();
    expect(fx.transport.discovery("sensor", "temperature").empty(), "while probing, temperature is not announced");

    // probing found an SHTC3 and an LD2410
    app::hardware::Capabilities found;
    found.detected = true;
    found.temperature = found.humidity = true;
    found.presence = true;
    found.parts = "SHTC3, LD2410";
    fx.sensors.set_capabilities(found);
    fx.transport.published.clear();
    fx.mqtt.poll(fx.clock.now);
    expect(fx.transport.discovery("sensor", "temperature").size() > 1 &&
               fx.transport.discovery("binary_sensor", "presence").size() > 1 &&
               fx.transport.discovery("sensor", "distance").size() > 1,
           "after probing: announced again with temperature, presence and distance");
    expect(fx.transport.discovery("sensor", "target_count").empty() && fx.transport.discovery("sensor", "target_1_x").empty(),
           "LD2410: no target count, no positions");
    expect(fx.transport.discovery("button", "haptic_test").empty(), "no BOS1921: no haptic test");
    fx.transport.published.clear();
    fx.mqtt.poll(fx.clock.now + 100);
    expect(fx.transport.count("homeassistant/") == 0, "announced only once per change");
}

void test_capability_override()
{
    using app::hardware::apply_override;
    const auto all = SimulatedSensors::planned_hardware();
    const auto none = apply_override(all, "none");
    expect(none.detected && !none.haptics && !none.temperature && !none.presence && none.parts.empty(),
           "HOMEPANE_HW=none: probing done, nothing found");
    const auto c = apply_override(all, "no-haptics,ld2410");
    expect(!c.haptics && !c.force && c.temperature && c.presence && !c.target_count && !c.target_position,
           "no-haptics,ld2410: climate and presence stay, haptics and targets go");
    expect(apply_override(all, nullptr) == all && apply_override(all, "") == all, "no override: unchanged");
}

void test_simulated_sensors()
{
    SimulatedSensors s(3);
    bool seen_present = false, seen_empty = false;
    int presses = 0;
    float tmin = 99, tmax = -99;
    for (uint64_t t = 0; t < 30ull * 60 * 1000; t += 1000) {
        auto r = s.read(t);
        seen_present |= *r.presence;
        seen_empty |= !*r.presence;
        if (*r.presence) {
            expect(*r.distance_m > 0.3f && *r.distance_m < 4.0f, "distance in range") ;
        }
        tmin = std::min(tmin, *r.temperature_c);
        tmax = std::max(tmax, *r.temperature_c);
        s.take_gestures([&](app::sensors::PressGesture) { ++presses; });
        if (g_failures) return;
    }
    expect(seen_present && seen_empty, "the room is occupied and empty in turns within 30 minutes");
    expect(tmin > 19 && tmax < 24, "temperature stays plausible");
    expect(presses > 0, "presses happen while someone is there");
}

} // namespace

int main()
{
    test_discovery_payload();
    test_connect_and_announce();
    test_change_based_publishing();
    test_commands_and_disable();
    test_simulated_sensors();
    test_internal_ram_diagnostics();
    test_display_switch();
    test_without_extra_hardware();
    test_hardware_found_after_connect();
    test_capability_override();
    if (g_failures == 0) {
        std::printf("All MQTT tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
