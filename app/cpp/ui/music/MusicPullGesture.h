#ifndef APP_CPP_UI_MUSIC_MUSICPULLGESTURE_H
#define APP_CPP_UI_MUSIC_MUSICPULLGESTURE_H

#include "lvgl/lvgl.h"

#include <functional>

namespace app::ui {

class MusicPlayer;

// Pulling the music player down from the dashboard's header, like a phone's
// notification shade: a press that starts on the zone (the header) and
// moves down more than across opens the player as it follows the finger;
// released far or fast enough it stays open, else it slides back.
// LVGL (9.2) sends no press events to input devices, so a short timer
// watches the pointer devices' state; the header's buttons keep their taps.
// UI thread.
class MusicPullGesture {
public:
    using PlayerFn = std::function<MusicPlayer*()>; // nullptr: no music (not set up)

    void set_player(PlayerFn fn) { player_ = std::move(fn); }
    void set_zone(lv_obj_t* zone);
    // the dashboard's shown page is scrolled to its top
    void set_at_top(std::function<bool()> fn) { at_top_ = std::move(fn); }
    void on_tick();

private:
    bool in_zone(lv_point_t p) const;

    PlayerFn player_;
    std::function<bool()> at_top_;
    lv_obj_t* zone_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_indev_t* indev_ = nullptr; // the device of the press being watched
    bool pressed_ = false;
    bool candidate_ = false;
    bool pulling_ = false;
    lv_point_t start_{};
    int32_t last_dy_ = 0;
    uint32_t last_at_ = 0;
    int32_t speed_dy_ = 0; // px in the last ~100 ms (a flick opens it)
};

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICPULLGESTURE_H
