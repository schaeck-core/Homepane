#include "ClimateDetail.h"

#include "../tiles/TileWidget.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app::ui {

namespace {
constexpr int32_t kGauge = 336;
constexpr int32_t kArcWidth = 22;
constexpr uint32_t kSendDelayMs = 700;
constexpr size_t kMaxModes = 4;

ClimateDetail* self_of(lv_event_t* e) { return static_cast<ClimateDetail*>(lv_event_get_user_data(e)); }
int index_of(lv_event_t* e) { return static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))); }

void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }
void raw_minus_cb(lv_event_t* e) { self_of(e)->on_step(-1); }
void raw_plus_cb(lv_event_t* e) { self_of(e)->on_step(+1); }
void raw_bound_cb(lv_event_t* e) { self_of(e)->on_select_bound(index_of(e) == 1); }
void raw_mode_cb(lv_event_t* e) { self_of(e)->on_mode(index_of(e)); }
void raw_preset_cb(lv_event_t* e) { self_of(e)->on_preset(index_of(e)); }
void raw_send_cb(lv_timer_t* t) { static_cast<ClimateDetail*>(lv_timer_get_user_data(t))->send_pending(); }

std::string degrees(double v)
{
    const bool whole = std::fabs(v - std::round(v)) < 0.05;
    return format_number(v, whole ? 0 : 1) + "°";
}

const char* mode_icon(const std::string& m)
{
    if (m == "off") return LI_POWER;
    if (m == "heat") return LI_FLAME;
    if (m == "cool") return LI_SNOWFLAKE;
    if (m == "dry") return LI_DROPLET;
    if (m == "fan_only") return LI_FAN;
    return LI_REFRESH_CW; // heat_cool, auto
}

uint32_t mode_color(const std::string& m)
{
    if (m == "heat") return UI_HEAT;
    if (m == "cool") return UI_COOL;
    if (m == "heat_cool" || m == "auto") return UI_AUTO;
    return UI_TEXT3;
}

lv_obj_t* round_button(lv_obj_t* parent, const char* icon)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, 104, 104); // primary touch target
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(b, UI_ICON_VALUE, UI_TEXT, icon));
    return b;
}

lv_obj_t* choice_button(lv_obj_t* row, int32_t radius)
{
    lv_obj_t* b = ui_box(row);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    return b;
}
} // namespace

const char* ClimateDetail::mode_name(const std::string& m)
{
    if (m == "off") return tr(TR_STATE_OFF);
    if (m == "heat") return tr(TR_CLIMATE_HEAT);
    if (m == "cool") return tr(TR_CLIMATE_COOL);
    if (m == "heat_cool") return tr(TR_CLIMATE_AUTO);
    if (m == "auto") return tr(TR_CLIMATE_AUTO);
    if (m == "dry") return tr(TR_CLIMATE_DRY);
    if (m == "fan_only") return tr(TR_CLIMATE_FAN_ONLY);
    return m.c_str();
}

const char* ClimateDetail::preset_name(const std::string& p)
{
    if (p == "home") return tr(TR_PRESET_HOME);
    if (p == "away") return tr(TR_PRESET_AWAY);
    if (p == "sleep") return tr(TR_PRESET_SLEEP);
    if (p == "eco") return tr(TR_PRESET_ECO);
    if (p == "comfort") return tr(TR_PRESET_COMFORT);
    if (p == "boost") return tr(TR_PRESET_BOOST);
    if (p == "activity") return tr(TR_PRESET_ACTIVITY);
    if (p == "none") return tr(TR_PRESET_NONE);
    return p.c_str();
}

std::string ClimateDetail::action_text(const State& s)
{
    if (s.mode == "off") return tr(TR_STATE_OFF);
    if (s.action == "heating") return tr(TR_CLIMATE_HEATING);
    if (s.action == "cooling") return tr(TR_CLIMATE_COOLING);
    if (s.action == "drying") return tr(TR_CLIMATE_DRYING);
    if (s.action == "fan") return tr(TR_CLIMATE_FANNING);
    if (s.action == "idle") return tr(TR_CLIMATE_IDLE);
    if (!s.action.empty()) return s.action;
    // without hvac_action: from the room and the target
    if (s.current) {
        const double heat_below = s.low ? *s.low : s.target.value_or(-1000);
        const double cool_above = s.high ? *s.high : s.target.value_or(1000);
        if ((s.mode == "heat" || s.mode == "heat_cool") && *s.current < heat_below) return tr(TR_CLIMATE_HEATING);
        if ((s.mode == "cool" || s.mode == "heat_cool") && *s.current > cool_above) return tr(TR_CLIMATE_COOLING);
    }
    return tr(TR_CLIMATE_IDLE);
}

ClimateDetail::ClimateDetail(Send send) : send_(std::move(send)) {}

ClimateDetail::~ClimateDetail()
{
    if (send_timer_) lv_timer_delete(send_timer_);
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_delete(root_);
}

void ClimateDetail::open(const State& state)
{
    if (root_) return;
    state_ = state;
    adjust_high_ = false;
    build();
    refresh();
}

void ClimateDetail::update(const State& state)
{
    if (!root_ || pending_) return; // a change of ours still on its way
    state_ = state;
    refresh();
}

void ClimateDetail::on_root_deleted()
{
    root_ = nullptr;
    if (send_timer_) {
        lv_timer_delete(send_timer_);
        send_timer_ = nullptr;
    }
    if (pending_) send_pending(); // closing must not lose the change
}

void ClimateDetail::build()
{
    ui_detail_t d = ui_detail_open(state_.name.c_str(), "");
    root_ = d.root;
    subtitle_ = d.subtitle;
    lv_obj_remove_flag(subtitle_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);

    // humidity pill
    humidity_ = ui_box(d.right);
    lv_obj_set_size(humidity_, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_radius(humidity_, 28, 0);
    lv_obj_set_style_bg_color(humidity_, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(humidity_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(humidity_, 18, 0);
    lv_obj_set_flex_flow(humidity_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(humidity_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(humidity_, 8, 0);
    ui_label(humidity_, UI_ICON_S, UI_TEXT2, LI_DROPLET);
    ui_label(humidity_, UI_FONT_BODY, UI_TEXT, "");

    lv_obj_t* c = d.content;
    lv_obj_t* big = ui_box(c);
    lv_obj_remove_flag(big, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(big, LV_PCT(100), kGauge);
    lv_obj_set_flex_flow(big, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(big, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(round_button(big, LI_MINUS), raw_minus_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* gauge = ui_box(big);
    lv_obj_remove_flag(gauge, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(gauge, kGauge, kGauge);
    arc_ = lv_arc_create(gauge);
    lv_obj_set_size(arc_, kGauge, kGauge);
    lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc_, 135);
    lv_arc_set_bg_angles(arc_, 0, 270);
    lv_obj_set_style_arc_width(arc_, kArcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, kArcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(UI_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_, 0, LV_PART_MAIN);
    dot_ = ui_box(gauge); // the room temperature on the scale
    lv_obj_set_size(dot_, 26, 26);
    lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot_, 4, 0);
    lv_obj_set_style_border_color(dot_, lv_color_black(), 0);

    single_ = ui_box(gauge);
    lv_obj_set_size(single_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(single_);
    lv_obj_set_flex_flow(single_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(single_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(single_, 4, 0);
    ui_label(single_, UI_FONT_LABEL, UI_TEXT2, tr(TR_CLIMATE_TARGET));
    target_label_ = ui_label(single_, UI_FONT_HUGE, UI_TEXT, "");
    lv_obj_set_style_text_letter_space(target_label_, -3, 0);
    room_label_ = ui_label(single_, UI_FONT_LABEL, UI_TEXT2, "");

    dual_ = ui_box(gauge);
    lv_obj_remove_flag(dual_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(dual_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(dual_);
    lv_obj_set_flex_flow(dual_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dual_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(dual_, 10, 0);
    lv_obj_t* pair = ui_box(dual_);
    lv_obj_remove_flag(pair, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(pair, LV_SIZE_CONTENT, 104);
    lv_obj_set_flex_flow(pair, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pair, 8, 0);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t* b = ui_box(pair);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(b, 116, 104);
        lv_obj_set_style_radius(b, UI_R_KEY, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE2), 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 2, 0);
        ui_label(b, UI_FONT_LABEL, i == 0 ? UI_HEAT : UI_COOL, tr(i == 0 ? TR_CLIMATE_HEAT_FROM : TR_CLIMATE_COOL_FROM));
        ui_label(b, UI_FONT_VALUE, UI_TEXT, "");
        lv_obj_add_event_cb(b, raw_bound_cb, LV_EVENT_CLICKED, this);
        bounds_[i] = b;
    }
    dual_room_ = ui_label(dual_, UI_FONT_LABEL, UI_TEXT2, "");

    lv_obj_add_event_cb(round_button(big, LI_PLUS), raw_plus_cb, LV_EVENT_CLICKED, this);

    // hvac modes the device has (at most four, in a fixed order)
    for (const char* m : {"off", "heat", "cool", "heat_cool", "auto", "dry", "fan_only"}) {
        if (shown_modes_.size() < kMaxModes &&
            std::find(state_.modes.begin(), state_.modes.end(), m) != state_.modes.end())
            shown_modes_.emplace_back(m);
    }
    if (!shown_modes_.empty()) {
        modes_row_ = ui_box(c);
        lv_obj_remove_flag(modes_row_, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(modes_row_, LV_PCT(100), 88);
        lv_obj_set_flex_flow(modes_row_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(modes_row_, 10, 0);
        for (const auto& m : shown_modes_) {
            lv_obj_t* b = choice_button(modes_row_, UI_R_KEY);
            lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_row(b, 6, 0);
            ui_label(b, UI_ICON_M, mode_color(m), mode_icon(m));
            // heat_cool and auto both exist: keep them apart
            const bool both = std::find(shown_modes_.begin(), shown_modes_.end(), "auto") != shown_modes_.end();
            ui_label(b, UI_FONT_NAME, UI_TEXT2, m == "heat_cool" && both ? tr(TR_CLIMATE_HEAT_COOL_SHORT) : mode_name(m));
            lv_obj_add_event_cb(b, raw_mode_cb, LV_EVENT_CLICKED, this);
        }
    }

    // presets (at most three) and the fan
    for (const auto& p : state_.presets) {
        if (shown_presets_.size() < 3 && p != "none") shown_presets_.push_back(p);
    }
    if (!state_.fan_modes.empty()) shown_presets_.emplace_back("fan");
    if (!shown_presets_.empty()) {
        presets_row_ = ui_box(c);
        lv_obj_remove_flag(presets_row_, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(presets_row_, LV_PCT(100), 72);
        lv_obj_set_flex_flow(presets_row_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(presets_row_, 10, 0);
        for (size_t i = 0; i < shown_presets_.size(); ++i) {
            lv_obj_t* b = choice_button(presets_row_, UI_R_KEY);
            lv_obj_center(ui_label(b, UI_FONT_NAME, UI_TEXT2, ""));
            lv_obj_add_event_cb(b, raw_preset_cb, LV_EVENT_CLICKED, this);
        }
    }
}

void ClimateDetail::refresh()
{
    if (!root_) return;
    const State& s = state_;
    lv_label_set_text(subtitle_, action_text(s).c_str());
    if (s.humidity) {
        lv_label_set_text(lv_obj_get_child(humidity_, 1), (format_number(*s.humidity, 0) + " %").c_str());
        lv_obj_remove_flag(humidity_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(humidity_, LV_OBJ_FLAG_HIDDEN);
    }

    const double span = s.max > s.min ? s.max - s.min : 1;
    auto frac = [&](double v) { return std::fmax(0.0, std::fmin(1.0, (v - s.min) / span)); };
    const bool dual = s.low && s.high && !s.target;
    const bool off = s.mode == "off";
    const double lo = dual ? frac(*s.low) : 0;
    const double hi = dual ? frac(*s.high) : frac(s.target.value_or(s.min));
    lv_obj_set_style_arc_color(arc_, lv_color_hex(off ? UI_ACTIVE : mode_color(s.mode)), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc_, off ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_angles(arc_, static_cast<lv_value_precise_t>(lo * 270),
                      static_cast<lv_value_precise_t>(std::fmax(hi * 270, lo * 270 + 1)));
    if (s.current) {
        const double a = (135 + 270 * frac(*s.current)) * M_PI / 180.0;
        const double r = (kGauge - kArcWidth) / 2.0;
        lv_obj_set_pos(dot_, static_cast<int32_t>(kGauge / 2 + r * std::cos(a) - 13),
                       static_cast<int32_t>(kGauge / 2 + r * std::sin(a) - 13));
        lv_obj_remove_flag(dot_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);
    }
    const std::string room = s.current ? trf(TR_CLIMATE_ROOM, {degrees(*s.current)}) : "";

    if (dual) {
        lv_obj_add_flag(single_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(dual_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(bounds_[0], 1), degrees(*s.low).c_str());
        lv_label_set_text(lv_obj_get_child(bounds_[1], 1), degrees(*s.high).c_str());
        lv_obj_set_style_bg_opa(bounds_[0], adjust_high_ ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(bounds_[1], adjust_high_ ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_label_set_text(dual_room_, room.c_str());
    } else {
        lv_obj_remove_flag(single_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dual_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(target_label_, off ? tr(TR_STATE_OFF) : s.target ? degrees(*s.target).c_str() : "–");
        lv_label_set_text(room_label_, room.c_str());
    }

    for (size_t i = 0; modes_row_ && i < shown_modes_.size(); ++i) {
        lv_obj_t* b = lv_obj_get_child(modes_row_, static_cast<int32_t>(i));
        const bool sel = shown_modes_[i] == s.mode;
        lv_obj_set_style_bg_color(b, lv_color_hex(sel ? UI_ACTIVE : UI_SURFACE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 1), lv_color_hex(sel ? UI_TEXT : UI_TEXT2), 0);
    }
    for (size_t i = 0; presets_row_ && i < shown_presets_.size(); ++i) {
        lv_obj_t* b = lv_obj_get_child(presets_row_, static_cast<int32_t>(i));
        const bool fan = shown_presets_[i] == "fan";
        const bool sel = fan ? s.fan_mode == "on" : shown_presets_[i] == s.preset;
        lv_label_set_text(lv_obj_get_child(b, 0), fan ? tr(s.fan_mode == "on" ? TR_CLIMATE_FAN_ON : TR_CLIMATE_FAN_AUTO)
                                                      : preset_name(shown_presets_[i]));
        lv_obj_set_style_bg_color(b, lv_color_hex(sel ? UI_ACTIVE : UI_SURFACE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(sel ? UI_TEXT : UI_TEXT2), 0);
    }
}

void ClimateDetail::on_step(int direction)
{
    State& s = state_;
    if (s.mode == "off") return;
    auto bounded = [&](double v) { return std::fmax(s.min, std::fmin(s.max, v)); };
    const double d = direction * s.step;
    if (s.low && s.high && !s.target) {
        if (adjust_high_) s.high = bounded(std::fmax(*s.low + s.step, *s.high + d));
        else s.low = bounded(std::fmin(*s.high - s.step, *s.low + d));
    } else {
        s.target = bounded(s.target.value_or(s.current.value_or(20)) + d);
    }
    pending_ = true;
    refresh();
    if (send_timer_) lv_timer_reset(send_timer_);
    else {
        send_timer_ = lv_timer_create(raw_send_cb, kSendDelayMs, this);
        lv_timer_set_repeat_count(send_timer_, 1);
    }
}

void ClimateDetail::send_pending()
{
    send_timer_ = nullptr; // one-shot timer: LVGL deletes it after this call
    if (!pending_) return;
    pending_ = false;
    char json[96];
    if (state_.low && state_.high && !state_.target)
        std::snprintf(json, sizeof(json), "{\"target_temp_low\":%.1f,\"target_temp_high\":%.1f}", *state_.low,
                      *state_.high);
    else
        std::snprintf(json, sizeof(json), "{\"temperature\":%.1f}", state_.target.value_or(20));
    send_("set_temperature", json);
}

void ClimateDetail::on_select_bound(bool high)
{
    adjust_high_ = high;
    refresh();
}

void ClimateDetail::on_mode(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= shown_modes_.size()) return;
    state_.mode = shown_modes_[static_cast<size_t>(index)];
    std::string json = "{\"hvac_mode\":\"" + state_.mode + "\"}";
    send_("set_hvac_mode", json);
    refresh();
}

void ClimateDetail::on_preset(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= shown_presets_.size()) return;
    const std::string& p = shown_presets_[static_cast<size_t>(index)];
    if (p == "fan") {
        const bool on = state_.fan_mode != "on";
        const bool has_on = std::find(state_.fan_modes.begin(), state_.fan_modes.end(), "on") != state_.fan_modes.end();
        state_.fan_mode = on && has_on ? "on" : "auto";
        send_("set_fan_mode", "{\"fan_mode\":\"" + state_.fan_mode + "\"}");
    } else {
        state_.preset = p;
        send_("set_preset_mode", "{\"preset_mode\":\"" + p + "\"}");
    }
    refresh();
}

} // namespace app::ui
