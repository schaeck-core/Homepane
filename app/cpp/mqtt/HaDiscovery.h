#ifndef APP_CPP_MQTT_HADISCOVERY_H
#define APP_CPP_MQTT_HADISCOVERY_H

#include <string>
#include <vector>

namespace app::mqtt {

// The panel as one Home Assistant device (MQTT discovery).
struct DeviceInfo {
    std::string node_id;  // stable, from the MAC: never changes over updates
    std::string name;
    std::string model = "Homepane ESP32-P4";
    std::string manufacturer = "Homepane";
    std::string sw_version;
    std::string suggested_area; // only used by HA when the device is created
};

struct EntityDef {
    std::string component; // sensor, binary_sensor, event, button
    std::string object_id;
    std::string name;
    std::string device_class, unit, state_class, entity_category, icon;
    int precision = -1;                   // suggested_display_precision
    std::vector<std::string> event_types; // event
    // High-rate values (radar coordinates) start disabled in HA: every state
    // change lands in HA's database.
    bool enabled_by_default = true;
};

std::string availability_topic(const std::string& node_id);
std::string state_topic(const std::string& node_id, const std::string& object_id);
std::string command_topic(const std::string& node_id, const std::string& object_id);
std::string discovery_topic(const std::string& node_id, const EntityDef& e);
// The retained config payload (JSON) for `e`.
std::string discovery_payload(const DeviceInfo& device, const EntityDef& e);

} // namespace app::mqtt

#endif // APP_CPP_MQTT_HADISCOVERY_H
