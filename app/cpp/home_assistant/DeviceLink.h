#ifndef APP_CPP_HOME_ASSISTANT_DEVICELINK_H
#define APP_CPP_HOME_ASSISTANT_DEVICELINK_H

#include "../core/ILogger.h"
#include "HaClient.h"

#include <functional>
#include <string>
#include <vector>

namespace app::home_assistant {

struct Area {
    std::string id;
    std::string name;
};

// Connects the panel's MQTT device in Home Assistant with the rest of the
// app: finds the device's registry id (HA creates it from MQTT discovery),
// keeps its area in line with the panel's "room" setting, and hands the id
// to voice requests — HA uses the requesting device's area for commands
// without one ("Licht aus" -> the lights in the panel's room). UI thread.
class DeviceLink {
public:
    DeviceLink(std::function<HaClient*()> client, std::function<std::string()> node_id,
               std::function<bool()> mqtt_connected, std::function<std::string()> wanted_area_id,
               app::core::ILogger* logger = nullptr);

    void poll(uint64_t now_ms);
    // The room setting changed: apply it to the device on the next poll.
    void room_changed() { area_applied_ = false; }
    const std::string& device_id() const { return device_id_; }
    // The last area update was refused (the panel's HA user is no admin).
    bool area_denied() const { return area_denied_; }

    // No room chosen yet and the device has an area in HA: adopt it
    // (needs no admin rights, unlike setting the area).
    // No room chosen yet ("" — an explicit "None" is stored as kNoRoom): pick
    // the area whose id or name appears in the dashboard path once HA is up.
    static constexpr const char* kNoRoom = "none";
    void set_default_room(std::function<std::string()> dashboard_path, std::function<void(const Area&)> apply)
    {
        dashboard_path_ = std::move(dashboard_path);
        apply_default_ = std::move(apply);
    }
    // The area matching `dashboard_path` (e.g. "dashboard-wohnzimmer" ->
    // wohnzimmer), longest match wins; nullptr if none.
    static const Area* match_dashboard(const std::vector<Area>& areas, const std::string& dashboard_path);

    // The found device, kept for a day: the device list is large (seconds to
    // parse on the panel) and hardly ever changes. load() -> {device id,
    // area id} when still valid ("" otherwise); save() after a lookup.
    struct Cached {
        std::string device_id, area_id;
    };
    void set_cache(std::function<Cached()> load, std::function<void(const Cached&)> save)
    {
        cache_load_ = std::move(load);
        cache_save_ = std::move(save);
    }

    using AreasCallback = std::function<void(bool ok, const std::vector<Area>& areas)>;
    void fetch_areas(AreasCallback done);

    // Parsers (host tests): config/device_registry/list -> id of the device
    // with identifier ["mqtt", node_id]; config/area_registry/list -> areas.
    static std::string find_device_id(const std::string& result_json, const std::string& node_id,
                                      std::string* area_id = nullptr);
    static std::vector<Area> parse_areas(const std::string& result_json);

private:
    void log(const std::string& line);

    std::function<HaClient*()> client_;
    std::function<std::string()> node_id_;
    std::function<bool()> mqtt_connected_;
    std::function<std::string()> wanted_area_id_;
    app::core::ILogger* logger_;

    std::function<std::string()> dashboard_path_;
    std::function<void(const Area&)> apply_default_;
    std::function<Cached()> cache_load_;
    std::function<void(const Cached&)> cache_save_;
    bool cache_checked_ = false;
    bool default_checked_ = false;

    std::string device_id_;
    std::string device_area_id_;
    bool lookup_pending_ = false;
    bool update_pending_ = false;
    bool area_applied_ = false;
    bool area_denied_ = false;
    uint32_t lookups_ = 0;
    uint64_t next_lookup_ms_ = 0;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_DEVICELINK_H
