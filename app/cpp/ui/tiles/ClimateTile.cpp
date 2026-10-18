#include "ClimateTile.h"

#include "../details/ClimateDetail.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"


#include <cmath>
#include <cstdio>

namespace app::ui {

namespace {
constexpr int32_t kGauge = 164;
constexpr int32_t kArcWidth = 12;
constexpr int32_t kKnob = 22;

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

std::string degrees(double v)
{
    const bool whole = std::fabs(v - std::round(v)) < 0.05;
    return format_number(v, whole ? 0 : 1) + "°";
}

const char* mode_text(const std::string& mode)
{
    if (mode == "heat") return tr(TR_CLIMATE_HEAT);
    if (mode == "cool") return tr(TR_CLIMATE_COOL);
    if (mode == "auto") return tr(TR_CLIMATE_AUTOMATIC);
    if (mode == "dry") return tr(TR_CLIMATE_DRY);
    if (mode == "fan_only") return tr(TR_CLIMATE_FAN_ONLY);
    if (mode == "eco") return tr(TR_CLIMATE_ECO);
    return "";
}

void raw_minus_cb(lv_event_t* e) { static_cast<ClimateTile*>(lv_event_get_user_data(e))->step(-1); }
void raw_plus_cb(lv_event_t* e) { static_cast<ClimateTile*>(lv_event_get_user_data(e))->step(+1); }
void raw_send_cb(lv_timer_t* t) { static_cast<ClimateTile*>(lv_timer_get_user_data(t))->send_now(); }
} // namespace

ClimateTile::View ClimateTile::view_for(const Model& m)
{
    View v;
    const double span = m.max > m.min ? m.max - m.min : 1;
    auto frac = [&](double x) { return clamp01((x - m.min) / span); };

    if (m.mode == "off") {
        v.main = tr(TR_STATE_OFF);
        v.sub = tr(m.humidity ? TR_CLIMATE_HUMIDIFIER_OFF : TR_CLIMATE_OFF);
        v.arc_visible = false;
        v.color = UI_ACTIVE;
        return v;
    }
    if (m.humidity) {
        const double t = m.target.value_or(m.min);
        v.main = format_number(t, 0) + " %";
        v.sub = m.current ? trf(TR_CLIMATE_NOW, {format_number(*m.current, 0) + " %"}) : "";
        v.arc_to = v.knob = frac(t);
        v.color = UI_COOL;
        return v;
    }
    if (m.low && m.high) {
        // heat_cool: the bound the room is closer to (or outside of) is
        // the one shown big and moved by -/+.
        const double cur = m.current.value_or((*m.low + *m.high) / 2);
        bool high;
        if (cur < *m.low) high = false;
        else if (cur > *m.high) high = true;
        else high = (cur - *m.low) > (*m.high - cur);
        v.adjusts_high = high;
        v.main = degrees(high ? *m.high : *m.low);
        v.sub = high ? trf(TR_CLIMATE_HEAT_BELOW, {degrees(*m.low)}) : trf(TR_CLIMATE_COOL_ABOVE, {degrees(*m.high)});
        v.arc_from = frac(*m.low);
        v.arc_to = frac(*m.high);
        v.knob = frac(high ? *m.high : *m.low);
        v.color = high ? UI_COOL : UI_HEAT;
        return v;
    }
    const double t = m.target.value_or(m.current.value_or(m.min));
    v.main = degrees(t);
    v.sub = mode_text(m.mode);
    if (m.action == "heating") v.sub = tr(TR_CLIMATE_HEATING);
    else if (m.action == "cooling") v.sub = tr(TR_CLIMATE_COOLING);
    v.arc_to = v.knob = frac(t);
    v.color = m.mode == "cool" ? UI_COOL : m.mode == "auto" ? UI_AUTO : UI_HEAT;
    return v;
}

ClimateTile::Model ClimateTile::model() const
{
    Model m;
    m.humidity = domain() == "humidifier";
    m.mode = state();
    m.action = attr("hvac_action");
    if (m.humidity) {
        m.min = attr_number("min_humidity").value_or(0);
        m.max = attr_number("max_humidity").value_or(100);
        m.step = 5;
        m.current = attr_number("current_humidity");
        m.target = pending_target_ ? pending_target_ : attr_number("humidity");
        return m;
    }
    m.min = attr_number("min_temp").value_or(7);
    m.max = attr_number("max_temp").value_or(30);
    m.step = attr_number("target_temp_step").value_or(0.5);
    m.current = attr_number("current_temperature");
    m.target = pending_target_ ? pending_target_ : attr_number("temperature");
    if (!m.target) {
        m.low = pending_low_ ? pending_low_ : attr_number("target_temp_low");
        m.high = pending_high_ ? pending_high_ : attr_number("target_temp_high");
    }
    return m;
}

void ClimateTile::build(lv_obj_t* tile)
{
    lv_obj_set_style_pad_row(tile, 8, 0);
    lv_obj_t* body = ui_box(tile);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 4, 0);
    name_label_ = ui_label_line(body, UI_FONT_NAME, UI_TEXT, "");

    lv_obj_t* gauge = ui_box(body);
    lv_obj_set_size(gauge, kGauge, kGauge);
    arc_ = lv_arc_create(gauge);
    lv_obj_set_size(arc_, kGauge, kGauge);
    lv_obj_center(arc_);
    lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc_, LV_OBJ_FLAG_EVENT_BUBBLE);
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
    arc_image_ = lv_image_create(gauge);
    lv_obj_add_flag(arc_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(arc_image_, LV_OBJ_FLAG_EVENT_BUBBLE);

    knob_ = ui_box(gauge);
    lv_obj_set_size(knob_, kKnob, kKnob);
    lv_obj_set_style_radius(knob_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(knob_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(knob_, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_border_width(knob_, 4, 0);
    lv_obj_set_style_border_color(knob_, lv_color_hex(UI_SURFACE), 0);

    lv_obj_t* center = ui_box(gauge);
    lv_obj_set_size(center, 140, LV_SIZE_CONTENT); // the arc is ~144 px wide at the label
    lv_obj_center(center);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center, 2, 0);
    main_label_ = ui_label(center, UI_FONT_VALUE, UI_TEXT, "");
    sub_label_ = ui_label(center, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_width(sub_label_, LV_PCT(100));
    lv_obj_set_style_text_align(sub_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sub_label_, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t* row = ui_button_row(tile);
    lv_obj_add_event_cb(ui_tile_button(row, LI_MINUS, nullptr, false), raw_minus_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(ui_tile_button(row, LI_PLUS, nullptr, false), raw_plus_cb, LV_EVENT_CLICKED, this);
}

void ClimateTile::update()
{
    if (!arc_) return;
    apply();
    bake_arc();
}

// The arc as a picture: drawn once per change (snapshot of the hidden arc),
// then only copied on redraws.
void ClimateTile::bake_arc()
{
    char key[96];
    std::snprintf(key, sizeof(key), "%d %d %d %06lx %d", static_cast<int>(lv_arc_get_angle_start(arc_)),
                  static_cast<int>(lv_arc_get_angle_end(arc_)), lv_obj_get_style_arc_opa(arc_, LV_PART_INDICATOR),
                  static_cast<unsigned long>(lv_color_to_u32(lv_obj_get_style_arc_color(arc_, LV_PART_INDICATOR)) & 0xFFFFFF),
                  static_cast<int>(lv_obj_get_width(arc_)));
    if (arc_buf_ && arc_baked_ == key) return;
    lv_obj_update_layout(arc_);
    if (lv_obj_get_width(arc_) <= 0) return; // not laid out yet: stays live
    lv_obj_remove_flag(arc_, LV_OBJ_FLAG_HIDDEN);
    lv_draw_buf_t* buf = lv_snapshot_take(arc_, LV_COLOR_FORMAT_ARGB8888);
    if (!buf) return; // out of memory: the live arc
    free_arc_buf();
    arc_buf_ = buf;
    arc_baked_ = key;
    lv_image_set_src(arc_image_, buf);
    lv_obj_center(arc_image_);
    lv_obj_remove_flag(arc_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(arc_, LV_OBJ_FLAG_HIDDEN);
}

void ClimateTile::free_arc_buf()
{
    if (!arc_buf_) return;
    lv_image_cache_drop(arc_buf_);
    lv_draw_buf_destroy(arc_buf_);
    arc_buf_ = nullptr;
}

void ClimateTile::apply()
{
    lv_label_set_text(name_label_, name().c_str());
    if (!loaded()) {
        lv_label_set_text(main_label_, "…");
        lv_label_set_text(sub_label_, "");
        lv_obj_add_flag(knob_, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_angles(arc_, 0, 0);
        return;
    }
    if (!available()) {
        lv_label_set_text(main_label_, "–");
        lv_label_set_text(sub_label_, tr(TR_STATE_UNAVAILABLE));
        lv_obj_add_flag(knob_, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_angles(arc_, 0, 0);
        return;
    }
    const View v = view_for(model());
    lv_label_set_text(main_label_, v.main.c_str());
    lv_label_set_text(sub_label_, v.sub.c_str());
    lv_obj_set_style_arc_color(arc_, lv_color_hex(v.color), LV_PART_INDICATOR);
    if (!v.arc_visible) {
        lv_obj_set_style_arc_opa(arc_, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_add_flag(knob_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_style_arc_opa(arc_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_angles(arc_, static_cast<lv_value_precise_t>(v.arc_from * 270),
                      static_cast<lv_value_precise_t>(std::fmax(v.arc_to * 270, v.arc_from * 270 + 1)));
    const double angle = (135 + 270 * v.knob) * M_PI / 180.0;
    const double r = (kGauge - kArcWidth) / 2.0;
    lv_obj_set_pos(knob_, static_cast<int32_t>(kGauge / 2 + r * std::cos(angle) - kKnob / 2),
                   static_cast<int32_t>(kGauge / 2 + r * std::sin(angle) - kKnob / 2));
    lv_obj_remove_flag(knob_, LV_OBJ_FLAG_HIDDEN);
}

void ClimateTile::on_data(const DataUpdate& u)
{
    // A pending change waits for its own send; an update after it is HA's answer.
    if (u.entity_state && !send_timer_) pending_target_ = pending_low_ = pending_high_ = std::nullopt;
    TileWidget::on_data(u);
    if (detail_ && detail_->is_open() && u.entity_state) open_detail(); // refreshes the open page
}

void ClimateTile::shutdown_ui()
{
    if (arc_image_) lv_image_set_src(arc_image_, nullptr);
    free_arc_buf();
    arc_ = arc_image_ = knob_ = nullptr;
    if (send_timer_) lv_timer_delete(send_timer_);
    send_timer_ = nullptr;
    detail_.reset(); // the page lives on the screen, not under the tile
    TileWidget::shutdown_ui();
}

void ClimateTile::step(int direction)
{
    if (!loaded() || !available() || state() == "off") return;
    const Model m = model();
    const View v = view_for(m);
    auto bounded = [&](double x) { return std::fmax(m.min, std::fmin(m.max, x)); };
    const double d = direction * m.step;
    if (m.low && m.high) {
        if (v.adjusts_high) pending_high_ = bounded(std::fmax(*m.low + m.step, *m.high + d));
        else pending_low_ = bounded(std::fmin(*m.high - m.step, *m.low + d));
        if (!pending_high_) pending_high_ = m.high;
        if (!pending_low_) pending_low_ = m.low;
    } else {
        pending_target_ = bounded(m.target.value_or(m.min) + d);
    }
    update();
    if (send_timer_) lv_timer_reset(send_timer_);
    else {
        send_timer_ = lv_timer_create(raw_send_cb, kSendDelayMs, this);
        lv_timer_set_repeat_count(send_timer_, 1);
    }
}

void ClimateTile::send_now()
{
    send_timer_ = nullptr; // one-shot: LVGL deletes it after this call
    char json[96];
    if (domain() == "humidifier" && pending_target_) {
        std::snprintf(json, sizeof(json), "{\"humidity\":%.0f}", *pending_target_);
        call("set_humidity", json);
    } else if (pending_low_ && pending_high_) {
        std::snprintf(json, sizeof(json), "{\"target_temp_low\":%.1f,\"target_temp_high\":%.1f}", *pending_low_,
                      *pending_high_);
        call("set_temperature", json);
    } else if (pending_target_) {
        std::snprintf(json, sizeof(json), "{\"temperature\":%.1f}", *pending_target_);
        call("set_temperature", json);
    }
}

void ClimateTile::on_command_failed() { pending_target_ = pending_low_ = pending_high_ = std::nullopt; }

ClimateTile::~ClimateTile() { free_arc_buf(); }

void ClimateTile::on_tap()
{
    if (!loaded() || !available()) return;
    if (domain() != "climate") {
        // humidifier / water heater: no detail page yet, the room value as a hint
        const auto cur = attr_number(domain() == "humidifier" ? "current_humidity" : "current_temperature");
        if (cur) ui_toast(trf(TR_CLIMATE_ROOM, {value_text(*cur)}).c_str());
        return;
    }
    open_detail();
}

void ClimateTile::open_detail()
{
    const ClimateDetail::State st = climate_detail_state(entity(), name(), state());
    if (!detail_) detail_ = std::make_unique<ClimateDetail>([this](const std::string& service, const std::string& json) {
        call(service, json);
    });
    if (detail_->is_open()) detail_->update(st);
    else detail_->open(st);
}

std::string ClimateTile::value_text(double v) const
{
    return domain() == "humidifier" ? format_number(v, 0) + " %" : degrees(v);
}

} // namespace app::ui
