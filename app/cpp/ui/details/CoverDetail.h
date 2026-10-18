#ifndef APP_CPP_UI_DETAILS_COVERDETAIL_H
#define APP_CPP_UI_DETAILS_COVERDETAIL_H

#include <functional>
#include <optional>
#include <string>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace app::ui {

// The cover detail of the design guide (CoverDetail artboard): a large
// window to drag the shade to a position, the position in big numbers,
// open / stop / close, four presets and the slat tilt. UI thread.
class CoverDetail {
public:
    struct State {
        std::string name;
        std::string state; // open, closed, opening, closing
        std::optional<double> position, tilt;
        bool garage = false;
        bool can_position = true;
    };
    using Send = std::function<void(const std::string& service, const std::string& json)>;

    explicit CoverDetail(Send send);
    ~CoverDetail();
    CoverDetail(const CoverDetail&) = delete;
    CoverDetail& operator=(const CoverDetail&) = delete;

    void open(const State& state);
    void update(const State& state);
    bool is_open() const { return root_ != nullptr; }

    // "Öffnet …", "Geschlossen", "Ganz offen", "Offen" (pure; host-tested).
    static std::string status_text(const State& s);

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_window(bool released);
    void on_button(int index); // 0 open, 1 stop, 2 close
    void on_preset(int index);
    void on_tilt(bool released);

private:
    void build();
    void refresh();
    void draw_shade(int position);

    Send send_;
    State state_;
    bool dragging_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* pane_ = nullptr;
    lv_obj_t* shade_ = nullptr;
    lv_obj_t* value_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* buttons_ = nullptr;
    lv_obj_t* presets_ = nullptr;
    lv_obj_t* tilt_ = nullptr;
    int32_t slats_height_ = -1;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_COVERDETAIL_H
