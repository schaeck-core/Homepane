#ifndef APP_CPP_UI_TILES_CLIMATETILE_H
#define APP_CPP_UI_TILES_CLIMATETILE_H

#include "../details/EntityDetail.h"
#include "TileWidget.h"

#include <memory>

struct _lv_timer_t;
struct _lv_draw_buf_t;

namespace app::ui {

// The gauge tile of the design guide for climate (also humidifier and
// water_heater): a 270° arc with the target (or the heat_cool range), the
// target as the big number, -/+ below. Presses are collected and sent as
// one set_temperature after a short pause; the tile shows the new target
// until HA confirms and falls back when the command fails.
class ClimateTile final : public TileWidget {
public:
    using TileWidget::TileWidget;
    static constexpr uint32_t kSendDelayMs = 700;

    struct Model {
        double min = 7, max = 30, step = 0.5;
        std::optional<double> current, target, low, high;
        std::string mode;   // hvac mode (state) / "on"
        std::string action; // hvac_action
        bool humidity = false; // humidifier: %, not °
    };
    struct View {
        std::string main, sub;
        double arc_from = 0, arc_to = 0, knob = 0; // fractions 0..1 of the arc
        bool arc_visible = true;
        uint32_t color = 0;
        bool adjusts_high = false; // -/+ move the upper bound (heat_cool)
    };
    // What the tile shows (pure; host-tested).
    static View view_for(const Model& m);

    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;
    void step(int direction); // -1 / +1
    void send_now();

    ~ClimateTile() override;

protected:
    void build(lv_obj_t* tile) override;
    void on_hold() override { on_tap(); }
    void update() override;
    void on_tap() override;
    void on_command_failed() override;

private:
    Model model() const;
    std::string value_text(double v) const;

    void open_detail();

    std::optional<double> pending_target_, pending_low_, pending_high_;
    std::unique_ptr<ClimateDetail> detail_;
    _lv_timer_t* send_timer_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* arc_ = nullptr;
    lv_obj_t* knob_ = nullptr;
    // The arc drawn once into a picture (bake_arc): a live arc costs three
    // masks per pixel on every redraw (a swipe redraws it each frame).
    lv_obj_t* arc_image_ = nullptr;
    _lv_draw_buf_t* arc_buf_ = nullptr;
    std::string arc_baked_; // what the picture shows
    void apply();
    void bake_arc();
    void free_arc_buf();
    lv_obj_t* main_label_ = nullptr;
    lv_obj_t* sub_label_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_CLIMATETILE_H
