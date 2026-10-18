#ifndef APP_CPP_UI_MUSIC_MUSICITEMPAGE_H
#define APP_CPP_UI_MUSIC_MUSICITEMPAGE_H

#include "MusicCovers.h"
#include "../../music_assistant/MaController.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <string>
#include <vector>

namespace app::ui {

// A playlist, an album or an artist, opened from the search or its covers.
// On top the cover, the name and what it is (owner or artist, year, number
// of tracks, length), play (asks how, like the search), shuffle play, similar
// music (when MA can) and the favourite; below the tracks (rows and loads in pages of kPage while scrolling), a
// tap asks how the track goes into the queue. An artist shows its popular
// tracks and its albums, which open their own page. A page over the search
// (lv_layer_top); closes with its back button or with the search.
class MusicItemPage {
public:
    using Open = std::function<void(const app::music_assistant::MaQueueItem&)>; // an album on an artist's page
    using Played = std::function<void()>; // the queue was replaced: back to the player

    MusicItemPage(app::music_assistant::MaController& ma, std::string image_base,
                  app::music_assistant::MaQueueItem item, Open open, Played played);
    ~MusicItemPage();

    void close(); // on the next tick (from the page's own events)
    bool shown() const { return root_ != nullptr; }
    void on_root_deleted();
    void on_scroll_end();
    void pick_track(size_t index);
    void open_album(size_t index);
    void play();
    void shuffle();
    void start_radio(); // similar music
    void toggle_favorite();

private:
    static constexpr int kPage = 25; // ~3 KB of JSON per track
    static constexpr int kTopTracks = 10;
    static constexpr int kAlbums = 20;
    static void delete_root(void* self);
    void render_info();
    void render_status();
    void load_more();
    void add_rows();
    void window_covers();
    void load_albums();
    void enqueued(app::music_assistant::MaController::Enqueue how);

    app::music_assistant::MaController& ma_;
    MusicCovers covers_;
    app::music_assistant::MaQueueItem item_;
    Open open_;
    Played played_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* fav_ = nullptr;
    lv_obj_t* radio_ = nullptr;
    lv_obj_t* by_ = nullptr;
    lv_obj_t* info_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* albums_ = nullptr; // an artist's: the section
    bool closing_ = false;
    bool favorite_ = false;
    std::vector<app::music_assistant::MaQueueItem> tracks_;
    size_t shown_ = 0; // rows made (a page more while scrolling)
    std::vector<app::music_assistant::MaQueueItem> album_items_;
    bool loading_ = false;
    bool complete_ = false; // all tracks are in tracks_
    bool failed_ = false;
    app::core::CancellationSource pending_; // cancelled when the page closes
};

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICITEMPAGE_H
