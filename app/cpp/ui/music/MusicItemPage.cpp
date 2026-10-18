#include "MusicItemPage.h"

#include "MusicItems.h"
#include "../../../i18n.h"
#include "../../../lucide_icons.h"
#include "../../../ui_kit.h"
#include "../../../ui_theme.h"

#include <algorithm>

namespace app::ui {

using app::music_assistant::MaQueueItem;
using Enqueue = app::music_assistant::MaController::Enqueue;

namespace {
constexpr int32_t kScreen = 720;
constexpr int32_t kCover = 240; // room for a name in two lines, two info lines and the buttons
constexpr int32_t kRowCover = 56;
constexpr int32_t kCard = 148; // as the search's covers

MusicItemPage* self_of(lv_event_t* e) { return static_cast<MusicItemPage*>(lv_event_get_user_data(e)); }

lv_obj_t* round_button(lv_obj_t* parent, int32_t size, uint32_t bg, uint32_t pressed, const lv_font_t* font,
                       const char* icon, uint32_t color)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, size / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(pressed), LV_STATE_PRESSED);
    lv_obj_center(ui_label(b, font, color, icon));
    return b;
}

struct Row {
    MusicItemPage* page;
    size_t index;
    bool album; // a card of an artist's albums, else a track
};

void row_cb(lv_event_t* e)
{
    auto* r = static_cast<Row*>(lv_event_get_user_data(e));
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        delete r;
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (r->album) r->page->open_album(r->index);
    else r->page->pick_track(r->index);
}
} // namespace

MusicItemPage::MusicItemPage(app::music_assistant::MaController& ma, std::string image_base, MaQueueItem item,
                             Open open, Played played)
    : ma_(ma), covers_(std::move(image_base)), item_(std::move(item)), open_(std::move(open)),
      played_(std::move(played)), favorite_(item_.favorite)
{
    root_ = ui_box(lv_layer_top());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(root_, kScreen, kScreen);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(root_, UI_EDGE, 0);
    lv_obj_set_style_pad_bottom(root_, 0, 0); // the list runs to the edge
    lv_obj_set_style_pad_row(root_, 12, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(root_, [](lv_event_t* e) { self_of(e)->on_root_deleted(); }, LV_EVENT_DELETE, this);

    // back, and the favourite
    lv_obj_t* head = ui_box(root_);
    lv_obj_set_size(head, LV_PCT(100), 64);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* back = ui_box(head);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(back, 64, 64);
    lv_obj_set_style_radius(back, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(back, UI_ICON_BUTTON, UI_TEXT, LI_CHEVRON_LEFT));
    lv_obj_add_event_cb(back, [](lv_event_t* e) { self_of(e)->close(); }, LV_EVENT_CLICKED, this);
    fav_ = round_button(head, 64, UI_BG, UI_ACTIVE, UI_ICON_M, LI_HEART, UI_TEXT3);
    lv_obj_add_event_cb(fav_, [](lv_event_t* e) { self_of(e)->toggle_favorite(); }, LV_EVENT_CLICKED, this);

    body_ = ui_box(root_);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body_, UI_GAP_LIST, 0);
    lv_obj_set_style_pad_bottom(body_, UI_EDGE, 0);
    lv_obj_add_event_cb(body_, [](lv_event_t* e) { self_of(e)->on_scroll_end(); }, LV_EVENT_SCROLL_END, this);

    // the cover; kind, name, info; play and shuffle
    lv_obj_t* hero = ui_box(body_);
    lv_obj_set_size(hero, LV_PCT(100), kCover);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hero, 20, 0);
    lv_obj_set_style_margin_bottom(hero, 16, 0);
    lv_obj_t* art = music_placeholder(hero, kCover, item_, 0);
    covers_.put(art, item_.image, kCover, music_cover_radius(item_.media_type, kCover));
    lv_obj_t* texts = ui_box(hero);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_PCT(100));
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 4, 0);
    ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, music_type_label(item_.media_type));
    lv_obj_t* title = ui_label(texts, UI_FONT_TITLE, UI_TEXT, item_.title.c_str());
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_height(title, 2 * lv_font_get_line_height(UI_FONT_TITLE), 0); // two lines
    by_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_width(by_, LV_PCT(100));
    info_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_width(info_, LV_PCT(100));
    lv_obj_t* gap = ui_box(texts);
    lv_obj_set_width(gap, 1);
    lv_obj_set_flex_grow(gap, 1);
    lv_obj_t* buttons = ui_box(texts);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 12, 0);
    lv_obj_add_event_cb(round_button(buttons, UI_TOUCH_MIN, UI_TEXT, UI_TEXT2, UI_ICON_BUTTON, LI_PLAY, UI_BG),
                        [](lv_event_t* e) { self_of(e)->play(); }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(round_button(buttons, UI_TOUCH_MIN, UI_SURFACE, UI_ACTIVE, UI_ICON_BUTTON, LI_SHUFFLE, UI_TEXT),
                        [](lv_event_t* e) { self_of(e)->shuffle(); }, LV_EVENT_CLICKED, this);
    // similar music (when MA can: render_info)
    radio_ = round_button(buttons, UI_TOUCH_MIN, UI_SURFACE, UI_ACTIVE, UI_ICON_BUTTON, LI_RADIO, UI_TEXT);
    lv_obj_add_event_cb(radio_, [](lv_event_t* e) { self_of(e)->start_radio(); }, LV_EVENT_CLICKED, this);

    if (item_.media_type == "artist") ui_label_line(body_, UI_FONT_NAME, UI_TEXT, tr(TR_MUSIC_TOP_TRACKS));
    list_ = ui_box(body_);
    lv_obj_set_size(list_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, UI_GAP_LIST, 0);
    status_ = ui_label(body_, UI_FONT_BODY, UI_TEXT2, "");
    lv_obj_set_width(status_, LV_PCT(100));
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(status_, 24, 0);

    // the whole item (owner, year, favourite), then its tracks
    const auto token = pending_.token();
    ma_.item(item_, [this, token](bool ok, MaQueueItem full) {
        if (token.is_cancelled() || !root_ || !ok) return;
        if (full.image.empty()) full.image = item_.image;
        item_ = std::move(full);
        favorite_ = item_.favorite;
        render_info();
    });
    render_info();
    load_more();
    if (item_.media_type == "artist") load_albums();
}

MusicItemPage::~MusicItemPage()
{
    if (closing_) lv_async_call_cancel(delete_root, this);
    if (!root_) return;
    lv_obj_t* root = root_;
    on_root_deleted();
    lv_obj_delete(root);
}

void MusicItemPage::delete_root(void* p)
{
    auto* self = static_cast<MusicItemPage*>(p);
    self->closing_ = false;
    if (self->root_) lv_obj_delete(self->root_); // on_root_deleted
}

void MusicItemPage::close()
{
    if (!root_ || closing_) return;
    closing_ = true;
    lv_async_call(delete_root, this);
}

void MusicItemPage::on_root_deleted()
{
    pending_.cancel();
    root_ = body_ = fav_ = radio_ = by_ = info_ = list_ = status_ = albums_ = nullptr;
}

// who: an album's artists, a playlist's owner; then "1975 · 12 Titel · 48 Min."
void MusicItemPage::render_info()
{
    if (!root_) return;
    std::string s;
    auto add = [&s](const std::string& part) {
        if (part.empty()) return;
        if (!s.empty()) s += " · ";
        s += part;
    };
    lv_label_set_text(by_, item_.media_type == "playlist" ? item_.owner.c_str()
                           : item_.media_type == "album"  ? item_.artist.c_str()
                                                          : "");
    if (item_.media_type == "album" && item_.year > 0) add(std::to_string(item_.year));
    if (item_.media_type != "artist" && !tracks_.empty()) {
        if (complete_) {
            double secs = 0;
            for (const auto& t : tracks_) secs += t.duration;
            add(trf(TR_MUSIC_TRACK_COUNT, {std::to_string(tracks_.size())}));
            if (secs > 0) add(music_total_time(secs));
        } else {
            add(trf(TR_MUSIC_TRACK_COUNT_MORE, {std::to_string(tracks_.size())}));
        }
    }
    lv_label_set_text(info_, s.c_str());
    if (ma_.radio_supported(item_)) lv_obj_remove_flag(radio_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(radio_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(lv_obj_get_child(fav_, 0), lv_color_hex(favorite_ ? UI_ACCENT : UI_TEXT3), 0);
}

void MusicItemPage::render_status()
{
    if (!status_) return;
    const char* text = loading_                ? tr(TR_MUSIC_LOADING)
                       : failed_               ? tr(TR_MUSIC_LOAD_FAILED)
                       : tracks_.empty()       ? tr(TR_MUSIC_NO_TRACKS)
                                               : "";
    lv_label_set_text(status_, text);
    if (text[0]) lv_obj_remove_flag(status_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
}

void MusicItemPage::on_scroll_end()
{
    if (!body_) return;
    window_covers();
    if (loading_) return;
    // near the end: the next rows (loaded or still to load)
    if (lv_obj_get_scroll_bottom(body_) < 400) load_more();
}

void MusicItemPage::load_more()
{
    if (loading_) return;
    if (shown_ < tracks_.size()) { // already here (an older MA sends them all at once)
        add_rows();
        return;
    }
    if (complete_) return;
    loading_ = true;
    failed_ = false;
    render_status();
    const auto token = pending_.token();
    const bool artist = item_.media_type == "artist";
    const int limit = artist ? kTopTracks : kPage;
    ma_.tracks(item_, static_cast<int>(tracks_.size()), limit,
               [this, token, artist, limit](bool ok, std::vector<MaQueueItem> items, int) {
                   if (token.is_cancelled() || !root_) return;
                   loading_ = false;
                   failed_ = !ok;
                   // fewer: the end; more: MA (before 2.11) ignored the paging and sent all
                   if (ok && (artist || static_cast<int>(items.size()) != limit)) complete_ = true;
                   if (!tracks_.empty() && !items.empty() && items.front().uri == tracks_.front().uri) {
                       complete_ = true; // the same again: that MA ignored the offset (exactly kPage tracks)
                       items.clear();
                   }
                   if (artist && items.size() > static_cast<size_t>(kTopTracks)) items.resize(kTopTracks);
                   for (auto& it : items) tracks_.push_back(std::move(it));
                   add_rows();
                   render_info();
                   render_status();
               });
}

// the next kPage track rows: an album's with their number, the others with a small cover
void MusicItemPage::add_rows()
{
    const bool numbered = item_.media_type == "album";
    const size_t end = std::min(tracks_.size(), shown_ + kPage);
    for (size_t i = shown_; i < end; ++i) {
        const MaQueueItem& it = tracks_[i];
        lv_obj_t* row = ui_box(list_);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row, LV_PCT(100), 80);
        lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 16, 0);
        if (numbered) {
            lv_obj_t* n = ui_label(row, UI_FONT_NAME, UI_TEXT3, std::to_string(i + 1).c_str());
            lv_obj_set_width(n, 40);
            lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
        } else {
            lv_obj_t* art = music_placeholder(row, kRowCover, it, i);
            covers_.put(art, it.image, kRowCover, music_cover_radius(it.media_type, kRowCover));
        }
        lv_obj_t* texts = ui_box(row);
        lv_obj_set_flex_grow(texts, 1);
        lv_obj_set_height(texts, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(texts, 2, 0);
        ui_label_line(texts, UI_FONT_NAME, UI_TEXT, it.title.c_str());
        // an album: who plays (its tracks share the album); an artist: the album
        const std::string sub = numbered ? it.artist : item_.media_type == "artist" ? it.album : music_item_sub(it, false);
        if (!sub.empty()) ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, sub.c_str());
        if (it.duration > 0) ui_label(row, UI_FONT_LABEL, UI_TEXT3, music_track_time(it.duration).c_str());
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_ALL, new Row{this, i, false});
    }
    shown_ = end;
    window_covers();
}

// only the covers near what is shown (internal RAM on the panel); an
// album's rows have their number instead
void MusicItemPage::window_covers()
{
    if (item_.media_type == "album") return;
    music_window_covers(body_, list_, [this](size_t i, lv_obj_t* box) {
        if (i < tracks_.size()) covers_.put(box, tracks_[i].image, kRowCover, music_cover_radius(tracks_[i].media_type, kRowCover));
    });
}

// an artist's albums: a row of covers under the tracks
void MusicItemPage::load_albums()
{
    const auto token = pending_.token();
    ma_.artist_albums(item_, kAlbums, [this, token](bool ok, std::vector<MaQueueItem> items, int) {
        if (token.is_cancelled() || !root_ || !ok || items.empty()) return;
        if (items.size() > static_cast<size_t>(kAlbums)) items.resize(kAlbums); // MA before 2.11: all of them
        album_items_ = std::move(items);
        albums_ = ui_box(body_);
        lv_obj_set_size(albums_, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(albums_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(albums_, 12, 0);
        lv_obj_set_style_pad_top(albums_, 16, 0);
        ui_label_line(albums_, UI_FONT_NAME, UI_TEXT, tr(TR_MUSIC_TYPE_ALBUMS));
        lv_obj_t* row = ui_box(albums_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(row, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER); // up and down: the page
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, UI_GAP, 0);
        for (size_t i = 0; i < album_items_.size(); ++i) {
            const MaQueueItem& it = album_items_[i];
            lv_obj_t* card = ui_box(row);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN));
            lv_obj_set_size(card, kCard, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(card, 8, 0);
            lv_obj_set_style_opa(card, LV_OPA_80, LV_STATE_PRESSED);
            lv_obj_t* art = ui_box(card);
            lv_obj_add_flag(art, LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_set_size(art, kCard, kCard);
            lv_obj_set_style_radius(art, music_cover_radius(it.media_type, kCard), 0);
            lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(art, lv_color_hex(music_tone(i)), 0);
            covers_.put(art, it.image, kCard, music_cover_radius(it.media_type, kCard));
            lv_obj_t* t = ui_label_line(card, UI_FONT_LABEL, UI_TEXT, it.title.c_str());
            lv_obj_set_width(t, LV_PCT(100));
            if (it.year > 0) ui_label_line(card, UI_FONT_LABEL, UI_TEXT2, std::to_string(it.year).c_str());
            lv_obj_add_event_cb(card, row_cb, LV_EVENT_ALL, new Row{this, i, true});
        }
    });
}

void MusicItemPage::pick_track(size_t index)
{
    if (index >= tracks_.size() || !root_) return;
    const MaQueueItem track = tracks_[index];
    music_enqueue_sheet(root_, track, [this, track](Enqueue how) {
        ma_.play_media(track, how);
        enqueued(how);
    });
}

void MusicItemPage::open_album(size_t index)
{
    if (index < album_items_.size() && open_) open_(album_items_[index]);
}

void MusicItemPage::play()
{
    if (!root_) return;
    music_enqueue_sheet(root_, item_, [this](Enqueue how) {
        ma_.play_media(item_, how);
        enqueued(how);
    });
}

void MusicItemPage::shuffle()
{
    ma_.play_shuffled(item_);
    if (played_) played_();
}

void MusicItemPage::start_radio()
{
    ma_.start_radio(item_.uri); // replaces the queue
    if (played_) played_();
}

void MusicItemPage::enqueued(Enqueue how)
{
    if (how == Enqueue::Play || how == Enqueue::Replace) {
        if (played_) played_(); // back to the player
    } else {
        ui_toast(tr(how == Enqueue::Next ? TR_MUSIC_QUEUED_NEXT : TR_MUSIC_QUEUED_END));
    }
}

void MusicItemPage::toggle_favorite()
{
    favorite_ = !favorite_; // shown at once
    ma_.set_favorite(item_, favorite_);
    item_.favorite = favorite_;
    render_info();
}

} // namespace app::ui
