#ifndef APP_CPP_UI_MUSIC_MUSICQUEUEPAGE_H
#define APP_CPP_UI_MUSIC_MUSICQUEUEPAGE_H

#include "MusicCovers.h"
#include "../../music_assistant/MaController.h"

#include "lvgl/lvgl.h"

#include <string>
#include <vector>

namespace app::ui {

// The queue (design guide: MusicQueue): what plays now, then the coming
// items (loaded in pages of kPage while scrolling). A tap plays an item;
// "⋯" offers play next / similar music / remove. A page over the player
// (lv_layer_top); closes with its back button or with the player.
class MusicQueuePage {
public:
    MusicQueuePage(app::music_assistant::MaController& ma, std::string image_base, lv_obj_t* player_root);
    ~MusicQueuePage();

    void on_root_deleted();
    void on_scroll_end();
    void on_changed();
    void open_sheet(size_t index);
    void play(size_t index);

private:
    static constexpr int kPage = 25; // ~5 KB of JSON per item
    void render_head();
    void load_more();
    void add_rows(size_t from);

    app::music_assistant::MaController& ma_;
    std::string image_base_;
    MusicCovers covers_;
    int listener_ = 0;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* now_cover_ = nullptr;
    lv_obj_t* now_label_ = nullptr;
    lv_obj_t* now_title_ = nullptr;
    lv_obj_t* bars_[3] = {};
    lv_obj_t* list_ = nullptr;
    lv_obj_t* sheet_ = nullptr;
    lv_timer_t* tick_ = nullptr;
    uint32_t ticks_ = 0;
    std::vector<app::music_assistant::MaQueueItem> items_; // after the current one
    int first_index_ = 0;  // queue index of items_[0]
    bool loading_ = false;
    bool complete_ = false;
    std::string shown_key_; // queue state the list was loaded for
    std::string now_cover_id_;
    app::core::CancellationSource load_;
};

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICQUEUEPAGE_H
