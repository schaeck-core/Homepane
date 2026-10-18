#include "WeatherWidget.h"

#include "AttributeText.h"
#include "lvgl/lvgl.h"
#include "../../i18n.h"

#include "../../fonts.h"
#include "../../ui_kit.h"
#include "tiles/TileWidget.h"
#include "tiles/ValueTile.h"

#include <cstdio>
#include <ctime>

namespace app::ui {

using app::home_assistant::Availability;
using app::home_assistant::EntityState;
using app::home_assistant::ForecastState;
using app::home_assistant::HaInstanceId;

namespace {
constexpr size_t kMaxForecastDaysShown = 4; // matches the legacy widget's fixed forecastSize
constexpr const char* kForecastType = "daily"; // matches the legacy widget's only mode, no card-level override

// German short weekday ("Mo".."So") of an ISO date.
std::string short_weekday(const std::string& iso_datetime) {
    struct tm tm_val {};
    if (!strptime(iso_datetime.c_str(), "%Y-%m-%dT%H:%M:%S", &tm_val)) return "";
    // weekday from the date (Sakamoto); no timegm() in newlib
    static const int kOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = tm_val.tm_year + 1900;
    const int m = tm_val.tm_mon + 1;
    if (m < 3) y -= 1;
    const int wday = (y + y / 4 - y / 100 + y / 400 + kOffsets[(m - 1) % 12] + tm_val.tm_mday) % 7;
    return tr(static_cast<tr_key_t>(TR_DAY_SHORT_0 + wday));
}

std::string whole_degrees(double v) { return format_number(v, 0) + "°"; }
} // namespace

WeatherWidget::WeatherWidget(HaInstanceId instance, std::string entity_id, std::string name_override)
    : instance_(std::move(instance)), entity_id_(std::move(entity_id)), name_override_(std::move(name_override)) {}

std::vector<DataRequirement> WeatherWidget::requirements() const {
    // [0] entity, [1] daily (the strip), [2] hourly (the detail page)
    return {EntityRequirement{instance_, entity_id_}, ForecastRequirement{instance_, entity_id_, kForecastType},
            ForecastRequirement{instance_, entity_id_, "hourly"}};
}

void WeatherWidget::create(lv_obj_t* parent) {
    // Weather block: condition icon and temperature, today's range below,
    // four days below that.
    lv_obj_t* wrapper = ui_box(parent);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(wrapper, 18, 0);
    lv_obj_set_style_pad_bottom(wrapper, 14, 0);
    lv_obj_set_style_pad_left(wrapper, 24, 0);
    lv_obj_set_style_pad_right(wrapper, 20, 0);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // like the earlier block: icon + temperature right-aligned, today's
    // range and humidity right-aligned below; the icon says the condition
    lv_obj_t* head = ui_box(wrapper);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(head, 2, 0);

    lv_obj_t* top_row = ui_box(head);
    lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_row, 14, 0);
    condition_icon_ = ui_label(top_row, UI_ICON_L, UI_ACCENT, "");
    temp_label_ = ui_label(top_row, font_manrope_600_56, UI_TEXT, "");

    current_label_ = ui_label(head, UI_FONT_BODY, UI_TEXT2, "");

    forecast_row_ = ui_box(wrapper);
    lv_obj_set_size(forecast_row_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(forecast_row_, LV_FLEX_FLOW_ROW);

    // a tap opens the weather detail
    lv_obj_remove_flag(wrapper, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(wrapper, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        wrapper, [](lv_event_t* e) { static_cast<WeatherWidget*>(lv_event_get_user_data(e))->open_detail(); },
        LV_EVENT_CLICKED, this);

    display_name_ = name_override_;
    render();
    render_summary_line();
    render_forecast();

    track_root(wrapper);
}

void WeatherWidget::on_data(const DataUpdate& update) {
    if (update.entity_state) {
        const EntityState& s = *update.entity_state;
        availability_ = s.availability;
        condition_ = s.state;
        temperature_ = find_attr_double(s.attributes, "temperature");
        humidity_ = find_attr_double(s.attributes, "humidity");
        temperature_unit_ = find_attr(s.attributes, "temperature_unit");
        apparent_ = find_attr_double(s.attributes, "apparent_temperature");
        wind_speed_ = find_attr_double(s.attributes, "wind_speed");
        wind_bearing_ = find_attr_double(s.attributes, "wind_bearing");
        wind_unit_ = find_attr(s.attributes, "wind_speed_unit");

        if (name_override_.empty()) {
            std::string friendly = find_attr(s.attributes, "friendly_name");
            display_name_ = friendly.empty() ? entity_id_ : friendly;
        } else {
            display_name_ = name_override_;
        }
        render();
        render_summary_line();
    } else if (update.forecast_state && update.requirement_index == 2) {
        hourly_ = *update.forecast_state;
    } else if (update.forecast_state) {
        forecast_ = *update.forecast_state;
        today_high_ = !forecast_.days.empty() ? forecast_.days[0].temperature : std::nullopt;
        today_low_ = !forecast_.days.empty() ? forecast_.days[0].templow : std::nullopt;
        render_summary_line();
        render_forecast();
    }
    if (detail_ && detail_->is_open()) detail_->update(detail_state());
}

WeatherDetail::State WeatherWidget::detail_state() const {
    WeatherDetail::State st;
    st.name = display_name_.empty() ? tr(TR_WEATHER_TITLE) : display_name_;
    st.condition = condition_;
    st.temperature = temperature_;
    st.apparent = apparent_;
    st.humidity = humidity_;
    st.wind_speed = wind_speed_;
    st.wind_bearing = wind_bearing_;
    if (!wind_unit_.empty()) st.wind_unit = wind_unit_;
    st.daily = forecast_;
    st.hourly = hourly_;
    return st;
}

void WeatherWidget::open_detail() {
    if (availability_ == Availability::loading) return;
    if (!detail_) detail_ = std::make_unique<WeatherDetail>();
    detail_->open(detail_state());
}

void WeatherWidget::shutdown_ui() {
    detail_.reset(); // the page lives on the screen, not under the widget
}

void WeatherWidget::render() {
    if (!temp_label_ || !condition_icon_) return;

    switch (availability_) {
        case Availability::loading:
        case Availability::unavailable:
        case Availability::unknown:
            lv_label_set_text(condition_icon_, "");
            lv_label_set_text(temp_label_, "");
            return;
        case Availability::available:
        case Availability::stale:
            break;
    }
    lv_label_set_text(condition_icon_, weather_icon(condition_));
    lv_label_set_text(temp_label_, temperature_ ? (format_number(*temperature_, 1) + "°").c_str() : "–");
}

void WeatherWidget::render_summary_line() {
    if (!current_label_) return;

    switch (availability_) {
        case Availability::loading:
            lv_label_set_text(current_label_, "…");
            return;
        case Availability::unavailable:
            lv_label_set_text(current_label_, tr(TR_STATE_UNAVAILABLE));
            return;
        case Availability::unknown:
            lv_label_set_text(current_label_, tr(TR_STATE_UNKNOWN));
            return;
        case Availability::available:
        case Availability::stale:
            break;
    }

    // "22° / 13° · 77 %"
    std::string text = (today_high_ ? whole_degrees(*today_high_) : "–") + " / " +
                       (today_low_ ? whole_degrees(*today_low_) : "–");
    if (humidity_) text += " · " + format_number(*humidity_, 0) + " %";
    if (availability_ == Availability::stale) text += std::string(" · ") + tr(TR_STATE_STALE);
    lv_label_set_text(current_label_, text.c_str());
}

void WeatherWidget::render_forecast() {
    if (!forecast_row_) return;
    lv_obj_clean(forecast_row_);

    // days[0] is "today" (consumed by render_summary_line() instead) — the
    // strip itself starts at days[1], matching the legacy widget's own
    // daily_forecast[] offset (weather_forecast.c's callback_subscribtion_forecast:
    // index 0 -> weatherData->high/low_temperature, index 1.. -> daily_forecast[]).
    size_t total_after_today = forecast_.days.size() > 0 ? forecast_.days.size() - 1 : 0;
    size_t shown = total_after_today < kMaxForecastDaysShown ? total_after_today : kMaxForecastDaysShown;
    for (size_t i = 0; i < shown; ++i) {
        const auto& day = forecast_.days[i + 1];

        lv_obj_t* day_wrapper = ui_box(forecast_row_);
        lv_obj_set_height(day_wrapper, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(day_wrapper, 1);
        lv_obj_set_flex_flow(day_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(day_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // a bit larger than the guide's strip (read from across the room),
        // with room to the current weather above
        lv_obj_set_style_pad_row(day_wrapper, 2, 0);
        lv_obj_t* weekday = ui_label(day_wrapper, UI_FONT_LABEL, UI_TEXT2, short_weekday(day.datetime).c_str());
        lv_obj_set_style_pad_bottom(weekday, 4, 0);
        lv_obj_t* icon = ui_label(day_wrapper, UI_ICON_M, UI_TEXT2, weather_icon(day.condition));
        lv_obj_set_style_pad_bottom(icon, 4, 0);
        ui_label(day_wrapper, font_manrope_600_26, UI_TEXT, day.temperature ? whole_degrees(*day.temperature).c_str() : "–");
        ui_label(day_wrapper, font_manrope_600_22, UI_TEXT3, day.templow ? whole_degrees(*day.templow).c_str() : "–");
    }
}

} // namespace app::ui
