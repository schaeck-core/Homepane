#ifndef APP_CPP_UI_MUSIC_MUSICBROWSEPAGE_H
#define APP_CPP_UI_MUSIC_MUSICBROWSEPAGE_H

#include "MusicCovers.h"
#include "MusicItemPage.h"
#include "../../music_assistant/MaController.h"

#include "lvgl/lvgl.h"

#include <memory>
#include <string>
#include <vector>

namespace app::ui {

// Find music (design guide: MusicBrowse, MusicSearch). Without a query: tabs
// as MA's menu, "Discover" (MA's suggestion rows of covers) and the library
// (artists, albums, tracks, playlists, ... as far as a provider keeps them;
// a grid of covers or a list of tracks, loaded in pages while scrolling).
// With one: type chips and the results. A tap on a track or a radio station
// asks how it goes into the queue (then back to the player); a playlist,
// album or artist opens its page (MusicItemPage). The query is typed on an
// on-screen keyboard (QWERTZ) shown while the field has the focus; the
// search runs a moment after the last key. A page over the player.
class MusicBrowsePage {
public:
    MusicBrowsePage(app::music_assistant::MaController& ma, std::string image_base, lv_obj_t* player_root);
    ~MusicBrowsePage();

    void on_root_deleted();
    void key(const char* text); // a key of the on-screen keyboard ("\b": delete)
    void set_keyboard(bool shown);
    void set_type(int index);
    void set_tab(int index); // index into the tabs (0: discover)
    void on_library_scroll_end();
    // section -1: the search results, -2: the library tab (opens the options or the page)
    void pick(int section, size_t index);
    void enqueue(app::music_assistant::MaController::Enqueue how);
    void close(); // back to the player
    void run_search();

private:
    void build_browse();
    void build_keyboard();
    void render_query();
    void render_results();
    lv_obj_t* add_section(const char* title, const char* hint);
    void fill_section(lv_obj_t* section, std::vector<app::music_assistant::MaQueueItem> items);
    void load_folder(size_t i);
    void open_page(const app::music_assistant::MaQueueItem& item);
    void add_cards(lv_obj_t* section, int s, const std::vector<app::music_assistant::MaQueueItem>& items, size_t first);
    void add_card(lv_obj_t* parent, int s, const app::music_assistant::MaQueueItem& it, size_t i, int32_t size, bool with_type);
    void add_row(lv_obj_t* parent, int s, const app::music_assistant::MaQueueItem& it, size_t i, bool with_type);
    void load_library();
    void window_library_covers();
    static constexpr int kLibraryPage = 24; // six rows of the grid (few covers loading at once)
    static constexpr size_t kRowItems = 10;
    static constexpr size_t kTopPicks = 9; // as MA's frontend

    app::music_assistant::MaController& ma_;
    std::string image_base_;
    MusicCovers covers_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* field_ = nullptr;
    lv_obj_t* query_label_ = nullptr;
    lv_obj_t* clear_ = nullptr;
    lv_obj_t* tabs_ = nullptr;
    lv_obj_t* browse_ = nullptr;
    lv_obj_t* library_ = nullptr;
    lv_obj_t* chips_ = nullptr;
    lv_obj_t* results_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;
    lv_timer_t* debounce_ = nullptr;
    std::string query_;
    int type_ = 0;
    bool searching_ = false;
    std::vector<app::music_assistant::MaQueueItem> found_;
    std::vector<std::vector<app::music_assistant::MaQueueItem>> sections_; // items per browse section
    app::core::CancellationSource pending_;
    uint32_t search_seq_ = 0;
    std::vector<std::string> keys_; // the keyboard's letters
    std::vector<app::music_assistant::MaController::Folder> folders_; // MA's suggestion rows
    std::vector<lv_obj_t*> folder_sections_;
    lv_obj_t* top_section_ = nullptr; // "Top picks for you"
    int top_index_ = -1;
    std::vector<app::music_assistant::MaQueueItem> top_items_;
    app::music_assistant::MaQueueItem picked_; // the item the options sheet is for
    std::vector<std::unique_ptr<MusicItemPage>> pages_; // opened from here, the last on top
    int tab_ = 0;
    std::string library_kind_; // the open library tab's ("albums", ...)
    std::vector<app::music_assistant::MaQueueItem> library_items_;
    bool library_loading_ = false;
    bool library_complete_ = false;
    uint32_t library_seq_ = 0;
    bool closing_ = false;
};

} // namespace app::ui

#endif // APP_CPP_UI_MUSIC_MUSICBROWSEPAGE_H
