#ifndef APP_CPP_UI_DETAILS_WEATHERDETAIL_H
#define APP_CPP_UI_DETAILS_WEATHERDETAIL_H

#include "../../home_assistant/QueryTypes.h"

#include <optional>
#include <string>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace app::ui {

// The weather detail of the design guide (WeatherDetail artboard): now
// (icon, temperature, condition, feels-like, today's range), humidity /
// wind / rain today, the next six hours and the next five days. Read-only.
class WeatherDetail {
public:
    struct State {
        std::string name, condition;
        std::optional<double> temperature, apparent, humidity, wind_speed, wind_bearing;
        std::string wind_unit = "km/h";
        app::home_assistant::ForecastState daily, hourly;
    };

    WeatherDetail() = default;
    ~WeatherDetail();
    WeatherDetail(const WeatherDetail&) = delete;
    WeatherDetail& operator=(const WeatherDetail&) = delete;

    void open(const State& state);
    void update(const State& state);
    bool is_open() const { return root_ != nullptr; }

    // 225° -> "SW" (pure; host-tested).
    static const char* compass(double bearing);
    // "2026-10-07T13:00:00+00:00" -> "13" in local time (hourly row).
    static std::string hour_of(const std::string& iso);

    void on_root_deleted();

private:
    void build();
    void refresh();

    State state_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* icon_ = nullptr;
    lv_obj_t* temp_ = nullptr;
    lv_obj_t* condition_ = nullptr;
    lv_obj_t* feels_ = nullptr;
    lv_obj_t* range_ = nullptr;
    lv_obj_t* facts_[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* hours_ = nullptr;
    lv_obj_t* days_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_WEATHERDETAIL_H
