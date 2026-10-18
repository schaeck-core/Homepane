#ifndef APP_CPP_UI_WEATHERWIDGET_H
#define APP_CPP_UI_WEATHERWIDGET_H

#include "Widget.h"
#include "details/WeatherDetail.h"

#include <memory>

#include <optional>
#include <string>

namespace app::ui {

// Combines two different data sources (CPP_MIGRATION_PLAN.md Phase 9):
// the weather entity's own state (condition/temperature/humidity, via
// the normal EntityRequirement path) and its forecast (via
// QueryCoordinator's ForecastRequirement path). Read-only, like
// SensorWidget — no service calls exist for the weather domain. Visual
// layout matches the legacy app/widgets/weather_forecast.c: big
// right-aligned current icon+temperature, high/low+humidity below it,
// and a 4-day forecast row (short day name, small condition icon,
// high/low) — using the same font_weather_70/30 icon font.
class WeatherWidget final : public Widget {
public:
    WeatherWidget(app::home_assistant::HaInstanceId instance, std::string entity_id, std::string name_override = {});

    std::vector<DataRequirement> requirements() const override;
    void create(lv_obj_t* parent) override;
    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;
    void open_detail();

private:
    void render();
    void render_forecast();
    void render_summary_line(); // the high/low + humidity line; depends on both on_data() branches

    app::home_assistant::HaInstanceId instance_;
    std::string entity_id_;
    std::string name_override_;

    std::string display_name_;
    std::string condition_; // the entity's own state, e.g. "sunny"
    std::string temperature_unit_;
    std::optional<double> temperature_;
    std::optional<double> humidity_;
    app::home_assistant::Availability availability_ = app::home_assistant::Availability::loading;

    app::home_assistant::ForecastState forecast_;
    // forecast_.days[0] is "today" (matches the legacy widget's own
    // special-cased first forecast entry) — its high/low feed the summary
    // line, not the 4-day forecast_row_, which starts at days[1].
    std::optional<double> today_high_;
    std::optional<double> today_low_;

    lv_obj_t* condition_icon_ = nullptr;
    lv_obj_t* temp_label_ = nullptr; // big current temperature, next to condition_icon_
    lv_obj_t* current_label_ = nullptr; // today's high/low + humidity, below the icon+temp row
    lv_obj_t* forecast_row_ = nullptr;

    // for the detail page
    app::home_assistant::ForecastState hourly_;
    std::optional<double> apparent_, wind_speed_, wind_bearing_;
    std::string wind_unit_;
    std::unique_ptr<WeatherDetail> detail_;
    WeatherDetail::State detail_state() const;
};

} // namespace app::ui

#endif // APP_CPP_UI_WEATHERWIDGET_H
