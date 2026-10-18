#include "EntityDetail.h"

#include "../AttributeText.h"

#include <cjson/cJSON.h>

namespace app::ui {

using app::home_assistant::Attributes;
using app::home_assistant::EntityState;

namespace {
std::vector<std::string> string_list(const Attributes& attrs, const char* key)
{
    std::vector<std::string> out;
    auto it = attrs.find(key);
    if (it == attrs.end()) return out;
    cJSON* arr = cJSON_Parse(it->second.c_str());
    const cJSON* v = nullptr;
    cJSON_ArrayForEach(v, arr)
    {
        if (cJSON_IsString(v)) out.emplace_back(v->valuestring);
    }
    cJSON_Delete(arr);
    return out;
}

bool parse_pair(const std::string& json, double* a, double* b)
{
    cJSON* arr = cJSON_Parse(json.c_str());
    const bool ok = arr && cJSON_IsArray(arr) && cJSON_GetArraySize(arr) >= 2;
    if (ok) {
        *a = cJSON_GetArrayItem(arr, 0)->valuedouble;
        *b = cJSON_GetArrayItem(arr, 1)->valuedouble;
    }
    cJSON_Delete(arr);
    return ok;
}
} // namespace

LightDetail::State light_detail_state(const EntityState& e, const std::string& name, const std::string& state)
{
    const Attributes& a = e.attributes;
    LightDetail::State st;
    st.name = name;
    st.on = state == "on";
    st.brightness = find_attr_int(a, "brightness", st.brightness);
    st.color_mode = find_attr(a, "color_mode");
    parse_pair(find_attr(a, "hs_color"), &st.hue, &st.saturation);
    st.kelvin = find_attr_int(a, "color_temp_kelvin", st.kelvin);
    st.min_kelvin = find_attr_int(a, "min_color_temp_kelvin", st.min_kelvin);
    st.max_kelvin = find_attr_int(a, "max_color_temp_kelvin", st.max_kelvin);
    const auto it = a.find("supported_color_modes");
    const std::string modes = it == a.end() ? "" : it->second;
    st.supports_color = modes.find("hs") != std::string::npos || modes.find("rgb") != std::string::npos ||
                        modes.find("xy") != std::string::npos;
    st.supports_temperature = modes.find("color_temp") != std::string::npos;
    st.supports_brightness = !(modes.find("onoff") != std::string::npos && !st.supports_color && !st.supports_temperature);
    return st;
}

CoverDetail::State cover_detail_state(const EntityState& e, const std::string& name, const std::string& state)
{
    const Attributes& a = e.attributes;
    CoverDetail::State st;
    st.name = name;
    st.state = state;
    st.position = find_attr_double(a, "current_position");
    st.tilt = find_attr_double(a, "current_tilt_position");
    const std::string c = find_attr(a, "device_class");
    st.garage = c == "garage" || c == "gate";
    const int features = static_cast<int>(find_attr_double(a, "supported_features").value_or(15));
    st.can_position = (features & 4) != 0; // CoverEntityFeature.SET_POSITION
    return st;
}

ClimateDetail::State climate_detail_state(const EntityState& e, const std::string& name, const std::string& state)
{
    const Attributes& a = e.attributes;
    ClimateDetail::State st;
    st.name = name;
    st.mode = state;
    st.action = find_attr(a, "hvac_action");
    st.min = find_attr_double(a, "min_temp").value_or(7);
    st.max = find_attr_double(a, "max_temp").value_or(30);
    st.step = find_attr_double(a, "target_temp_step").value_or(0.5);
    st.current = find_attr_double(a, "current_temperature");
    st.target = find_attr_double(a, "temperature");
    st.low = find_attr_double(a, "target_temp_low");
    st.high = find_attr_double(a, "target_temp_high");
    st.humidity = find_attr_double(a, "current_humidity");
    st.modes = string_list(a, "hvac_modes");
    st.presets = string_list(a, "preset_modes");
    st.fan_modes = string_list(a, "fan_modes");
    st.preset = find_attr(a, "preset_mode");
    st.fan_mode = find_attr(a, "fan_mode");
    return st;
}

MediaDetail::State media_detail_state(const EntityState& e, const std::string& name, const std::string& state)
{
    const Attributes& a = e.attributes;
    MediaDetail::State st;
    st.name = name;
    st.state = state;
    st.title = find_attr(a, "media_title");
    st.artist = find_attr(a, "media_artist");
    st.album = find_attr(a, "media_album_name");
    st.position = find_attr_double(a, "media_position");
    st.duration = find_attr_double(a, "media_duration");
    st.volume = find_attr_double(a, "volume_level");
    st.position_updated_epoch_s = MediaDetail::parse_iso_epoch(find_attr(a, "media_position_updated_at"));
    st.shuffle = find_attr(a, "shuffle") == "true";
    st.repeat = find_attr(a, "repeat");
    st.tv = find_attr(a, "device_class") == "tv";
    return st;
}

AlarmDetail::State alarm_detail_state(const EntityState& e, const std::string& name, const std::string& state)
{
    const Attributes& a = e.attributes;
    AlarmDetail::State st;
    st.name = name;
    st.state = state;
    st.supported_features = static_cast<int>(find_attr_double(a, "supported_features").value_or(7));
    const std::string format = find_attr(a, "code_format");
    st.code_needed = !format.empty() && format != "null";
    st.code_arm_required = find_attr(a, "code_arm_required") != "false";
    return st;
}

EntityDetail::EntityDetail(Send send) : send_(std::move(send)) {}

EntityDetail::~EntityDetail() = default;

bool EntityDetail::exists_for(const std::string& d)
{
    return d == "light" || d == "cover" || d == "climate" || d == "media_player" || d == "alarm_control_panel";
}

void EntityDetail::open(const std::string& domain, const EntityState& e, const std::string& name,
                        const std::string& state)
{
    if (domain == "light") {
        if (!light_) light_ = std::make_unique<LightDetail>(send_, nullptr);
        light_->open(light_detail_state(e, name, state));
    } else if (domain == "cover") {
        if (!cover_) cover_ = std::make_unique<CoverDetail>(send_);
        cover_->open(cover_detail_state(e, name, state));
    } else if (domain == "climate") {
        if (!climate_) climate_ = std::make_unique<ClimateDetail>(send_);
        climate_->open(climate_detail_state(e, name, state));
    } else if (domain == "media_player") {
        if (!media_) media_ = std::make_unique<MediaDetail>(send_);
        media_->open(media_detail_state(e, name, state));
    } else if (domain == "alarm_control_panel") {
        if (!alarm_) alarm_ = std::make_unique<AlarmDetail>(send_);
        alarm_->open(alarm_detail_state(e, name, state));
    }
}

void EntityDetail::update(const EntityState& e, const std::string& name, const std::string& state)
{
    if (light_ && light_->is_open()) light_->update(light_detail_state(e, name, state));
    if (cover_ && cover_->is_open()) cover_->update(cover_detail_state(e, name, state));
    if (climate_ && climate_->is_open()) climate_->update(climate_detail_state(e, name, state));
    if (media_ && media_->is_open()) media_->update(media_detail_state(e, name, state));
    if (alarm_ && alarm_->is_open()) alarm_->update(alarm_detail_state(e, name, state));
}

} // namespace app::ui
