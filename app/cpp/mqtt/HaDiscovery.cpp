#include "HaDiscovery.h"

#include <cjson/cJSON.h>

namespace app::mqtt {

std::string availability_topic(const std::string& node_id) { return "homepane/" + node_id + "/status"; }

std::string state_topic(const std::string& node_id, const std::string& object_id)
{
    return "homepane/" + node_id + "/" + object_id + "/state";
}

std::string command_topic(const std::string& node_id, const std::string& object_id)
{
    return "homepane/" + node_id + "/" + object_id + "/set";
}

std::string discovery_topic(const std::string& node_id, const EntityDef& e)
{
    return "homeassistant/" + e.component + "/" + node_id + "/" + e.object_id + "/config";
}

std::string discovery_payload(const DeviceInfo& device, const EntityDef& e)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", e.name.c_str());
    cJSON_AddStringToObject(root, "unique_id", (device.node_id + "_" + e.object_id).c_str());
    cJSON_AddStringToObject(root, "availability_topic", availability_topic(device.node_id).c_str());
    if (e.component == "button") {
        cJSON_AddStringToObject(root, "command_topic", command_topic(device.node_id, e.object_id).c_str());
    } else {
        cJSON_AddStringToObject(root, "state_topic", state_topic(device.node_id, e.object_id).c_str());
    }
    if (!e.device_class.empty()) cJSON_AddStringToObject(root, "device_class", e.device_class.c_str());
    if (!e.unit.empty()) cJSON_AddStringToObject(root, "unit_of_measurement", e.unit.c_str());
    if (!e.state_class.empty()) cJSON_AddStringToObject(root, "state_class", e.state_class.c_str());
    if (!e.entity_category.empty()) cJSON_AddStringToObject(root, "entity_category", e.entity_category.c_str());
    if (!e.icon.empty()) cJSON_AddStringToObject(root, "icon", e.icon.c_str());
    if (e.precision >= 0) cJSON_AddNumberToObject(root, "suggested_display_precision", e.precision);
    if (!e.enabled_by_default) cJSON_AddFalseToObject(root, "enabled_by_default");
    if (!e.event_types.empty()) {
        cJSON* types = cJSON_AddArrayToObject(root, "event_types");
        for (const auto& t : e.event_types) cJSON_AddItemToArray(types, cJSON_CreateString(t.c_str()));
    }
    cJSON* dev = cJSON_AddObjectToObject(root, "device");
    cJSON* ids = cJSON_AddArrayToObject(dev, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(device.node_id.c_str()));
    cJSON_AddStringToObject(dev, "name", device.name.c_str());
    cJSON_AddStringToObject(dev, "model", device.model.c_str());
    cJSON_AddStringToObject(dev, "manufacturer", device.manufacturer.c_str());
    if (!device.sw_version.empty()) cJSON_AddStringToObject(dev, "sw_version", device.sw_version.c_str());
    if (!device.suggested_area.empty()) cJSON_AddStringToObject(dev, "suggested_area", device.suggested_area.c_str());
    char* text = cJSON_PrintUnformatted(root);
    std::string out = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

} // namespace app::mqtt
