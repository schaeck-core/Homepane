#ifndef APP_CPP_UI_DETAILS_CLIMATEDETAIL_H
#define APP_CPP_UI_DETAILS_CLIMATEDETAIL_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
struct _lv_timer_t;

namespace app::ui {

// The thermostat detail of the design guide (ClimateDetail artboard): a
// large arc with the room temperature as a dot, the target (or the
// heating / cooling bounds of heat_cool, selectable) in the middle, big
// -/+ buttons, hvac modes, presets and the fan mode. -/+ presses are sent
// together after a short pause. UI thread.
class ClimateDetail {
public:
    struct State {
        std::string name;
        std::string mode;   // hvac mode (entity state)
        std::string action; // hvac_action
        double min = 7, max = 30, step = 0.5;
        std::optional<double> current, target, low, high, humidity;
        std::vector<std::string> modes, presets, fan_modes;
        std::string preset, fan_mode;
    };
    using Send = std::function<void(const std::string& service, const std::string& json)>;

    explicit ClimateDetail(Send send);
    ~ClimateDetail();
    ClimateDetail(const ClimateDetail&) = delete;
    ClimateDetail& operator=(const ClimateDetail&) = delete;

    void open(const State& state);
    void update(const State& state);
    bool is_open() const { return root_ != nullptr; }

    // "Heizt", "Kühlt", "Leerlauf", "Aus" (pure; host-tested).
    static std::string action_text(const State& s);
    // German names of HA hvac / preset / fan modes.
    static const char* mode_name(const std::string& mode);
    static const char* preset_name(const std::string& preset);

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_step(int direction);
    void on_select_bound(bool high);
    void on_mode(int index);
    void on_preset(int index);
    void send_pending();

private:
    void build();
    void refresh();

    Send send_;
    State state_;
    bool adjust_high_ = false;
    bool pending_ = false; // -/+ changed the target, not sent yet
    _lv_timer_t* send_timer_ = nullptr;
    std::vector<std::string> shown_modes_;
    std::vector<std::string> shown_presets_; // "fan" = the fan button

    lv_obj_t* root_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* humidity_ = nullptr;
    lv_obj_t* arc_ = nullptr;
    lv_obj_t* dot_ = nullptr;
    lv_obj_t* single_ = nullptr; // "Ziel" / target / "Raum"
    lv_obj_t* target_label_ = nullptr;
    lv_obj_t* room_label_ = nullptr;
    lv_obj_t* dual_ = nullptr; // low / high buttons
    lv_obj_t* bounds_[2] = {nullptr, nullptr};
    lv_obj_t* dual_room_ = nullptr;
    lv_obj_t* modes_row_ = nullptr;
    lv_obj_t* presets_row_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_CLIMATEDETAIL_H
