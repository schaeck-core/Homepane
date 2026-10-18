#include "MusicBrowsePage.h"

#include "MusicItemPage.h"
#include "MusicItems.h"

#include "../../../i18n.h"
#include "../../../lucide_icons.h"
#include "../../../ui_kit.h"
#include "../../../ui_theme.h"

#include <algorithm>
#include <cstring>

namespace app::ui {

using app::music_assistant::MaQueueItem;

namespace {
constexpr int32_t kScreen = 720;
constexpr int32_t kCard = 148; // a row shows 4 and a half covers: it can be scrolled sideways
constexpr uint32_t kSearchDelayMs = 450;

// chips: label key and MA media type
struct TypeChip {
    tr_key_t label;
    const char* media_type;
};
const TypeChip kTypes[] = {{TR_MUSIC_TYPE_ALL, ""},          {TR_MUSIC_TYPE_TRACKS, "track"},
                           {TR_MUSIC_TYPE_ARTISTS, "artist"}, {TR_MUSIC_TYPE_ALBUMS, "album"},
                           {TR_MUSIC_TYPE_PLAYLISTS, "playlist"}, {TR_MUSIC_TYPE_RADIO, "radio"}};

// tabs without a query: discover, then the library as MA's menu orders it
struct LibraryTab {
    tr_key_t label;
    const char* kind; // music/<kind>/library_items; "": discover
};
const LibraryTab kTabs[] = {{TR_MUSIC_TAB_DISCOVER, ""},          {TR_MUSIC_TYPE_ARTISTS, "artists"},
                            {TR_MUSIC_TYPE_ALBUMS, "albums"},     {TR_MUSIC_TYPE_TRACKS, "tracks"},
                            {TR_MUSIC_TYPE_PLAYLISTS, "playlists"}, {TR_MUSIC_TYPE_AUDIOBOOKS, "audiobooks"},
                            {TR_MUSIC_TYPE_PODCASTS, "podcasts"}, {TR_MUSIC_TYPE_RADIO, "radios"}};
// the library grid: four covers side by side, as big as MA's 160 px pictures
// (bigger ones load the next size, 256 px: too much at once for the panel's
// Wi-Fi buffers while a page of covers comes in)
constexpr int32_t kGridCard = 160;
constexpr int32_t kGridGap = (kScreen - 2 * UI_EDGE - 4 * kGridCard) / 3;

MusicBrowsePage* self_of(lv_event_t* e) { return static_cast<MusicBrowsePage*>(lv_event_get_user_data(e)); }

// a row of chips, scrolled sideways
lv_obj_t* chip_row(lv_obj_t* parent)
{
    lv_obj_t* row = ui_box(parent);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    return row;
}

lv_obj_t* chip(lv_obj_t* row, const char* text)
{
    lv_obj_t* c = ui_box(row);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_radius(c, 28, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(c, UI_FONT_LABEL, UI_TEXT2, text));
    return c;
}

// the chosen chip light, the others dark
void mark_chips(lv_obj_t* row, lv_obj_t* on)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(row); ++i) {
        lv_obj_t* c = lv_obj_get_child(row, static_cast<int32_t>(i));
        lv_obj_set_style_bg_color(c, lv_color_hex(c == on ? UI_TEXT : UI_SURFACE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(c, 0), lv_color_hex(c == on ? UI_BG : UI_TEXT2), 0);
    }
}

// MA's own rows by their id in German (or the UI language); others (from
// the streaming services) keep the name MA gives them
std::string folder_title(const app::music_assistant::MaController::Folder& f)
{
    struct Known {
        const char* id;
        tr_key_t text;
    };
    static const Known known[] = {{"trending", TR_MUSIC_FOLDER_TRENDING},
                                  {"in_progress", TR_MUSIC_FOLDER_IN_PROGRESS},
                                  {"recently_played", TR_MUSIC_FOLDER_RECENTLY_PLAYED},
                                  {"recently_added_tracks", TR_MUSIC_FOLDER_ADDED_TRACKS},
                                  {"recently_added_albums", TR_MUSIC_FOLDER_ADDED_ALBUMS},
                                  {"random_artists", TR_MUSIC_FOLDER_RANDOM_ARTISTS},
                                  {"random_albums", TR_MUSIC_FOLDER_RANDOM_ALBUMS},
                                  {"recent_favorite_tracks", TR_MUSIC_FOLDER_FAVORITED_TRACKS},
                                  {"favorite_playlists", TR_MUSIC_FOLDER_FAVORITE_PLAYLISTS},
                                  {"favorite_radio", TR_MUSIC_FOLDER_FAVORITE_RADIO},
                                  {"recent_artists", TR_MUSIC_FOLDER_RECENT_ARTISTS},
                                  {"recent_tracks", TR_MUSIC_FOLDER_RECENT_TRACKS},
                                  {"forgotten_tracks", TR_MUSIC_FOLDER_FORGOTTEN_TRACKS},
                                  {"forgotten_albums", TR_MUSIC_FOLDER_FORGOTTEN_ALBUMS},
                                  {"forgotten_artists", TR_MUSIC_FOLDER_FORGOTTEN_ARTISTS},
                                  {"most_played_tracks", TR_MUSIC_FOLDER_MOST_PLAYED},
                                  {"never_played_tracks", TR_MUSIC_FOLDER_RARELY_PLAYED},
                                  {"mixed_for_you", TR_MUSIC_FOLDER_MIXED_FOR_YOU}};
    for (const Known& k : known) {
        const size_t n = std::strlen(k.id);
        if (f.item_id == k.id || (f.item_id.size() > n && f.item_id.compare(f.item_id.size() - n, n, k.id) == 0)) {
            return tr(k.text);
        }
    }
    return f.name;
}

struct Pick {
    MusicBrowsePage* page;
    int section;
    size_t index;
};

void pick_cb(lv_event_t* e)
{
    auto* p = static_cast<Pick*>(lv_event_get_user_data(e));
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        delete p;
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) p->page->pick(p->section, p->index);
}
} // namespace

MusicBrowsePage::MusicBrowsePage(app::music_assistant::MaController& ma, std::string image_base, lv_obj_t*)
    : ma_(ma), image_base_(image_base), covers_(std::move(image_base))
{
    root_ = ui_box(lv_layer_top());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(root_, kScreen, kScreen);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(root_, UI_EDGE, 0);
    lv_obj_set_style_pad_row(root_, 12, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(root_, [](lv_event_t* e) { self_of(e)->on_root_deleted(); }, LV_EVENT_DELETE, this);

    // back, and the search field
    lv_obj_t* head = ui_box(root_);
    lv_obj_set_size(head, LV_PCT(100), 64);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(head, 12, 0);
    lv_obj_t* back = ui_box(head);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, 64, 64);
    lv_obj_set_style_radius(back, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(back, UI_ICON_BUTTON, UI_TEXT, LI_CHEVRON_LEFT));
    lv_obj_add_event_cb(back, [](lv_event_t* e) {
        auto* self = self_of(e);
        if (self->keyboard_ && !lv_obj_has_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN)) self->set_keyboard(false);
        else self->close();
    }, LV_EVENT_CLICKED, this);

    field_ = ui_box(head);
    lv_obj_add_flag(field_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(field_, 64);
    lv_obj_set_flex_grow(field_, 1);
    lv_obj_set_style_radius(field_, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(field_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(field_, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_border_color(field_, lv_color_hex(UI_TEXT3), 0);
    lv_obj_set_style_pad_left(field_, 20, 0);
    lv_obj_set_style_pad_right(field_, 8, 0);
    lv_obj_set_flex_flow(field_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(field_, 12, 0);
    ui_label(field_, UI_ICON_BUTTON, UI_TEXT2, LI_SEARCH);
    query_label_ = ui_label_line(field_, UI_FONT_BODY, UI_TEXT3, "");
    lv_obj_set_flex_grow(query_label_, 1);
    clear_ = ui_box(field_);
    lv_obj_add_flag(clear_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(clear_, 48, 48);
    lv_obj_set_style_radius(clear_, 24, 0);
    lv_obj_set_style_bg_opa(clear_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(clear_, lv_color_hex(UI_ACTIVE), 0);
    lv_obj_center(ui_label(clear_, UI_ICON_S, UI_TEXT, LI_X));
    lv_obj_add_event_cb(clear_, [](lv_event_t* e) {
        auto* self = self_of(e);
        self->query_.clear();
        self->type_ = 0;
        self->set_type(0);
        self->render_query();
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(field_, [](lv_event_t* e) { self_of(e)->set_keyboard(true); }, LV_EVENT_CLICKED, this);

    // tabs: discover and the library kinds a provider keeps (user data: the index in kTabs)
    tabs_ = chip_row(root_);
    for (size_t i = 0; i < sizeof(kTabs) / sizeof(kTabs[0]); ++i) {
        if (kTabs[i].kind[0] && !ma_.has_library(kTabs[i].kind)) continue;
        lv_obj_t* c = chip(tabs_, tr(kTabs[i].label));
        lv_obj_set_user_data(c, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(c, [](lv_event_t* e) {
            lv_obj_t* t = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            self_of(e)->set_tab(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(t))));
        }, LV_EVENT_CLICKED, this);
    }

    // browse: sections of covers
    browse_ = ui_box(root_);
    lv_obj_set_width(browse_, LV_PCT(100));
    lv_obj_set_flex_grow(browse_, 1);
    lv_obj_add_flag(browse_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(browse_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(browse_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(browse_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(browse_, 24, 0);
    lv_obj_set_style_pad_top(browse_, 4, 0);

    // the library tab: a grid of covers or a list of tracks
    library_ = ui_box(root_);
    lv_obj_add_flag(library_, LV_OBJ_FLAG_CLICKABLE); // a swipe between the covers scrolls too
    lv_obj_set_width(library_, LV_PCT(100));
    lv_obj_set_flex_grow(library_, 1);
    lv_obj_add_flag(library_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(library_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(library_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(library_, [](lv_event_t* e) { self_of(e)->on_library_scroll_end(); }, LV_EVENT_SCROLL_END, this);

    // search: type chips and results
    chips_ = chip_row(root_);
    for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); ++i) {
        lv_obj_add_event_cb(chip(chips_, tr(kTypes[i].label)), [](lv_event_t* e) {
            lv_obj_t* c = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            self_of(e)->set_type(static_cast<int>(lv_obj_get_index(c)));
        }, LV_EVENT_CLICKED, this);
    }
    results_ = ui_box(root_);
    lv_obj_set_width(results_, LV_PCT(100));
    lv_obj_set_flex_grow(results_, 1);
    lv_obj_add_flag(results_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(results_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(results_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(results_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(results_, UI_GAP_LIST, 0);

    // scrolling or tapping the lists puts the keyboard away
    for (lv_obj_t* list : {browse_, library_, results_}) {
        for (lv_event_code_t c : {LV_EVENT_SCROLL_BEGIN, LV_EVENT_PRESSED}) {
            lv_obj_add_event_cb(list, [](lv_event_t* e) { self_of(e)->set_keyboard(false); }, c, this);
        }
    }

    build_keyboard();
    set_type(0);
    set_tab(0);
    build_browse();
    render_query();
    set_keyboard(false); // shown when the field is tapped
}

MusicBrowsePage::~MusicBrowsePage()
{
    if (!root_) return;
    lv_obj_t* root = root_;
    on_root_deleted();
    lv_obj_delete(root);
}

void MusicBrowsePage::on_root_deleted()
{
    if (debounce_) lv_timer_delete(debounce_);
    debounce_ = nullptr;
    pending_.cancel();
    ++search_seq_;
    ++library_seq_;
    root_ = field_ = query_label_ = clear_ = tabs_ = browse_ = library_ = chips_ = results_ = keyboard_ = top_section_ = nullptr;
    for (auto& p : pages_) p->close();
    folder_sections_.clear();
}

// ── browse ──

lv_obj_t* MusicBrowsePage::add_section(const char* title, const char* hint)
{
    lv_obj_t* sec = ui_box(browse_);
    lv_obj_set_size(sec, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 12, 0);
    lv_obj_add_flag(sec, LV_OBJ_FLAG_HIDDEN); // until it has items
    lv_obj_t* h = ui_box(sec);
    lv_obj_set_size(h, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(h, 12, 0);
    ui_label_line(h, UI_FONT_NAME, UI_TEXT, title);
    if (hint && hint[0]) ui_label_line(h, UI_FONT_LABEL, UI_TEXT3, hint);
    // one row of covers, scrolled sideways
    lv_obj_t* row = ui_box(sec);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER); // up and down: the page
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_GAP, 0);
    lv_obj_add_event_cb(row, [](lv_event_t* e) { self_of(e)->set_keyboard(false); }, LV_EVENT_SCROLL_BEGIN, this);
    sections_.emplace_back();
    lv_obj_set_user_data(sec, reinterpret_cast<void*>(static_cast<intptr_t>(sections_.size() - 1)));
    return sec;
}

void MusicBrowsePage::fill_section(lv_obj_t* sec, std::vector<MaQueueItem> items)
{
    if (!root_ || items.empty()) return;
    const int s = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(sec)));
    if (items.size() > kRowItems) items.resize(kRowItems); // few objects: internal RAM
    add_cards(sec, s, items, 0);
    sections_[static_cast<size_t>(s)] = std::move(items);
}

// covers in a row; `first`: the index of items[0] in the section
void MusicBrowsePage::add_cards(lv_obj_t* sec, int s, const std::vector<MaQueueItem>& items, size_t first)
{
    lv_obj_t* row = lv_obj_get_child(sec, 1);
    for (size_t k = 0; k < items.size(); ++k) add_card(row, s, items[k], first + k, kCard, true);
    lv_obj_remove_flag(sec, LV_OBJ_FLAG_HIDDEN);
}

// a cover with its name and what it is; a tap picks items[i] of section s
void MusicBrowsePage::add_card(lv_obj_t* parent, int s, const MaQueueItem& it, size_t i, int32_t size, bool with_type)
{
    lv_obj_t* card = ui_box(parent);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_set_size(card, size, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_style_opa(card, LV_OPA_80, LV_STATE_PRESSED);
    // the cover box (its colour until the picture is there)
    lv_obj_t* art = ui_box(card);
    lv_obj_add_flag(art, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(art, size, size);
    lv_obj_set_style_radius(art, music_cover_radius(it.media_type, size), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(music_tone(i + static_cast<size_t>(s < 0 ? 0 : s))), 0);
    covers_.put(art, it.image, size, music_cover_radius(it.media_type, size));
    lv_obj_t* t = ui_label_line(card, UI_FONT_LABEL, UI_TEXT, it.title.c_str());
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_t* sub = ui_label_line(card, UI_FONT_LABEL, UI_TEXT2, music_item_sub(it, with_type).c_str());
    lv_obj_set_width(sub, LV_PCT(100));
    lv_obj_add_event_cb(card, pick_cb, LV_EVENT_ALL, new Pick{this, s, i});
}

// MA's suggestion rows as MA's home page shows them: "Top picks for you"
// (the first item of each row), then the rows MA shows (not the ones it
// hides), in MA's order, each loaded after the one before
void MusicBrowsePage::build_browse()
{
    const auto token = (pending_ = app::core::CancellationSource()).token();
    ma_.recommendations([this, token](std::vector<app::music_assistant::MaController::Folder> folders) {
        if (token.is_cancelled() || !root_) return;
        if (ma_.top_picks_shown()) {
            top_section_ = add_section(tr(TR_MUSIC_TOP_PICKS), nullptr);
            top_index_ = static_cast<int>(sections_.size()) - 1;
        }
        for (auto& f : folders) {
            if (!f.shown) continue;
            folder_sections_.push_back(add_section(folder_title(f).c_str(), nullptr)); // MA's subtitles are English
            folders_.push_back(std::move(f));
        }
        load_folder(0);
    });
}

void MusicBrowsePage::load_folder(size_t i)
{
    if (i >= folders_.size() || !root_) return;
    const auto token = pending_.token();
    ma_.recommendation_items(folders_[i], [this, token, i](bool ok, std::vector<MaQueueItem> items, int) {
        if (token.is_cancelled() || !root_) return;
        if (ok && !items.empty()) {
            // its first item, if new, for the top picks
            bool seen = false;
            for (const auto& t : top_items_) seen |= t.uri == items.front().uri;
            if (top_section_ && !seen && top_items_.size() < kTopPicks) {
                top_items_.push_back(items.front());
                add_cards(top_section_, top_index_, {items.front()}, top_items_.size() - 1);
                sections_[static_cast<size_t>(top_index_)] = top_items_;
            }
            fill_section(folder_sections_[i], std::move(items));
        }
        load_folder(i + 1);
    });
}

// ── search ──

void MusicBrowsePage::render_query()
{
    if (!root_) return;
    lv_label_set_text(query_label_, query_.empty() ? tr(TR_MUSIC_SEARCH_PLACEHOLDER) : query_.c_str());
    lv_obj_set_style_text_color(query_label_, lv_color_hex(query_.empty() ? UI_TEXT3 : UI_TEXT), 0);
    const bool searching = !query_.empty();
    if (searching) lv_obj_remove_flag(clear_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(clear_, LV_OBJ_FLAG_HIDDEN);
    const bool discover = kTabs[tab_].kind[0] == 0;
    auto show = [](lv_obj_t* o, bool shown) {
        if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    show(chips_, searching);
    show(results_, searching);
    show(tabs_, !searching);
    show(browse_, !searching && discover);
    show(library_, !searching && !discover);
    // a moment after the last key
    if (debounce_) lv_timer_reset(debounce_);
    else if (searching) {
        debounce_ = lv_timer_create([](lv_timer_t* t) {
            auto* self = static_cast<MusicBrowsePage*>(lv_timer_get_user_data(t));
            self->debounce_ = nullptr;
            self->run_search();
        }, kSearchDelayMs, this);
        lv_timer_set_repeat_count(debounce_, 1);
    }
    if (!searching) {
        found_.clear();
        lv_obj_clean(results_);
    }
}

void MusicBrowsePage::run_search()
{
    if (!root_ || query_.empty()) return;
    const uint32_t seq = ++search_seq_;
    searching_ = true;
    render_results();
    const bool all = type_ == 0;
    ma_.search(query_, kTypes[type_].media_type, all ? 4 : 12,
               [this, seq](bool ok, std::vector<MaQueueItem> items, int) {
                   if (seq != search_seq_ || !root_) return; // an older search, or closed
                   searching_ = false;
                   found_ = ok ? std::move(items) : std::vector<MaQueueItem>{};
                   render_results();
               });
}

void MusicBrowsePage::render_results()
{
    if (!results_) return;
    lv_obj_clean(results_);
    lv_obj_scroll_to_y(results_, 0, LV_ANIM_OFF);
    if (searching_ && found_.empty()) {
        ui_label(results_, UI_FONT_BODY, UI_TEXT2, tr(TR_MUSIC_SEARCHING));
        return;
    }
    if (found_.empty()) {
        lv_obj_t* box = ui_box(results_);
        lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(box, 40, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(box, 8, 0);
        ui_label(box, UI_FONT_NAME, UI_TEXT, trf(TR_MUSIC_NOTHING_FOUND, {query_}).c_str());
        lv_obj_t* hint = ui_label(box, UI_FONT_LABEL, UI_TEXT2, tr(TR_MUSIC_NOTHING_FOUND_HINT));
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        return;
    }
    for (size_t i = 0; i < found_.size(); ++i) add_row(results_, -1, found_[i], i, true);
}

// a list row: cover, name, what it is; play (a track, radio) or open (the others)
void MusicBrowsePage::add_row(lv_obj_t* parent, int s, const MaQueueItem& it, size_t i, bool with_type)
{
    lv_obj_t* row = ui_box(parent);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_PCT(100), 88);
    lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_t* art = music_placeholder(row, 64, it, i);
    covers_.put(art, it.image, 64, music_cover_radius(it.media_type, 64));
    lv_obj_t* texts = ui_box(row);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 2, 0);
    ui_label_line(texts, UI_FONT_NAME, UI_TEXT, it.title.c_str());
    ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, music_item_sub(it, with_type).c_str());
    if (music_opens_page(it.media_type)) { // opens its page
        lv_obj_t* open = ui_label(row, UI_ICON_BUTTON, UI_TEXT3, LI_CHEVRON_RIGHT);
        lv_obj_set_width(open, 56);
        lv_obj_set_style_text_align(open, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_t* play = ui_box(row);
        lv_obj_set_size(play, 56, 56);
        lv_obj_set_style_radius(play, 28, 0);
        lv_obj_set_style_bg_opa(play, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(play, lv_color_hex(UI_SURFACE2), 0);
        lv_obj_center(ui_label(play, UI_ICON_S, UI_TEXT, LI_PLAY));
    }
    lv_obj_add_event_cb(row, pick_cb, LV_EVENT_ALL, new Pick{this, s, i});
}

void MusicBrowsePage::set_type(int index)
{
    type_ = index;
    mark_chips(chips_, lv_obj_get_child(chips_, index));
    if (!query_.empty()) run_search();
}

// ── library ──

void MusicBrowsePage::set_tab(int index)
{
    tab_ = index;
    for (uint32_t i = 0; i < lv_obj_get_child_count(tabs_); ++i) {
        lv_obj_t* c = lv_obj_get_child(tabs_, static_cast<int32_t>(i));
        if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(c)) == index) mark_chips(tabs_, c);
    }
    library_kind_ = kTabs[index].kind;
    ++library_seq_; // an older tab's pages are not wanted any more
    library_items_.clear();
    library_loading_ = library_complete_ = false;
    lv_obj_clean(library_);
    lv_obj_scroll_to_y(library_, 0, LV_ANIM_OFF);
    // tracks in a list, the others as a grid of covers
    const bool list = library_kind_ == "tracks";
    lv_obj_set_flex_flow(library_, list ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(library_, list ? UI_GAP_LIST : 20, 0);
    lv_obj_set_style_pad_column(library_, list ? UI_GAP : kGridGap, 0);
    render_query();
    if (!library_kind_.empty()) load_library();
}

void MusicBrowsePage::on_library_scroll_end()
{
    if (!library_) return;
    window_library_covers();
    if (library_loading_ || library_complete_) return;
    // near the end: the next page
    if (lv_obj_get_scroll_bottom(library_) < 400) load_library();
}

void MusicBrowsePage::load_library()
{
    if (library_loading_ || library_complete_ || !root_) return;
    library_loading_ = true;
    if (library_items_.empty()) ui_label(library_, UI_FONT_BODY, UI_TEXT2, tr(TR_MUSIC_LOADING));
    const uint32_t seq = library_seq_;
    ma_.library(library_kind_, kLibraryPage, [this, seq](bool ok, std::vector<MaQueueItem> items, int) {
        if (seq != library_seq_ || !root_) return; // another tab, or closed
        library_loading_ = false;
        const size_t from = library_items_.size();
        if (from == 0) lv_obj_clean(library_); // "Loading"
        if (!ok) {
            ui_label(library_, UI_FONT_BODY, UI_TEXT2, tr(TR_MUSIC_LOAD_FAILED));
            return;
        }
        if (static_cast<int>(items.size()) < kLibraryPage) library_complete_ = true;
        for (auto& it : items) library_items_.push_back(std::move(it));
        const bool list = library_kind_ == "tracks";
        for (size_t i = from; i < library_items_.size(); ++i) {
            if (list) add_row(library_, -2, library_items_[i], i, false);
            else add_card(library_, -2, library_items_[i], i, kGridCard, false);
        }
        if (library_items_.empty()) ui_label(library_, UI_FONT_BODY, UI_TEXT2, tr(TR_MUSIC_LIBRARY_EMPTY));
        window_library_covers();
    }, static_cast<int>(library_items_.size()));
}

// only the covers near what is shown (internal RAM on the panel)
void MusicBrowsePage::window_library_covers()
{
    const bool list = library_kind_ == "tracks";
    music_window_covers(library_, library_, [this, list](size_t i, lv_obj_t* box) {
        if (i >= library_items_.size()) return;
        const MaQueueItem& it = library_items_[i];
        const int32_t size = list ? 64 : kGridCard;
        covers_.put(box, it.image, size, music_cover_radius(it.media_type, size));
    });
}

void MusicBrowsePage::pick(int section, size_t index)
{
    const std::vector<MaQueueItem>& list = section == -1   ? found_
                                           : section == -2 ? library_items_
                                                           : sections_[static_cast<size_t>(section)];
    if (index >= list.size() || !root_) return;
    set_keyboard(false);
    picked_ = list[index];
    if (music_opens_page(picked_.media_type)) {
        open_page(picked_);
        return;
    }
    // how it goes into the queue: a sheet like the queue's
    music_enqueue_sheet(root_, picked_, [this](app::music_assistant::MaController::Enqueue how) { enqueue(how); });
}

// a playlist's, album's or artist's page over the search (an artist's
// albums open theirs over it)
void MusicBrowsePage::open_page(const MaQueueItem& item)
{
    if (!root_) return;
    // the closed ones go (not the one this comes from: it is still shown)
    pages_.erase(std::remove_if(pages_.begin(), pages_.end(), [](const auto& p) { return !p->shown(); }), pages_.end());
    pages_.push_back(std::make_unique<MusicItemPage>(
        ma_, image_base_, item, [this](const MaQueueItem& album) { open_page(album); }, [this] { close(); }));
}

void MusicBrowsePage::enqueue(app::music_assistant::MaController::Enqueue how)
{
    using E = app::music_assistant::MaController::Enqueue;
    ma_.play_media(picked_, how);
    if (how == E::Play || how == E::Replace) {
        close(); // back to the player
    } else {
        ui_toast(tr(how == E::Next ? TR_MUSIC_QUEUED_NEXT : TR_MUSIC_QUEUED_END));
    }
}

void MusicBrowsePage::close()
{
    if (!root_ || closing_) return;
    closing_ = true;
    lv_obj_delete_async(root_); // on_root_deleted: the pages over it too
}

// ── keyboard ──

void MusicBrowsePage::build_keyboard()
{
    keyboard_ = ui_box(root_);
    lv_obj_set_size(keyboard_, kScreen, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_left(keyboard_, -UI_EDGE + 0, 0);
    lv_obj_set_style_margin_bottom(keyboard_, -UI_EDGE, 0);
    lv_obj_set_style_radius(keyboard_, 24, 0);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(UI_KEY_BG), 0);
    lv_obj_set_style_pad_all(keyboard_, 8, 0);
    lv_obj_set_style_pad_top(keyboard_, 10, 0);
    lv_obj_set_style_pad_row(keyboard_, 6, 0);
    lv_obj_set_flex_flow(keyboard_, LV_FLEX_FLOW_COLUMN);
    static const char* const kRows[] = {"q w e r t z u i o p", "a s d f g h j k l ö", "y x c v b n m ä"};
    auto add_key = [this](lv_obj_t* row, const char* text, int grow, uint32_t bg, uint32_t fg, const lv_font_t* font) {
        lv_obj_t* k = ui_box(row);
        lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_height(k, 60);
        lv_obj_set_width(k, 1);
        lv_obj_set_flex_grow(k, grow);
        lv_obj_set_style_radius(k, 14, 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(k, lv_color_hex(bg), 0);
        lv_obj_set_style_bg_color(k, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_t* l = ui_label(k, font, fg, text);
        lv_obj_center(l);
        lv_obj_add_event_cb(k, [](lv_event_t* e) {
            lv_obj_t* key = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            const char* t = static_cast<const char*>(lv_obj_get_user_data(key));
            self_of(e)->key(t);
        }, LV_EVENT_CLICKED, this);
        return k;
    };
    auto new_row = [this]() {
        lv_obj_t* r = ui_box(keyboard_);
        lv_obj_set_size(r, LV_PCT(100), 60);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(r, 6, 0);
        return r;
    };
    // the letters (user data: the text a key types)
    std::vector<std::string>& letters = keys_; // the keys' user data points into it
    letters.clear();
    letters.reserve(40); // never reallocated: at most 32 keys
    for (const char* spec : kRows) {
        lv_obj_t* row = new_row();
        const char* p = spec;
        while (*p) {
            const char* end = std::strchr(p, ' ');
            letters.emplace_back(p, end ? static_cast<size_t>(end - p) : std::strlen(p));
            lv_obj_t* k = add_key(row, letters.back().c_str(), 1, UI_SURFACE2, UI_TEXT, UI_FONT_NAME);
            lv_obj_set_user_data(k, const_cast<char*>(letters.back().c_str()));
            if (!end) break;
            p = end + 1;
        }
        if (spec == kRows[2]) {
            lv_obj_t* del = add_key(row, LI_DELETE, 2, UI_ACTIVE, UI_TEXT, UI_ICON_BUTTON);
            lv_obj_set_user_data(del, const_cast<char*>("\b"));
        }
    }
    lv_obj_t* last = new_row();
    letters.emplace_back("ü");
    lv_obj_set_user_data(add_key(last, letters.back().c_str(), 1, UI_SURFACE2, UI_TEXT, UI_FONT_NAME),
                         const_cast<char*>(letters.back().c_str()));
    letters.emplace_back("ß");
    lv_obj_set_user_data(add_key(last, letters.back().c_str(), 1, UI_SURFACE2, UI_TEXT, UI_FONT_NAME),
                         const_cast<char*>(letters.back().c_str()));
    lv_obj_set_user_data(add_key(last, tr(TR_MUSIC_KEY_SPACE), 5, UI_SURFACE2, UI_TEXT2, UI_FONT_NAME),
                         const_cast<char*>(" "));
    lv_obj_set_user_data(add_key(last, tr(TR_MUSIC_KEY_DONE), 3, UI_TEXT, UI_BG, UI_FONT_NAME), const_cast<char*>("\n"));
}

void MusicBrowsePage::key(const char* text)
{
    if (!text) return;
    if (std::strcmp(text, "\n") == 0) {
        set_keyboard(false);
        if (debounce_) { // search now
            lv_timer_delete(debounce_);
            debounce_ = nullptr;
            run_search();
        }
        return;
    }
    if (std::strcmp(text, "\b") == 0) {
        if (query_.empty()) return;
        size_t n = query_.size() - 1; // a whole UTF-8 character
        while (n > 0 && (static_cast<unsigned char>(query_[n]) & 0xC0) == 0x80) --n;
        query_.erase(n);
    } else if (query_.size() < 60) {
        query_ += text;
    }
    render_query();
}

void MusicBrowsePage::set_keyboard(bool shown)
{
    if (!keyboard_) return;
    if (shown) lv_obj_remove_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(field_, shown ? 2 : 0, 0);
}

} // namespace app::ui
