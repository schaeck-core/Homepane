#ifndef APP_CPP_UI_LIGHTWIDGET_H
#define APP_CPP_UI_LIGHTWIDGET_H

#include "../core/CancellationToken.h"
#include "../home_assistant/HaCommandService.h"
#include "Widget.h"

#include <cstdint>
#include <memory>
#include <string>

namespace app::ui {

class LightDetail;

// The light tile of the design guide (centred tile): icon circle in the
// light's colour, name, brightness. Tap toggles (optimistic UI with
// rollback, §6.3), double tap sets 100 % neutral white, hold opens the
// light detail (LightDetail).
class LightWidget final : public Widget {
public:
    LightWidget(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService& commands,
                std::string entity_id, std::string name_override = {});
    ~LightWidget() override;

    std::vector<DataRequirement> requirements() const override;
    void create(lv_obj_t* parent) override;
    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;

    void handle_tap();
    void handle_long_press();
    void handle_double_tap();

private:
    void render();
    void send(const std::string& service, const std::string& json);

    app::home_assistant::HaInstanceId instance_;
    app::home_assistant::HaCommandService& commands_;
    std::string entity_id_;
    std::string name_override_;

    std::string display_name_;
    bool is_on_ = false;
    app::home_assistant::Availability availability_ = app::home_assistant::Availability::loading;

    // Optimistic UI + rollback (§6.3): set immediately on tap, cleared by
    // the next real EntityStore update (confirms or supersedes it) or
    // rolled back explicitly on a failed/timed-out command.
    bool pending_ = false;
    bool pending_target_on_ = false;
    app::core::CancellationSource pending_command_;

    // Only meaningful once a real update has arrived.
    uint8_t color_r_ = 255, color_g_ = 255, color_b_ = 255;
    int brightness_ = 255;
    int color_temp_kelvin_ = 3000;
    int min_color_temp_kelvin_ = 2000;
    int max_color_temp_kelvin_ = 6500;
    double hue_ = 30, saturation_ = 70;
    std::string color_mode_;
    bool supports_color_ = false, supports_temperature_ = false, supports_brightness_ = true;

    lv_obj_t* wrapper_ = nullptr;
    lv_obj_t* bubble_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
    std::unique_ptr<LightDetail> detail_;
};

} // namespace app::ui

#endif // APP_CPP_UI_LIGHTWIDGET_H
