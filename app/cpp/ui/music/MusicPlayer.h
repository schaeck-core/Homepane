#ifndef APP_CPP_UI_MUSIC_MUSICPLAYER_H
#define APP_CPP_UI_MUSIC_MUSICPLAYER_H

#include "../../core/CancellationToken.h"
#include "../../media/ImageDecoder.h"
#include "../../music_assistant/MaController.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app::ui {

class MusicQueuePage;
class MusicBrowsePage;

// The music player of the design guide (MediaDetail artboard), for Music
// Assistant: a full page over the dashboard, pulled down from the header
// (begin_pull / pull / end_pull) or opened directly. Shows the room's
// player: cover, title, position, the controls, volume, the other rooms it
// can play in too; opens the queue and the music search. UI thread.
class MusicPlayer {
public:
    MusicPlayer(app::music_assistant::MaController& ma, std::string image_base);
    ~MusicPlayer();

    void prepare(); // built hidden, so the first pull starts at once
    void open();
    void close();
    void close_now(); // at once, also while it slides (the screensaver's reset)
    bool is_open() const;

    // the pull-down from the header: dy = how far the finger is down
    void begin_pull();
    void pull(int32_t dy);
    void end_pull(int32_t dy, bool fast);

    // LVGL callbacks (free functions in the .cpp forward here)
    void on_root_deleted();
    void on_tick();
    void render();

private:
    void build();
    void slide_to(int32_t y, bool show_after);
    bool make_picture();
    void drop_picture();
    void render_cover(const app::music_assistant::MaQueueItem* item);
    void on_volume_drag(lv_event_t* e);

    app::music_assistant::MaController& ma_;
    std::string image_base_;
    int listener_ = 0;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* cover_ = nullptr;
    lv_obj_t* cover_icon_ = nullptr;
    lv_obj_t* track_ = nullptr;
    lv_obj_t* artist_ = nullptr;
    lv_obj_t* fav_ = nullptr;
    lv_obj_t* progress_ = nullptr;
    lv_obj_t* progress_fill_ = nullptr;
    lv_obj_t* elapsed_ = nullptr;
    lv_obj_t* duration_ = nullptr;
    lv_obj_t* shuffle_ = nullptr;
    lv_obj_t* play_icon_ = nullptr;
    lv_obj_t* repeat_ = nullptr;
    lv_obj_t* volume_ = nullptr;
    lv_obj_t* volume_fill_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* rooms_label_ = nullptr; // the tabs
    lv_obj_t* rooms_ = nullptr;
    lv_obj_t* empty_ = nullptr; // not connected / nothing to play
    lv_timer_t* tick_ = nullptr;
    lv_obj_t* picture_ = nullptr;          // the page as a picture, while it slides
    lv_draw_buf_t* picture_buf_ = nullptr;

    std::string rooms_key_;  // the rooms shown (rebuilt when they change)
    int rooms_tab_ = 0;      // 0: play also in, 1: switch player
    std::string cover_url_;
    std::shared_ptr<app::media::DecodedImage> cover_picture_;
    lv_image_dsc_t cover_dsc_{};
    app::core::CancellationSource cover_load_;
    int dragged_volume_ = -1;   // while the finger is on the volume bar
    uint32_t volume_sent_at_ = 0;
    bool fav_pending_ = false;

    std::unique_ptr<MusicQueuePage> queue_page_;
    std::unique_ptr<MusicBrowsePage> browse_page_;
};

// "1:32" / "1:02:05"
std::string music_time(double seconds);

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICPLAYER_H
