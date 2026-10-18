#ifndef APP_CPP_UI_TILES_ENTITYTILE_H
#define APP_CPP_UI_TILES_ENTITYTILE_H

#include "TileWidget.h"
#include "../details/EntityDetail.h"

#include <memory>

namespace app::ui {

// The centred tile of the design guide: icon circle, name and state, at
// most one quick action (tap) and optional buttons. Covers the simple
// domains: switch, input_boolean, fan, lock, button, script, scene,
// binary_sensor, person, alarm_control_panel, siren, valve, vacuum, ...
class EntityTile final : public TileWidget {
public:
    using TileWidget::TileWidget;

    // What the tile shows for an entity (pure; host-tested).
    struct Look {
        const char* icon = nullptr;
        int look = 0; // ui_look_t
        std::string state_text;
        bool warn_text = false; // state in the warning colour
    };
    struct Facts { // the entity facts Look depends on
        std::string domain, state, device_class;
        std::string message; // calendar: the current / next event
        std::optional<double> seconds_in_state, percentage;
        int fan_steps = 0;
        bool available = true;
    };
    static Look look_for(const Facts& f);

    // LVGL callback targets (free functions in the .cpp forward here).
    void button_pressed(int index);
    void hold_completed();
    void hold_progress_start();
    void hold_progress_stop();

    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;

protected:
    void build(lv_obj_t* tile) override;
    void update() override;
    void on_tap() override;
    void on_hold() override;

private:
    enum class Buttons { None, MinusPlus, Stop, OpenClose, StartDock };
    Buttons buttons_kind() const;
    Facts facts() const;

    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
    lv_obj_t* buttons_[2] = {nullptr, nullptr};
    lv_obj_t* progress_ = nullptr; // lock: fills the tile while held
    std::unique_ptr<AlarmDetail> alarm_; // alarm_control_panel: the keypad
    AlarmDetail::State alarm_state() const;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_ENTITYTILE_H
