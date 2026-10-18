#ifndef APP_CPP_UI_TILES_VALUETILE_H
#define APP_CPP_UI_TILES_VALUETILE_H

#include "TileWidget.h"

struct _lv_timer_t; // as in ClockWidget.h: no LVGL header here
using lv_timer_t = _lv_timer_t;

namespace app::ui {

// The value tile of the design guide: name at the top, a big value with
// its unit and a status line at the bottom, optional buttons. For sensor,
// number / input_number / counter (-/+), update (install), weather, timer
// (countdown, start/pause + cancel), date / time / datetime helpers, event
// (last event), sun (next rise / set) and other read-only values.
class ValueTile final : public TileWidget {
public:
    using TileWidget::TileWidget;

    struct Text {
        std::string value, unit, status;
        uint32_t status_color = 0; // 0: text 2
        const char* icon = nullptr; // optional icon top right (weather)
        uint32_t icon_color = 0;
    };
    struct Facts {
        std::string domain, state, unit, device_class;
        std::string latest_version, installed_version; // update
        std::optional<double> seconds_in_state, temperature; // temperature: weather
        bool available = true, stale = false;
        // timer, sun, event, datetime helpers (raw attribute texts)
        std::string finishes_at, remaining, duration, next_rising, next_setting, event_type, has_date, has_time;
        double now = 0; // epoch seconds (relative times, countdowns)
    };
    // "2026-10-08T05:30:00+00:00" / "2026-10-08 07:30:00" (local) -> epoch seconds.
    static std::optional<double> parse_time(const std::string& text);
    // What the tile shows (pure; host-tested).
    static Text text_for(const Facts& f);
    // "21.456" -> "21,5"; non-numbers unchanged.
    static std::string format_value(const std::string& state);

    void button_pressed(int index);

    void shutdown_ui() override;

protected:
    void build(lv_obj_t* tile) override;
    void update() override;
    void on_tap() override;

private:
    bool has_buttons() const;
    Facts facts() const;

    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* icon_label_ = nullptr;
    lv_obj_t* value_label_ = nullptr;
    lv_obj_t* unit_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* buttons_[2] = {nullptr, nullptr};
    lv_timer_t* tick_ = nullptr; // a running timer counts down every second
};

// Lucide icon for a HA weather condition ("partlycloudy" -> cloud-sun, ...).
const char* weather_icon(const std::string& condition);
// German name of a HA weather condition ("partlycloudy" -> "Teils bewölkt").
const char* weather_text_de(const std::string& condition);

} // namespace app::ui

#endif // APP_CPP_UI_TILES_VALUETILE_H
