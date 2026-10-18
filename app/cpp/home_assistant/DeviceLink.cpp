#include "DeviceLink.h"

#include <cjson/cJSON.h>

#include <cctype>
#include <memory>

using app::core::LogLevel;

namespace app::home_assistant {

namespace {
struct JsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string str(const cJSON* node) { return cJSON_IsString(node) ? node->valuestring : ""; }

bool safe_id(const std::string& id)
{
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-')) return false;
    }
    return true;
}
} // namespace

DeviceLink::DeviceLink(std::function<HaClient*()> client, std::function<std::string()> node_id,
                       std::function<bool()> mqtt_connected, std::function<std::string()> wanted_area_id,
                       app::core::ILogger* logger)
    : client_(std::move(client)), node_id_(std::move(node_id)), mqtt_connected_(std::move(mqtt_connected)),
      wanted_area_id_(std::move(wanted_area_id)), logger_(logger)
{
}

void DeviceLink::log(const std::string& line)
{
    if (logger_) logger_->log(LogLevel::Info, ("DeviceLink: " + line).c_str());
}

std::string DeviceLink::find_device_id(const std::string& result_json, const std::string& node_id, std::string* area_id)
{
    JsonPtr root(cJSON_Parse(result_json.c_str()));
    if (!cJSON_IsArray(root.get())) return "";
    const cJSON* dev = nullptr;
    cJSON_ArrayForEach(dev, root.get())
    {
        const cJSON* ids = cJSON_GetObjectItemCaseSensitive(dev, "identifiers");
        const cJSON* pair = nullptr;
        cJSON_ArrayForEach(pair, ids)
        {
            if (cJSON_GetArraySize(pair) == 2 && str(cJSON_GetArrayItem(pair, 0)) == "mqtt" &&
                str(cJSON_GetArrayItem(pair, 1)) == node_id) {
                if (area_id) *area_id = str(cJSON_GetObjectItemCaseSensitive(dev, "area_id"));
                return str(cJSON_GetObjectItemCaseSensitive(dev, "id"));
            }
        }
    }
    return "";
}

std::vector<Area> DeviceLink::parse_areas(const std::string& result_json)
{
    std::vector<Area> out;
    JsonPtr root(cJSON_Parse(result_json.c_str()));
    const cJSON* a = nullptr;
    cJSON_ArrayForEach(a, root.get())
    {
        Area area{str(cJSON_GetObjectItemCaseSensitive(a, "area_id")), str(cJSON_GetObjectItemCaseSensitive(a, "name"))};
        if (!area.id.empty()) out.push_back(std::move(area));
    }
    return out;
}

namespace {
// lower case, '_' and ' ' -> '-' ("Living_Room" and "living room" -> "living-room")
std::string norm(const std::string& s)
{
    std::string out;
    for (unsigned char c : s) out += (c == '_' || c == ' ') ? '-' : static_cast<char>(std::tolower(c));
    return out;
}
} // namespace

const Area* DeviceLink::match_dashboard(const std::vector<Area>& areas, const std::string& dashboard_path)
{
    const std::string path = norm(dashboard_path);
    const Area* best = nullptr;
    size_t best_len = 0;
    for (const auto& a : areas) {
        for (const std::string& key : {norm(a.id), norm(a.name)}) {
            if (key.size() > best_len && path.find(key) != std::string::npos) {
                best = &a;
                best_len = key.size();
            }
        }
    }
    return best;
}

void DeviceLink::poll(uint64_t now_ms)
{
    HaClient* client = client_ ? client_() : nullptr;
    if (!client || client->state() != HaClientState::Ready) return;

    // 0. Default room from the dashboard, once.
    if (!default_checked_ && apply_default_ && dashboard_path_) {
        default_checked_ = true;
        const std::string dash = dashboard_path_();
        if (!dash.empty() && (wanted_area_id_ ? wanted_area_id_() : "").empty()) {
            fetch_areas([this, dash](bool ok, const std::vector<Area>& areas) {
                const Area* a = ok ? match_dashboard(areas, dash) : nullptr;
                if (!a) return;
                log("room from the dashboard: " + a->name);
                apply_default_(*a);
                room_changed();
            });
        }
    }

    // 1. The device only exists once MQTT discovery went through.
    if (device_id_.empty()) {
        // HA creates the device a moment after discovery: retry every 10 s.
        if (lookup_pending_ || now_ms < next_lookup_ms_ || !mqtt_connected_ || !mqtt_connected_() || lookups_ >= 30) return;
        // today's lookup, if there was one (checked here, not earlier: by now the clock is set)
        if (!cache_checked_) {
            cache_checked_ = true;
            const Cached c = cache_load_ ? cache_load_() : Cached{};
            if (!c.device_id.empty()) {
                device_id_ = c.device_id;
                device_area_id_ = c.area_id;
                log("HA device " + device_id_ + " (area '" + device_area_id_ + "', from today's cache)");
                return;
            }
        }
        lookup_pending_ = true;
        ++lookups_;
        next_lookup_ms_ = now_ms + 10000;
        const std::string node = node_id_ ? node_id_() : "";
        client->send_command(R"({"type":"config/device_registry/list"})", [this, node](const ResultMessage& r) {
            lookup_pending_ = false;
            if (!r.success) {
                log("device list failed: " + r.error_message);
                return;
            }
            device_id_ = find_device_id(r.result_json, node, &device_area_id_);
            if (device_id_.empty()) {
                log("no HA device for " + node + " yet");
                return;
            }
            log("HA device " + device_id_ + " (area '" + device_area_id_ + "')");
            if (cache_save_) cache_save_(Cached{device_id_, device_area_id_});
            if (!device_area_id_.empty() && apply_default_ && (wanted_area_id_ ? wanted_area_id_() : "").empty()) {
                // The room was set in HA: take it over, name included.
                const std::string area = device_area_id_;
                fetch_areas([this, area](bool ok, const std::vector<Area>& areas) {
                    for (const auto& a : ok ? areas : std::vector<Area>{}) {
                        if (a.id != area) continue;
                        log("room from HA: " + a.name);
                        apply_default_(a);
                        area_applied_ = true;
                    }
                });
            }
        });
        return;
    }

    // 2. Keep its area in line with the room setting.
    if (area_applied_ || update_pending_) return;
    const std::string wanted = wanted_area_id_ ? wanted_area_id_() : "";
    if (wanted.empty() || wanted == kNoRoom || wanted == device_area_id_ || !safe_id(wanted) || !safe_id(device_id_)) {
        area_applied_ = true;
        return;
    }
    update_pending_ = true;
    client->send_command(R"({"type":"config/device_registry/update","device_id":")" + device_id_ +
                             R"(","area_id":")" + wanted + "\"}",
                         [this, wanted](const ResultMessage& r) {
                             update_pending_ = false;
                             area_denied_ = !r.success;
                             if (r.success) {
                                 device_area_id_ = wanted;
                                 area_applied_ = true;
                                 if (cache_save_) cache_save_(Cached{device_id_, device_area_id_});
                                 log("device area set to " + wanted);
                             } else {
                                 log("could not set the area: " + r.error_message);
                                 area_applied_ = true; // do not retry endlessly; a room change retries
                             }
                         });
}

void DeviceLink::fetch_areas(AreasCallback done)
{
    HaClient* client = client_ ? client_() : nullptr;
    if (!client || client->state() != HaClientState::Ready) {
        if (done) done(false, {});
        return;
    }
    client->send_command(R"({"type":"config/area_registry/list"})", [done](const ResultMessage& r) {
        if (done) done(r.success, r.success ? parse_areas(r.result_json) : std::vector<Area>{});
    });
}

} // namespace app::home_assistant
