#ifndef APP_CPP_UI_DETAILS_ALARMDETAIL_H
#define APP_CPP_UI_DETAILS_ALARMDETAIL_H

#include <functional>
#include <string>
#include <vector>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace app::ui {

// The alarm panel of the design guide (Alarm artboard): status and the
// arm modes on the left, the code dots and a 3 x 4 keypad on the right.
// Disarmed: enter the code, then pick a mode; armed: code, then "Disarm".
// A code is only sent when HA asks for one (code_format, and for arming
// code_arm_required). UI thread.
class AlarmDetail {
public:
    struct State {
        std::string name;
        std::string state; // disarmed, armed_home, armed_away, arming, pending, triggered, ...
        int supported_features = 7; // AlarmControlPanelEntityFeature: 1 home, 2 away, 4 night, 32 vacation
        bool code_needed = false;   // code_format set
        bool code_arm_required = true;
    };
    using Send = std::function<void(const std::string& service, const std::string& json)>;

    struct Mode {
        std::string service; // alarm_arm_home, ..., alarm_disarm
        int label;           // tr_key_t
        const char* icon;
    };
    // The mode buttons for a state (pure; host-tested).
    static std::vector<Mode> modes_for(const State& s);
    static bool is_armed(const std::string& state);
    // Service data: {"code":"1234"} when a code is needed, else {}.
    static std::string service_json(const State& s, const std::string& service, const std::string& code);

    explicit AlarmDetail(Send send);
    ~AlarmDetail();
    AlarmDetail(const AlarmDetail&) = delete;
    AlarmDetail& operator=(const AlarmDetail&) = delete;

    void open(const State& state);
    void update(const State& state);
    bool is_open() const { return root_ != nullptr; }

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_key(int index); // 0..11: 1-9, C, 0, back
    void on_mode(int index);

private:
    void build();
    void refresh();
    void refresh_dots();

    Send send_;
    State state_;
    std::string code_;
    std::vector<Mode> modes_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* status_card_ = nullptr;
    lv_obj_t* status_icon_ = nullptr;
    lv_obj_t* status_title_ = nullptr;
    lv_obj_t* status_sub_ = nullptr;
    lv_obj_t* mode_column_ = nullptr;
    lv_obj_t* dots_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_ALARMDETAIL_H
