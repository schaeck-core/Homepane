#ifndef APP_CPP_UI_MUSIC_MUSICTRACKPOPOVER_H
#define APP_CPP_UI_MUSIC_MUSICTRACKPOPOVER_H

#include "../../music_assistant/MaController.h"
#include "MusicCovers.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <string>

namespace app::ui {

// A small card at the top of the screen when the room's player starts
// another track (cover, title, artist), gone after a few seconds. Not while
// `hidden()` says so (the music player or the screensaver show the track
// anyway). A tap opens the music player (`open_player`). UI thread.
class MusicTrackPopover {
public:
    MusicTrackPopover(app::music_assistant::MaController& ma, std::string image_base, std::function<bool()> hidden,
                      std::function<void()> open_player);
    ~MusicTrackPopover();
    MusicTrackPopover(const MusicTrackPopover&) = delete;
    MusicTrackPopover& operator=(const MusicTrackPopover&) = delete;

    void hide(); // at once

    // What makes a track another one: the queue and the item's texts (a
    // radio station changes only its texts). "" while nothing is in the queue.
    static std::string track_key(const app::music_assistant::MaQueue* queue);

    // LVGL callbacks (free functions in the .cpp forward here)
    void on_root_deleted();
    void on_tick();
    void on_clicked();

private:
    void on_change();
    void show(const app::music_assistant::MaQueueItem& item);
    void slide(int32_t to, bool delete_after);

    app::music_assistant::MaController& ma_;
    MusicCovers covers_;
    std::function<bool()> hidden_;
    std::function<void()> open_player_;
    int listener_ = 0;
    bool known_ = false;     // the first state after connecting is not a change
    std::string key_;        // the track seen last
    std::string queue_;      // ... in this queue (another room is no change)

    lv_obj_t* root_ = nullptr;
    lv_obj_t* cover_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* artist_ = nullptr;
    lv_timer_t* tick_ = nullptr;
    uint32_t shown_at_ = 0;
    bool leaving_ = false;
};

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICTRACKPOPOVER_H
