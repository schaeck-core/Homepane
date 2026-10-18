#include "LightDetail.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app::ui {

namespace {
constexpr int kStrips = 36; // the hue field: 36 two-stop gradients (ESP: 2 stops per gradient)

struct Favourite {
    int hue, sat;
};
constexpr Favourite kFavourites[] = {{30, 70}, {42, 40}, {200, 45}, {265, 40}, {340, 38}, {150, 30}};
struct White {
    int kelvin;
    tr_key_t label;
};
constexpr White kWhites[] = {{2200, TR_LIGHT_WHITE_CANDLE},
                             {2700, TR_LIGHT_WHITE_WARM},
                             {4000, TR_LIGHT_WHITE_NEUTRAL},
                             {5000, TR_LIGHT_WHITE_COOL},
                             {6500, TR_LIGHT_WHITE_DAY}};
struct Profile {
    tr_key_t label;
    int kelvin, brightness_pct;
};
constexpr Profile kProfiles[] = {{TR_LIGHT_PROFILE_RELAX, 2200, 50},
                                 {TR_LIGHT_PROFILE_READ, 4000, 100},
                                 {TR_LIGHT_PROFILE_FOCUS, 5000, 100},
                                 {TR_LIGHT_PROFILE_NIGHT, 2200, 5}};
// the band: warm .. cool, 5 segments
constexpr uint32_t kBandStops[] = {0xE39F5C, 0xEDC48F, 0xF3E2C4, 0xEFEEEA, 0xD3E0EA, 0xB6CCE0};
constexpr int kBandMinK = 2200, kBandMaxK = 6500;

LightDetail* self_of(lv_event_t* e) { return static_cast<LightDetail*>(lv_event_get_user_data(e)); }

void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }
void raw_toggle_cb(lv_event_t* e) { self_of(e)->on_toggle(); }
void raw_tab_cb(lv_event_t* e) { self_of(e)->on_tab(); }
void raw_field_cb(lv_event_t* e) { self_of(e)->on_field(lv_event_get_code(e) == LV_EVENT_RELEASED); }
void raw_band_cb(lv_event_t* e) { self_of(e)->on_band(lv_event_get_code(e) == LV_EVENT_RELEASED); }
void raw_brightness_cb(lv_event_t* e) { self_of(e)->on_brightness(lv_event_get_code(e) == LV_EVENT_RELEASED); }
void raw_favourite_cb(lv_event_t* e)
{
    self_of(e)->on_favourite(static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))));
}
void raw_white_cb(lv_event_t* e)
{
    self_of(e)->on_white(static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))));
}
void raw_profile_cb(lv_event_t* e)
{
    self_of(e)->on_profile(static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))));
}

// The pointer position inside `obj` as fractions 0..1.
void pointer_fraction(lv_obj_t* obj, double* fx, double* fy)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const double w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    *fx = w > 0 ? std::fmax(0.0, std::fmin(1.0, (p.x - a.x1) / w)) : 0;
    *fy = h > 0 ? std::fmax(0.0, std::fmin(1.0, (p.y - a.y1) / h)) : 0;
}

lv_obj_t* control_area(lv_obj_t* parent)
{
    lv_obj_t* o = ui_box(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_width(o, LV_PCT(100));
    lv_obj_set_flex_grow(o, 1);
    lv_obj_set_style_radius(o, UI_R_TILE, 0);
    lv_obj_set_style_clip_corner(o, true, 0);
    return o;
}

lv_obj_t* grid_row(lv_obj_t* parent, int32_t height, int32_t gap)
{
    lv_obj_t* r = ui_box(parent);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(r, LV_PCT(100), height);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

lv_obj_t* grid_button(lv_obj_t* row, int32_t radius)
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

uint32_t LightDetail::hsl(double h, double s, double l)
{
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360;
    s /= 100.0;
    l /= 100.0;
    const double c = (1 - std::fabs(2 * l - 1)) * s;
    const double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2) - 1));
    const double m = l - c / 2;
    double r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    auto byte = [&](double v) { return static_cast<uint32_t>(std::lround((v + m) * 255)) & 0xFF; };
    return (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

uint32_t LightDetail::kelvin_color(int kelvin)
{
    const double t = std::fmax(0.0, std::fmin(1.0, (kelvin - 2200) / 4300.0));
    const int a[] = {229, 168, 106}, b[] = {242, 226, 198}, c[] = {190, 209, 226};
    int rgb[3];
    for (int i = 0; i < 3; ++i) {
        rgb[i] = t < 0.45 ? static_cast<int>(a[i] + (b[i] - a[i]) * (t / 0.45))
                          : static_cast<int>(b[i] + (c[i] - b[i]) * ((t - 0.45) / 0.55));
    }
    return (static_cast<uint32_t>(rgb[0]) << 16) | (static_cast<uint32_t>(rgb[1]) << 8) | static_cast<uint32_t>(rgb[2]);
}

const char* LightDetail::kelvin_name(int k)
{
    return tr(k < 2500 ? TR_KELVIN_CANDLE : k < 3300 ? TR_KELVIN_WARM : k < 4500 ? TR_KELVIN_NEUTRAL
              : k < 5800 ? TR_KELVIN_COOL : TR_KELVIN_DAY);
}

LightDetail::LightDetail(Send send, std::function<void()> on_closed)
    : send_(std::move(send)), on_closed_(std::move(on_closed))
{
}

LightDetail::~LightDetail()
{
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_delete(root_);
    root_ = nullptr;
}

void LightDetail::open(const State& state)
{
    if (root_) return;
    state_ = state;
    const bool colourful = state.color_mode == "hs" || state.color_mode == "rgb" || state.color_mode == "xy" ||
                           state.color_mode == "rgbw" || state.color_mode == "rgbww";
    color_tab_ = state.supports_color && (colourful || !state.supports_temperature);
    build();
    refresh();
}

void LightDetail::update(const State& state)
{
    if (!root_ || dragging_) return; // never fight the finger
    state_ = state;
    refresh();
}

void LightDetail::on_root_deleted()
{
    root_ = nullptr;
    if (on_closed_) on_closed_();
}

void LightDetail::build()
{
    ui_detail_t d = ui_detail_open(state_.name.c_str(), "");
    root_ = d.root;
    subtitle_ = d.subtitle;
    lv_obj_remove_flag(subtitle_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
    toggle_ = ui_toggle_large(d.right);
    lv_obj_add_event_cb(toggle_, raw_toggle_cb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_t* c = d.content;

    if (state_.supports_color && state_.supports_temperature) {
        const char* const tabs[] = {tr(TR_LIGHT_COLOR), tr(TR_LIGHT_WHITE)};
        tabs_ = ui_segmented(c, tabs, 2);
        lv_obj_add_event_cb(tabs_, raw_tab_cb, LV_EVENT_VALUE_CHANGED, this);
    }

    if (state_.supports_color) {
        // hue left to right, white at the top fading to the full colour below
        field_ = control_area(c);
        lv_obj_set_flex_flow(field_, LV_FLEX_FLOW_ROW);
        for (int i = 0; i < kStrips; ++i) {
            lv_obj_t* s = ui_box(field_);
            lv_obj_set_height(s, LV_PCT(100));
            lv_obj_set_flex_grow(s, 1);
            lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(s, lv_color_hex(hsl(i * 360.0 / kStrips, 78, 56)), 0);
            lv_obj_set_style_bg_grad_color(s, lv_color_hex(hsl((i + 1) * 360.0 / kStrips, 78, 56)), 0);
            lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, 0);
        }
        lv_obj_t* white = ui_box(field_);
        lv_obj_add_flag(white, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(white, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(white, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_color(white, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_dir(white, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_opa(white, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_opa(white, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(white, LV_OPA_COVER, 0);
        field_knob_ = ui_box(field_);
        lv_obj_add_flag(field_knob_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(field_knob_, 44, 44);
        lv_obj_set_style_radius(field_knob_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(field_knob_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(field_knob_, 5, 0);
        lv_obj_set_style_border_color(field_knob_, lv_color_white(), 0);
        lv_obj_set_style_outline_width(field_knob_, 2, 0);
        lv_obj_set_style_outline_color(field_knob_, lv_color_black(), 0);
        for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
            lv_obj_add_event_cb(field_, raw_field_cb, code, this);
    }

    if (state_.supports_temperature) {
        band_ = control_area(c);
        lv_obj_set_flex_flow(band_, LV_FLEX_FLOW_ROW);
        for (int i = 0; i < 5; ++i) {
            lv_obj_t* s = ui_box(band_);
            lv_obj_set_height(s, LV_PCT(100));
            lv_obj_set_flex_grow(s, 1);
            lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(s, lv_color_hex(kBandStops[i]), 0);
            lv_obj_set_style_bg_grad_color(s, lv_color_hex(kBandStops[i + 1]), 0);
            lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, 0);
        }
        lv_obj_t* warm = ui_label(band_, UI_FONT_LABEL, 0x3A2F22, tr(TR_LIGHT_WARM));
        lv_obj_add_flag(warm, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(warm, LV_ALIGN_TOP_LEFT, 20, 16);
        lv_obj_t* cold = ui_label(band_, UI_FONT_LABEL, 0x2A3540, tr(TR_LIGHT_COLD));
        lv_obj_add_flag(cold, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(cold, LV_ALIGN_TOP_RIGHT, -20, 16);
        band_marker_ = ui_box(band_);
        lv_obj_add_flag(band_marker_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_style_radius(band_marker_, 6, 0);
        lv_obj_set_style_bg_color(band_marker_, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(band_marker_, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_width(band_marker_, 3, 0);
        lv_obj_set_style_outline_color(band_marker_, lv_color_black(), 0);
        band_pill_ = ui_box(band_);
        lv_obj_add_flag(band_pill_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(band_pill_, LV_SIZE_CONTENT, 48);
        lv_obj_align(band_pill_, LV_ALIGN_BOTTOM_LEFT, 20, -16);
        lv_obj_set_style_radius(band_pill_, 24, 0);
        lv_obj_set_style_bg_color(band_pill_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(band_pill_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(band_pill_, 18, 0);
        lv_obj_set_flex_flow(band_pill_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(band_pill_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(band_pill_, 10, 0);
        ui_label(band_pill_, UI_FONT_NAME, UI_TEXT, "");
        ui_label(band_pill_, UI_FONT_LABEL, UI_TEXT2, "");
        for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
            lv_obj_add_event_cb(band_, raw_band_cb, code, this);
        lv_obj_move_foreground(band_pill_); // the value pill above the marker
    }

    if (state_.supports_brightness) {
        brightness_ = ui_slider_row(c, LI_SUN_DIM, tr(TR_LIGHT_BRIGHTNESS));
        lv_obj_add_event_cb(brightness_, raw_brightness_cb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(brightness_, raw_brightness_cb, LV_EVENT_RELEASED, this);
    }

    if (state_.supports_color) {
        favourites_ = grid_row(c, 80, 12);
        for (const auto& f : kFavourites) {
            lv_obj_t* b = grid_button(favourites_, 40);
            lv_obj_set_style_bg_color(b, lv_color_hex(hsl(f.hue, f.sat * 0.78, 100 - f.sat * 0.44)), 0);
            lv_obj_set_style_outline_color(b, lv_color_hex(UI_TEXT), 0);
            lv_obj_set_style_outline_pad(b, 4, 0);
            lv_obj_add_event_cb(b, raw_favourite_cb, LV_EVENT_CLICKED, this);
        }
    }
    if (state_.supports_temperature) {
        whites_ = grid_row(c, 96, 10);
        for (const auto& w : kWhites) {
            lv_obj_t* b = grid_button(whites_, UI_R_CONTROL);
            lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_row(b, 8, 0);
            lv_obj_t* swatch = ui_box(b);
            lv_obj_set_size(swatch, 32, 32);
            lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(swatch, lv_color_hex(kelvin_color(w.kelvin)), 0);
            lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
            ui_label(b, UI_FONT_LABEL, UI_TEXT2, tr(w.label));
            lv_obj_add_event_cb(b, raw_white_cb, LV_EVENT_CLICKED, this);
        }
        profiles_ = grid_row(c, 72, 10);
        for (const auto& p : kProfiles) {
            lv_obj_t* b = grid_button(profiles_, UI_R_KEY);
            lv_obj_center(ui_label(b, UI_FONT_LABEL, UI_TEXT2, tr(p.label)));
            lv_obj_add_event_cb(b, raw_profile_cb, LV_EVENT_CLICKED, this);
        }
    }
    show_tab(color_tab_);
}

void LightDetail::show_tab(bool color)
{
    color_tab_ = color;
    auto show = [](lv_obj_t* o, bool visible) {
        if (!o) return;
        if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    show(field_, color);
    show(favourites_, color);
    show(band_, !color);
    show(whites_, !color);
    show(profiles_, !color);
    if (tabs_) ui_segmented_select(tabs_, color ? 0 : 1);
}

void LightDetail::refresh()
{
    if (!root_) return;
    const int pct = (state_.brightness * 100 + 127) / 255;
    char buf[48];
    if (state_.on) std::snprintf(buf, sizeof(buf), "%d %%", pct);
    else std::snprintf(buf, sizeof(buf), "%s", tr(TR_STATE_OFF));
    lv_label_set_text(subtitle_, buf);
    if (state_.on) lv_obj_add_state(toggle_, LV_STATE_CHECKED);
    else lv_obj_remove_state(toggle_, LV_STATE_CHECKED);
    if (brightness_) ui_slider_row_set(brightness_, state_.on ? pct : 0, buf);

    const bool colourful = state_.color_mode == "hs" || state_.color_mode == "rgb" || state_.color_mode == "xy" ||
                           state_.color_mode == "rgbw" || state_.color_mode == "rgbww";
    if (field_) place_field_knob();
    if (band_) place_band_marker();
    if (favourites_) {
        for (size_t i = 0; i < sizeof(kFavourites) / sizeof(kFavourites[0]); ++i) {
            const bool sel = colourful && std::lround(state_.hue) == kFavourites[i].hue &&
                             std::lround(state_.saturation) == kFavourites[i].sat;
            lv_obj_set_style_outline_width(lv_obj_get_child(favourites_, static_cast<int32_t>(i)), sel ? 3 : 0, 0);
        }
    }
    if (whites_) {
        for (size_t i = 0; i < sizeof(kWhites) / sizeof(kWhites[0]); ++i) {
            lv_obj_t* b = lv_obj_get_child(whites_, static_cast<int32_t>(i));
            const bool sel = !colourful && state_.kelvin == kWhites[i].kelvin;
            lv_obj_set_style_bg_color(b, lv_color_hex(sel ? UI_ACTIVE : UI_SURFACE), 0);
            lv_obj_set_style_border_width(b, sel ? 2 : 0, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(UI_TEXT), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(b, 1), lv_color_hex(sel ? UI_TEXT : UI_TEXT2), 0);
        }
    }
    if (profiles_) {
        for (size_t i = 0; i < sizeof(kProfiles) / sizeof(kProfiles[0]); ++i) {
            lv_obj_t* b = lv_obj_get_child(profiles_, static_cast<int32_t>(i));
            const bool sel = state_.on && !colourful && state_.kelvin == kProfiles[i].kelvin &&
                             pct == kProfiles[i].brightness_pct;
            lv_obj_set_style_bg_color(b, lv_color_hex(sel ? UI_ACTIVE : UI_SURFACE), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(sel ? UI_TEXT : UI_TEXT2), 0);
        }
    }
}

void LightDetail::place_field_knob()
{
    lv_obj_update_layout(field_);
    const int32_t w = lv_obj_get_width(field_), h = lv_obj_get_height(field_);
    lv_obj_set_pos(field_knob_, static_cast<int32_t>(state_.hue / 360.0 * w) - 22,
                   static_cast<int32_t>(state_.saturation / 100.0 * h) - 22);
    lv_obj_set_style_bg_color(field_knob_,
                              lv_color_hex(hsl(state_.hue, state_.saturation * 0.78, 100 - state_.saturation * 0.44)), 0);
}

void LightDetail::place_band_marker()
{
    lv_obj_update_layout(band_);
    const int32_t w = lv_obj_get_width(band_), h = lv_obj_get_height(band_);
    const double t = std::fmax(0.0, std::fmin(1.0, (state_.kelvin - kBandMinK) / double(kBandMaxK - kBandMinK)));
    char k[16];
    std::snprintf(k, sizeof(k), "%d K", state_.kelvin);
    lv_label_set_text(lv_obj_get_child(band_pill_, 0), k);
    lv_label_set_text(lv_obj_get_child(band_pill_, 1), kelvin_name(state_.kelvin));
    // The marker ends above the value pill where they would overlap: no
    // reliance on drawing order (the ESP's parallel draw units).
    lv_obj_update_layout(band_pill_);
    const int32_t x = static_cast<int32_t>(t * w) - 6;
    const int32_t pill_left = lv_obj_get_x(band_pill_), pill_right = pill_left + lv_obj_get_width(band_pill_);
    const bool over_pill = x + 12 + 6 > pill_left && x - 6 < pill_right;
    const int32_t bottom = over_pill ? lv_obj_get_y(band_pill_) - 10 : h - 12;
    lv_obj_set_size(band_marker_, 12, bottom - 12);
    lv_obj_set_pos(band_marker_, x, 12);
}

void LightDetail::on_toggle()
{
    state_.on = lv_obj_has_state(toggle_, LV_STATE_CHECKED);
    send_(state_.on ? "turn_on" : "turn_off", "");
    refresh();
}

void LightDetail::on_tab() { show_tab(ui_segmented_selected(tabs_) == 0); }

void LightDetail::on_field(bool released)
{
    double fx, fy;
    pointer_fraction(field_, &fx, &fy);
    dragging_ = !released;
    state_.hue = std::lround(fx * 360);
    state_.saturation = std::lround(fy * 100);
    state_.color_mode = "hs";
    state_.on = true;
    place_field_knob();
    if (!released) return;
    char json[64];
    std::snprintf(json, sizeof(json), "{\"hs_color\":[%.0f,%.0f]}", state_.hue, state_.saturation);
    send_("turn_on", json);
    refresh();
}

void LightDetail::on_band(bool released)
{
    double fx, fy;
    pointer_fraction(band_, &fx, &fy);
    dragging_ = !released;
    const int lo = std::max(kBandMinK, state_.min_kelvin), hi = std::min(kBandMaxK, state_.max_kelvin);
    int k = static_cast<int>(std::lround((kBandMinK + fx * (kBandMaxK - kBandMinK)) / 100.0) * 100);
    k = std::max(lo, std::min(hi, k));
    state_.kelvin = k;
    state_.color_mode = "color_temp";
    state_.on = true;
    place_band_marker();
    if (!released) return;
    char json[48];
    std::snprintf(json, sizeof(json), "{\"color_temp_kelvin\":%d}", k);
    send_("turn_on", json);
    refresh();
}

void LightDetail::on_brightness(bool released)
{
    const int pct = std::max(1, ui_slider_row_value(brightness_));
    dragging_ = !released;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d %%", pct);
    lv_label_set_text(lv_obj_get_child(brightness_, 2), buf);
    if (!released) return;
    state_.on = true;
    state_.brightness = pct * 255 / 100;
    char json[40];
    std::snprintf(json, sizeof(json), "{\"brightness_pct\":%d}", pct);
    send_("turn_on", json);
    refresh();
}

void LightDetail::on_favourite(int i)
{
    state_.hue = kFavourites[i].hue;
    state_.saturation = kFavourites[i].sat;
    state_.color_mode = "hs";
    state_.on = true;
    char json[64];
    std::snprintf(json, sizeof(json), "{\"hs_color\":[%d,%d]}", kFavourites[i].hue, kFavourites[i].sat);
    send_("turn_on", json);
    refresh();
}

void LightDetail::on_white(int i)
{
    state_.kelvin = kWhites[i].kelvin;
    state_.color_mode = "color_temp";
    state_.on = true;
    char json[48];
    std::snprintf(json, sizeof(json), "{\"color_temp_kelvin\":%d}", kWhites[i].kelvin);
    send_("turn_on", json);
    refresh();
}

void LightDetail::on_profile(int i)
{
    state_.kelvin = kProfiles[i].kelvin;
    state_.brightness = kProfiles[i].brightness_pct * 255 / 100;
    state_.color_mode = "color_temp";
    state_.on = true;
    char json[64];
    std::snprintf(json, sizeof(json), "{\"color_temp_kelvin\":%d,\"brightness_pct\":%d}", kProfiles[i].kelvin,
                  kProfiles[i].brightness_pct);
    send_("turn_on", json);
    refresh();
}

} // namespace app::ui
