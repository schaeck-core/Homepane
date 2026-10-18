#ifndef APP_CPP_UI_DETAILS_MEDIADETAIL_H
#define APP_CPP_UI_DETAILS_MEDIADETAIL_H

#include <functional>
#include <optional>
#include <string>

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
struct _lv_timer_t;

namespace app::ui {

// The media player detail of the design guide (MediaDetail artboard):
// artwork, title and artist, progress, transport controls, volume. UI
// thread.
class MediaDetail {
public:
    struct State {
        std::string name, state; // playing, paused, idle, off, ...
        std::string title, artist, album;
        std::optional<double> position, duration, volume; // s, s, 0..1
        std::optional<double> position_updated_epoch_s;
        bool shuffle = false;
        std::string repeat; // off, all, one
        bool tv = false;
    };
    using Send = std::function<void(const std::string& service, const std::string& json)>;

    explicit MediaDetail(Send send);
    ~MediaDetail();
    MediaDetail(const MediaDetail&) = delete;
    MediaDetail& operator=(const MediaDetail&) = delete;

    void open(const State& state);
    void update(const State& state);
    bool is_open() const { return root_ != nullptr; }

    // 92 -> "1:32", 3725 -> "1:02:05" (pure; host-tested).
    static std::string clock_text(double seconds);
    // "2026-10-07T18:00:00.123+00:00" -> epoch seconds (UTC offsets honoured).
    static std::optional<double> parse_iso_epoch(const std::string& iso);

    // LVGL callbacks (free functions in the .cpp forward here).
    void on_root_deleted();
    void on_control(int index); // 0 shuffle, 1 prev, 2 play, 3 next, 4 repeat
    void on_volume(bool released);
    void tick();

private:
    void build();
    void refresh();
    void refresh_progress();

    Send send_;
    State state_;
    bool dragging_ = false;
    _lv_timer_t* timer_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* art_icon_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* artist_ = nullptr;
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* elapsed_ = nullptr;
    lv_obj_t* total_ = nullptr;
    lv_obj_t* controls_ = nullptr;
    lv_obj_t* volume_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_DETAILS_MEDIADETAIL_H
