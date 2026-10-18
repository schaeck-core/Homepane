#include "LightWidget.h"

#include "AttributeText.h"
#include "details/LightDetail.h"
#include "lvgl/lvgl.h"
#include "../../i18n.h"

#include "../../haptic/haptic.h"
#include "../../ui_kit.h"

#include <cjson/cJSON.h>

#include <cstdio>

namespace app::ui {

using app::home_assistant::Availability;
using app::home_assistant::EntityState;
using app::home_assistant::HaInstanceId;
using app::home_assistant::ResultMessage;

namespace {
// [a, b] JSON array -> the two numbers.
bool parse_pair(const std::string& json, double* a, double* b) {
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

LightWidget::LightWidget(HaInstanceId instance, app::home_assistant::HaCommandService& commands,
                          std::string entity_id, std::string name_override)
    : instance_(std::move(instance)),
      commands_(commands),
      entity_id_(std::move(entity_id)),
      name_override_(std::move(name_override)) {}

LightWidget::~LightWidget() = default;

std::vector<DataRequirement> LightWidget::requirements() const { return {EntityRequirement{instance_, entity_id_}}; }

void LightWidget::create(lv_obj_t* parent) {
    // Centred tile of the design guide: tap toggles, double tap = 100 %
    // neutral white, hold opens the detail view.
    wrapper_ = ui_tile(parent);
    ui_gestures(
        wrapper_, [](void* self) { static_cast<LightWidget*>(self)->handle_tap(); },
        [](void* self) { static_cast<LightWidget*>(self)->handle_long_press(); },
        [](void* self) { static_cast<LightWidget*>(self)->handle_double_tap(); }, this);
    haptic_add_click(wrapper_, HAPTIC_BUTTON_PRESS);

    lv_obj_t* center = ui_box(wrapper_);
    lv_obj_set_width(center, LV_PCT(100));
    lv_obj_set_flex_grow(center, 1);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center, 20, 0);
    bubble_ = ui_bubble(center, UI_BUBBLE, UI_ICON_TILE, LI_LIGHTBULB, UI_LOOK_OFF);
    lv_obj_t* texts = ui_box(center);
    lv_obj_set_size(texts, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(texts, 2, 0);
    name_label_ = ui_label_line(texts, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_set_style_text_align(name_label_, LV_TEXT_ALIGN_CENTER, 0);
    state_label_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_style_text_align(state_label_, LV_TEXT_ALIGN_CENTER, 0);

    display_name_ = name_override_;
    render();

    track_root(wrapper_);
}

void LightWidget::shutdown_ui() {
    // Generation-safety (ARCHITECTURE.md §7/§8): a command sent right before a dashboard
    // switch must not touch this widget after it is gone. Cancelling here
    // (UI context, before any member is destroyed) makes HaClient skip
    // the callback entirely when the Result/timeout eventually arrives.
    pending_command_.cancel();
    // The detail page lives on the screen, not under the tile: close it here.
    detail_.reset();
}

void LightWidget::on_data(const DataUpdate& update) {
    if (!update.entity_state) return;
    const EntityState& s = *update.entity_state;

    availability_ = s.availability;
    is_on_ = (s.state == "on");

    if (name_override_.empty()) {
        std::string friendly = find_attr(s.attributes, "friendly_name");
        display_name_ = friendly.empty() ? entity_id_ : friendly;
    } else {
        display_name_ = name_override_;
    }

    brightness_ = find_attr_int(s.attributes, "brightness", brightness_);
    color_mode_ = find_attr(s.attributes, "color_mode");
    color_temp_kelvin_ = find_attr_int(s.attributes, "color_temp_kelvin", color_temp_kelvin_);
    min_color_temp_kelvin_ = find_attr_int(s.attributes, "min_color_temp_kelvin", min_color_temp_kelvin_);
    max_color_temp_kelvin_ = find_attr_int(s.attributes, "max_color_temp_kelvin", max_color_temp_kelvin_);

    std::string rgb_json = find_attr(s.attributes, "rgb_color");
    if (!rgb_json.empty()) {
        cJSON* arr = cJSON_Parse(rgb_json.c_str());
        if (arr && cJSON_IsArray(arr) && cJSON_GetArraySize(arr) >= 3) {
            color_r_ = static_cast<uint8_t>(cJSON_GetArrayItem(arr, 0)->valueint);
            color_g_ = static_cast<uint8_t>(cJSON_GetArrayItem(arr, 1)->valueint);
            color_b_ = static_cast<uint8_t>(cJSON_GetArrayItem(arr, 2)->valueint);
        }
        if (arr) cJSON_Delete(arr);
    }

    double h = 0, sat = 0;
    if (parse_pair(find_attr(s.attributes, "hs_color"), &h, &sat)) {
        hue_ = h;
        saturation_ = sat;
    }
    const std::string modes = s.attributes.count("supported_color_modes") ? s.attributes.at("supported_color_modes") : "";
    supports_color_ = modes.find("hs") != std::string::npos || modes.find("rgb") != std::string::npos ||
                      modes.find("xy") != std::string::npos;
    supports_temperature_ = modes.find("color_temp") != std::string::npos;
    supports_brightness_ = !(modes.find("onoff") != std::string::npos && !supports_color_ && !supports_temperature_);

    // Any real update resolves an outstanding optimistic guess, whether it
    // confirms it or supersedes it (e.g. toggled from elsewhere while our
    // command was still in flight).
    pending_ = false;

    render();
    if (detail_ && detail_->is_open()) {
        LightDetail::State st;
        st.name = display_name_;
        st.on = is_on_;
        st.brightness = brightness_;
        st.color_mode = color_mode_;
        st.hue = hue_;
        st.saturation = saturation_;
        st.kelvin = color_temp_kelvin_;
        st.min_kelvin = min_color_temp_kelvin_;
        st.max_kelvin = max_color_temp_kelvin_;
        st.supports_color = supports_color_;
        st.supports_temperature = supports_temperature_;
        st.supports_brightness = supports_brightness_;
        detail_->update(st);
    }
}

void LightWidget::handle_tap() {
    if (availability_ == Availability::loading) return; // nothing confirmed to toggle yet

    bool current_effective_on = pending_ ? pending_target_on_ : is_on_;
    bool target_on = !current_effective_on;

    pending_ = true;
    pending_target_on_ = target_on;
    render();

    pending_command_.cancel(); // supersede any earlier still-in-flight toggle from this widget
    const char* service = target_on ? "turn_on" : "turn_off";
    pending_command_ = commands_.call_service("light", service, entity_id_, "", [this](const ResultMessage& result) {
        if (!result.success) {
            // Rollback: the command failed or timed out, and no real
            // update is coming to correct the optimistic guess.
            pending_ = false;
            render();
        }
        // On success: leave pending_ as-is until the real EntityStore
        // update arrives via on_data(), so the UI doesn't flicker back to
        // the old state in the gap before HA's own event shows up.
    });
}

void LightWidget::handle_long_press() {
    if (availability_ == Availability::loading) return;
    if (!detail_) detail_ = std::make_unique<LightDetail>([this](const std::string& service, const std::string& json) { send(service, json); },
                                                          nullptr);
    LightDetail::State st;
    st.name = display_name_.empty() ? entity_id_ : display_name_;
    st.on = is_on_;
    st.brightness = brightness_;
    st.color_mode = color_mode_;
    st.hue = hue_;
    st.saturation = saturation_;
    st.kelvin = color_temp_kelvin_;
    st.min_kelvin = min_color_temp_kelvin_;
    st.max_kelvin = max_color_temp_kelvin_;
    st.supports_color = supports_color_;
    st.supports_temperature = supports_temperature_;
    st.supports_brightness = supports_brightness_;
    detail_->open(st);
}

void LightWidget::send(const std::string& service, const std::string& json) {
    pending_command_.cancel();
    pending_command_ = commands_.call_service("light", service, entity_id_, json, [](const ResultMessage&) {});
}

void LightWidget::render() {
    if (!bubble_) return;
    const bool effective_on = pending_ ? pending_target_on_ : is_on_;
    const bool loaded = availability_ != Availability::loading;
    const bool unavailable = availability_ == Availability::unavailable;

    // On: the icon in the light's colour (muted toward the text colour so
    // saturated colours stay calm); white light: sand.
    uint32_t icon_color = 0;
    if (effective_on && (color_mode_ == "hs" || color_mode_ == "rgb" || color_mode_ == "xy" ||
                         color_mode_ == "rgbw" || color_mode_ == "rgbww")) {
        lv_color_t mixed = lv_color_mix(lv_color_make(color_r_, color_g_, color_b_), lv_color_hex(UI_TEXT), 170);
        icon_color = lv_color_to_u32(mixed) & 0xFFFFFF;
    }
    ui_bubble_set(bubble_, effective_on ? LI_LIGHTBULB : LI_LIGHTBULB_OFF,
                  loaded && effective_on && !unavailable ? UI_LOOK_ON : UI_LOOK_OFF, icon_color);

    lv_label_set_text(name_label_, display_name_.empty() ? entity_id_.c_str() : display_name_.c_str());
    char state[32] = "";
    if (!loaded) std::snprintf(state, sizeof(state), "%s", "");
    else if (unavailable) std::snprintf(state, sizeof(state), "%s", tr(TR_STATE_UNAVAILABLE));
    else if (!effective_on) std::snprintf(state, sizeof(state), "%s", tr(TR_STATE_OFF));
    else if (pending_ && !is_on_) std::snprintf(state, sizeof(state), "%s", tr(TR_STATE_ON));
    else std::snprintf(state, sizeof(state), "%d %%", (brightness_ * 100 + 127) / 255);
    lv_label_set_text(state_label_, state);
    lv_obj_set_style_opa(wrapper_, unavailable ? LV_OPA_60 : LV_OPA_COVER, 0);
}

void LightWidget::handle_double_tap() {
    if (availability_ == Availability::loading) return;
    pending_ = true;
    pending_target_on_ = true;
    brightness_ = 255;
    render();
    pending_command_.cancel();
    pending_command_ = commands_.call_service("light", "turn_on", entity_id_,
                                              "{\"brightness\":255,\"color_temp_kelvin\":4000}",
                                              [this](const ResultMessage& result) {
                                                  if (!result.success) {
                                                      pending_ = false;
                                                      render();
                                                  }
                                              });
    ui_toast(trf(TR_LIGHT_FULL_NEUTRAL, {display_name_.empty() ? entity_id_ : display_name_}).c_str());
}

} // namespace app::ui
