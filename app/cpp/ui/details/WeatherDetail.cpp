#include "WeatherDetail.h"

#include "../../core/DateText.h"

#include "../tiles/TileWidget.h"
#include "../tiles/ValueTile.h"
#include "MediaDetail.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cmath>
#include <ctime>

namespace app::ui {

namespace {
constexpr size_t kHours = 6, kDays = 5;

void raw_deleted_cb(lv_event_t* e) { static_cast<WeatherDetail*>(lv_event_get_user_data(e))->on_root_deleted(); }

std::string deg(std::optional<double> v) { return v ? format_number(*v, 0) + "°" : "–"; }

lv_obj_t* card(lv_obj_t* parent)
{
    lv_obj_t* c = ui_box(parent);
    lv_obj_set_style_radius(c, UI_R_TILE, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    return c;
}

const char* weekday_de(const std::string& iso)
{
    int y = 0, m = 0, d = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return "";
    static const int kOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    const int w = (y + y / 4 - y / 100 + y / 400 + kOffsets[(m - 1 + 12) % 12] + d) % 7;
    return tr(static_cast<tr_key_t>(TR_DAY_SHORT_0 + w));
}
} // namespace

const char* WeatherDetail::compass(double bearing)
{
    const int i = static_cast<int>(std::lround(std::fmod(std::fmod(bearing, 360.0) + 360.0, 360.0) / 45.0)) % 8;
    return tr(static_cast<tr_key_t>(TR_COMPASS_0 + i));
}

std::string WeatherDetail::hour_of(const std::string& iso)
{
    const auto epoch = MediaDetail::parse_iso_epoch(iso);
    if (!epoch) return "";
    return app::core::hour_text(app::core::local_tm(*epoch).tm_hour); // "14" / "2 PM"
}

WeatherDetail::~WeatherDetail()
{
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_delete(root_);
}

void WeatherDetail::open(const State& state)
{
    if (root_) return;
    state_ = state;
    build();
    refresh();
}

void WeatherDetail::update(const State& state)
{
    if (!root_) return;
    state_ = state;
    refresh();
}

void WeatherDetail::on_root_deleted() { root_ = nullptr; }

void WeatherDetail::build()
{
    ui_detail_t d = ui_detail_open(state_.name.c_str(), tr(TR_WEATHER_FORECAST));
    root_ = d.root;
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
    lv_obj_t* c = d.content;

    lv_obj_t* now = ui_box(c);
    lv_obj_set_size(now, LV_PCT(100), 140);
    lv_obj_set_style_pad_hor(now, 8, 0);
    lv_obj_set_flex_flow(now, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(now, 24, 0);
    icon_ = ui_label(now, UI_ICON_L, UI_ACCENT, "");
    temp_ = ui_label(now, UI_FONT_HUGE, UI_TEXT, "");
    lv_obj_set_style_text_letter_space(temp_, -3, 0);
    lv_obj_t* right = ui_box(now);
    lv_obj_set_height(right, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(right, 6, 0);
    condition_ = ui_label(right, UI_FONT_TITLE, UI_TEXT, "");
    feels_ = ui_label(right, UI_FONT_LABEL, UI_TEXT2, "");
    range_ = ui_label(right, UI_FONT_LABEL, UI_TEXT2, "");

    lv_obj_t* facts = ui_box(c);
    lv_obj_set_size(facts, LV_PCT(100), 88);
    lv_obj_set_flex_flow(facts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(facts, 10, 0);
    const char* const titles[] = {tr(TR_WEATHER_HUMIDITY), tr(TR_WEATHER_WIND), tr(TR_WEATHER_RAIN_TODAY)};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* f = card(facts);
        lv_obj_set_height(f, LV_PCT(100));
        lv_obj_set_flex_grow(f, 1);
        lv_obj_set_style_radius(f, UI_R_CONTROL, 0);
        lv_obj_set_style_pad_hor(f, 18, 0);
        lv_obj_set_style_pad_ver(f, 14, 0);
        lv_obj_set_flex_flow(f, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        ui_label(f, UI_FONT_LABEL, UI_TEXT2, titles[i]);
        facts_[i] = ui_label(f, UI_FONT_TITLE, UI_TEXT, "");
    }

    hours_ = card(c);
    lv_obj_set_size(hours_, LV_PCT(100), 140);
    lv_obj_set_style_pad_hor(hours_, 8, 0);
    lv_obj_set_flex_flow(hours_, LV_FLEX_FLOW_ROW);

    days_ = card(c);
    lv_obj_set_width(days_, LV_PCT(100));
    lv_obj_set_flex_grow(days_, 1);
    lv_obj_set_style_pad_hor(days_, 8, 0);
    lv_obj_set_flex_flow(days_, LV_FLEX_FLOW_ROW);
}

void WeatherDetail::refresh()
{
    if (!root_) return;
    const State& s = state_;
    lv_label_set_text(icon_, weather_icon(s.condition));
    lv_label_set_text(temp_, s.temperature ? (format_number(*s.temperature, 1) + "°").c_str() : "–");
    lv_label_set_text(condition_, weather_text_de(s.condition));
    lv_label_set_text(feels_, s.apparent ? trf(TR_WEATHER_FEELS, {deg(s.apparent)}).c_str() : "");
    const auto* today = s.daily.days.empty() ? nullptr : &s.daily.days[0];
    lv_label_set_text(range_, today ? trf(TR_WEATHER_RANGE, {deg(today->temperature), deg(today->templow)}).c_str() : "");

    lv_label_set_text(facts_[0], s.humidity ? (format_number(*s.humidity, 0) + " %").c_str() : "–");
    std::string wind = s.wind_speed ? format_number(*s.wind_speed, 0) + " " + s.wind_unit : "–";
    if (s.wind_bearing) wind += std::string(" ") + compass(*s.wind_bearing);
    lv_label_set_text(facts_[1], wind.c_str());
    lv_label_set_text(facts_[2], today && today->precipitation
                                     ? (ValueTile::format_value(std::to_string(*today->precipitation)) + " mm").c_str()
                                     : "–");

    lv_obj_clean(hours_);
    for (size_t i = 0; i < kHours && i < s.hourly.days.size(); ++i) {
        const auto& h = s.hourly.days[i];
        lv_obj_t* col = ui_box(hours_);
        lv_obj_set_height(col, LV_PCT(100));
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 10, 0);
        ui_label(col, UI_FONT_LABEL, UI_TEXT2, hour_of(h.datetime).c_str());
        ui_label(col, UI_ICON_M, UI_TEXT2, weather_icon(h.condition));
        ui_label(col, UI_FONT_NAME, UI_TEXT, deg(h.temperature).c_str());
    }
    if (s.hourly.days.empty()) {
        lv_obj_center(ui_label(hours_, UI_FONT_LABEL, UI_TEXT3, tr(TR_WEATHER_NO_HOURLY)));
    }

    lv_obj_clean(days_);
    for (size_t i = 1; i <= kDays && i < s.daily.days.size(); ++i) {
        const auto& d = s.daily.days[i];
        lv_obj_t* col = ui_box(days_);
        lv_obj_set_height(col, LV_PCT(100));
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 10, 0);
        ui_label(col, UI_FONT_LABEL, UI_TEXT2, weekday_de(d.datetime));
        ui_label(col, UI_ICON_VALUE, UI_TEXT2, weather_icon(d.condition));
        lv_obj_t* temps = ui_box(col);
        lv_obj_set_size(temps, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(temps, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(temps, 6, 0);
        ui_label(temps, UI_FONT_NAME, UI_TEXT, deg(d.temperature).c_str());
        ui_label(temps, UI_FONT_NAME, UI_TEXT3, deg(d.templow).c_str());
        const std::string rain = d.precipitation_probability
                                     ? format_number(*d.precipitation_probability, 0) + " %"
                                     : d.precipitation ? format_number(*d.precipitation, 1) + " mm" : "";
        ui_label(col, UI_FONT_LABEL, UI_COOL, rain.c_str());
    }
}

} // namespace app::ui
