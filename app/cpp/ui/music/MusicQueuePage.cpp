#include "MusicQueuePage.h"

#include "MusicPlayer.h"
#include "../../../i18n.h"
#include "../../../lucide_icons.h"
#include "../../../ui_kit.h"
#include "../../../ui_theme.h"

#include <algorithm>
#include <cmath>

namespace app::ui {

using app::music_assistant::MaQueueItem;

namespace {
constexpr int32_t kScreen = 720;
constexpr uint32_t kNowBg = 0x1F1E1C;
constexpr uint32_t kSheetShade = 0x000000;

MusicQueuePage* self_of(lv_event_t* e) { return static_cast<MusicQueuePage*>(lv_event_get_user_data(e)); }

lv_obj_t* back_button(lv_obj_t* parent)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, 64, 64);
    lv_obj_set_style_radius(b, UI_R_CONTROL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_center(ui_label(b, UI_ICON_BUTTON, UI_TEXT, LI_CHEVRON_LEFT));
    return b;
}

// a cover box with the music placeholder
lv_obj_t* cover_box(lv_obj_t* parent, int32_t size, int32_t radius, uint32_t bg)
{
    lv_obj_t* box = ui_box(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(bg), 0);
    lv_obj_center(ui_label(box, UI_ICON_M, 0x6F6B66, LI_MUSIC));
    return box;
}

struct RowData {
    MusicQueuePage* page;
    size_t index;
};
} // namespace

MusicQueuePage::MusicQueuePage(app::music_assistant::MaController& ma, std::string image_base, lv_obj_t*)
    : ma_(ma), image_base_(image_base), covers_(std::move(image_base))
{
    root_ = ui_box(lv_layer_top());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(root_, kScreen, kScreen);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(root_, UI_EDGE, 0);
    lv_obj_set_style_pad_bottom(root_, 0, 0);
    lv_obj_set_style_pad_row(root_, 16, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(root_, [](lv_event_t* e) { self_of(e)->on_root_deleted(); }, LV_EVENT_DELETE, this);

    lv_obj_t* head = ui_box(root_);
    lv_obj_set_size(head, LV_PCT(100), 72);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 16, 0);
    lv_obj_add_event_cb(back_button(head), [](lv_event_t* e) { lv_obj_delete_async(self_of(e)->root_ ? self_of(e)->root_ : nullptr); },
                        LV_EVENT_CLICKED, this);
    lv_obj_t* names = ui_box(head);
    lv_obj_set_flex_grow(names, 1);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(names, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(names, 2, 0);
    ui_label_line(names, UI_FONT_TITLE, UI_TEXT, tr(TR_MUSIC_QUEUE));
    subtitle_ = ui_label_line(names, UI_FONT_LABEL, UI_TEXT2, "");

    // what plays now
    lv_obj_t* now = ui_box(root_);
    lv_obj_set_size(now, LV_PCT(100), 104);
    lv_obj_set_style_radius(now, UI_R_TILE, 0);
    lv_obj_set_style_bg_opa(now, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(now, lv_color_hex(kNowBg), 0);
    lv_obj_set_style_pad_left(now, 16, 0);
    lv_obj_set_style_pad_right(now, 20, 0);
    lv_obj_set_flex_flow(now, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(now, 16, 0);
    now_cover_ = cover_box(now, 72, 14, UI_ACTIVE);
    lv_obj_t* nt = ui_box(now);
    lv_obj_set_flex_grow(nt, 1);
    lv_obj_set_height(nt, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(nt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nt, 2, 0);
    now_label_ = ui_label(nt, UI_FONT_LABEL, UI_ACCENT, "");
    now_title_ = ui_label_line(nt, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_t* bars = ui_box(now);
    lv_obj_set_size(bars, LV_SIZE_CONTENT, 28);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(bars, 4, 0);
    for (auto& b : bars_) {
        b = ui_box(bars);
        lv_obj_set_size(b, 5, 6);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACCENT), 0);
    }

    lv_obj_t* label = ui_box(root_);
    lv_obj_set_size(label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(label, 4, 0);
    lv_obj_set_flex_flow(label, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(label, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    ui_label(label, UI_FONT_NAME, UI_TEXT, tr(TR_MUSIC_UP_NEXT));
    ui_label(label, UI_FONT_LABEL, UI_TEXT3, tr(TR_MUSIC_QUEUE_HINT));

    list_ = ui_box(root_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, UI_GAP_LIST, 0);
    lv_obj_set_style_pad_bottom(list_, 16, 0);
    lv_obj_add_event_cb(list_, [](lv_event_t* e) { self_of(e)->on_scroll_end(); }, LV_EVENT_SCROLL_END, this);

    listener_ = ma_.add_listener([this] { on_changed(); });
    tick_ = lv_timer_create(
        [](lv_timer_t* t) {
            auto* self = static_cast<MusicQueuePage*>(lv_timer_get_user_data(t));
            const auto* q = self->ma_.queue();
            const bool playing = q && q->state == "playing";
            ++self->ticks_;
            for (int i = 0; i < 3; ++i) { // the level bars move while playing
                const int h = playing ? 8 + static_cast<int>(20 * std::fabs(std::sin(self->ticks_ * (0.7 + i * 0.45) + i))) : 6;
                lv_obj_set_height(self->bars_[i], h);
            }
        },
        200, this);
    on_changed();
}

MusicQueuePage::~MusicQueuePage()
{
    if (!root_) return;
    lv_obj_t* root = root_;
    on_root_deleted();
    lv_obj_delete(root);
}

void MusicQueuePage::on_root_deleted()
{
    if (tick_) lv_timer_delete(tick_);
    tick_ = nullptr;
    if (listener_) ma_.remove_listener(listener_);
    listener_ = 0;
    load_.cancel();
    root_ = list_ = sheet_ = subtitle_ = now_cover_ = now_label_ = now_title_ = nullptr;
}

// the queue changed (or the page opened): the head, and the list again
// when the current item or the length changed
void MusicQueuePage::on_changed()
{
    if (!root_) return;
    render_head();
    const auto* q = ma_.queue();
    const std::string key = q ? q->id + "|" + std::to_string(q->current_index) + "|" + std::to_string(q->items) +
                                    "|" + (q->shuffle ? "s" : "")
                              : std::string();
    if (key == shown_key_) return;
    shown_key_ = key;
    load_.cancel();
    if (sheet_) lv_obj_delete(sheet_);
    sheet_ = nullptr;
    lv_obj_clean(list_);
    items_.clear();
    complete_ = false;
    loading_ = false;
    first_index_ = q ? std::max(0, q->current_index + 1) : 0;
    load_more();
}

void MusicQueuePage::render_head()
{
    const auto* q = ma_.queue();
    const auto* p = ma_.player();
    const int count = q ? std::max(0, q->items - std::max(0, q->current_index)) : 0;
    double secs = 0;
    for (const auto& it : items_) secs += it.duration;
    std::string sub = p ? p->name : std::string();
    sub += (sub.empty() ? "" : " · ") + trf(TR_MUSIC_QUEUE_SUMMARY, {std::to_string(count)});
    lv_label_set_text(subtitle_, sub.c_str());
    const bool playing = q && q->state == "playing";
    lv_label_set_text(now_label_, tr(playing ? TR_MUSIC_NOW_PLAYING : TR_MUSIC_STATE_PAUSED));
    const MaQueueItem* cur = q && q->current ? &*q->current : nullptr;
    std::string title = cur ? cur->title : std::string();
    if (cur && !cur->artist.empty()) title += " · " + cur->artist;
    lv_label_set_text(now_title_, cur ? title.c_str() : tr(TR_MUSIC_NOTHING));
    // the now cover: rebuilt with the item (child 0 is the placeholder)
    const std::string id = cur ? cur->queue_item_id : std::string();
    if (id == now_cover_id_) return;
    now_cover_id_ = id;
    while (lv_obj_get_child_count(now_cover_) > 1) lv_obj_delete(lv_obj_get_child(now_cover_, 1));
    if (cur) covers_.put(now_cover_, cur->image, 72, 14);
}

void MusicQueuePage::on_scroll_end()
{
    if (!list_ || loading_ || complete_) return;
    // near the end: the next page
    if (lv_obj_get_scroll_bottom(list_) < 400) load_more();
}

void MusicQueuePage::load_more()
{
    if (loading_ || complete_) return;
    loading_ = true;
    load_ = app::core::CancellationSource();
    const auto token = load_.token();
    const size_t from = items_.size();
    ma_.load_queue_items(first_index_ + static_cast<int>(from), kPage,
                         [this, token, from](bool ok, std::vector<MaQueueItem> items, int) {
                             if (token.is_cancelled() || !root_) return;
                             loading_ = false;
                             if (!ok) return;
                             if (static_cast<int>(items.size()) < kPage) complete_ = true;
                             for (auto& it : items) items_.push_back(std::move(it));
                             add_rows(from);
                             render_head();
                         });
}

void MusicQueuePage::add_rows(size_t from)
{
    if (!list_) return;
    if (lv_obj_get_child_count(list_) > from) { // the "empty" line
        lv_obj_delete(lv_obj_get_child(list_, static_cast<int32_t>(from)));
    }
    for (size_t i = from; i < items_.size(); ++i) {
        const MaQueueItem& it = items_[i];
        lv_obj_t* row = ui_box(list_);
        lv_obj_set_size(row, LV_PCT(100), 88);
        lv_obj_set_style_radius(row, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
        lv_obj_set_style_pad_right(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* play = ui_box(row);
        lv_obj_add_flag(play, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_height(play, 88);
        lv_obj_set_flex_grow(play, 1);
        lv_obj_set_style_radius(play, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_color(play, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(play, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(play, 12, 0);
        lv_obj_set_flex_flow(play, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(play, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(play, 16, 0);
        lv_obj_t* no = ui_label(play, UI_FONT_LABEL, UI_TEXT3, std::to_string(i + 1).c_str());
        lv_obj_set_width(no, 28);
        lv_obj_set_style_text_align(no, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_t* cover = cover_box(play, 64, 12, UI_SURFACE2);
        lv_obj_remove_flag(cover, LV_OBJ_FLAG_CLICKABLE);
        covers_.put(cover, it.image, 64, 12);
        lv_obj_t* texts = ui_box(play);
        lv_obj_set_flex_grow(texts, 1);
        lv_obj_set_height(texts, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(texts, 2, 0);
        ui_label_line(texts, UI_FONT_NAME, UI_TEXT, it.title.c_str());
        ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, it.artist.c_str());
        ui_label(play, UI_FONT_LABEL, UI_TEXT3, it.duration > 0 ? music_time(it.duration).c_str() : "");

        lv_obj_t* more = ui_box(row);
        lv_obj_add_flag(more, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(more, 64, 64);
        lv_obj_set_style_radius(more, UI_R_CONTROL, 0);
        lv_obj_set_style_bg_color(more, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(more, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_center(ui_label(more, UI_ICON_BUTTON, UI_TEXT2, LI_ELLIPSIS));

        auto* rd = new RowData{this, i};
        lv_obj_add_event_cb(
            play,
            [](lv_event_t* e) {
                auto* d = static_cast<RowData*>(lv_event_get_user_data(e));
                if (lv_event_get_code(e) == LV_EVENT_DELETE) {
                    delete d;
                    return;
                }
                if (lv_event_get_code(e) == LV_EVENT_CLICKED) d->page->play(d->index);
            },
            LV_EVENT_ALL, rd);
        lv_obj_add_event_cb(
            more, [](lv_event_t* e) {
                auto* d = static_cast<RowData*>(lv_event_get_user_data(e));
                d->page->open_sheet(d->index);
            },
            LV_EVENT_CLICKED, rd);
    }
    if (items_.empty()) {
        lv_obj_t* empty = ui_label(list_, UI_FONT_BODY, UI_TEXT2, tr(TR_MUSIC_QUEUE_END));
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty, 40, 0);
    }
}

void MusicQueuePage::play(size_t index)
{
    if (index < items_.size()) ma_.play_queue_item(items_[index].queue_item_id);
}

// a row's ⋯: play next / similar music / remove
void MusicQueuePage::open_sheet(size_t index)
{
    if (index >= items_.size() || !root_) return;
    const MaQueueItem it = items_[index];
    sheet_ = ui_box(root_);
    lv_obj_add_flag(sheet_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(sheet_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(sheet_, -UI_EDGE, -UI_EDGE);
    lv_obj_set_size(sheet_, kScreen, kScreen);
    lv_obj_set_style_bg_opa(sheet_, 178, 0); // .7
    lv_obj_set_style_bg_color(sheet_, lv_color_hex(kSheetShade), 0);
    lv_obj_add_event_cb(sheet_, [](lv_event_t* e) { // a tap beside the sheet closes it
        auto* self = self_of(e);
        if (lv_event_get_target(e) == self->sheet_) {
            lv_obj_delete_async(self->sheet_);
            self->sheet_ = nullptr;
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* panel = ui_box(sheet_);
    lv_obj_set_size(panel, kScreen, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(panel, 32, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 24, 0);
    lv_obj_set_style_pad_bottom(panel, 40, 0); // the bottom corners lie below the screen
    lv_obj_set_style_margin_bottom(panel, -16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_t* t = ui_label_line(panel, UI_FONT_NAME, UI_TEXT, it.title.c_str());
    lv_obj_set_style_pad_hor(t, 8, 0);
    lv_obj_set_style_pad_bottom(t, 6, 0);
    struct Action {
        const char* icon;
        tr_key_t text;
        uint32_t color;
        int what;
    };
    const Action actions[] = {{LI_ARROW_UP, TR_MUSIC_PLAY_NEXT, UI_TEXT, 0},
                              {LI_RADIO, TR_MUSIC_SIMILAR, UI_TEXT, 1},
                              {LI_TRASH_2, TR_MUSIC_REMOVE, UI_WARN, 2}};
    for (const Action& a : actions) {
        if (a.what == 1 && !ma_.radio_supported(it)) continue; // MA cannot start similar music from it
        lv_obj_t* b = ui_box(panel);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(b, LV_PCT(100), 72);
        lv_obj_set_style_radius(b, 22, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_SURFACE2), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(b, 20, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 14, 0);
        ui_label(b, UI_ICON_BUTTON, a.color == UI_WARN ? UI_WARN : UI_TEXT2, a.icon);
        ui_label(b, UI_FONT_BODY, a.color, tr(a.text));
        struct Act {
            MusicQueuePage* page;
            size_t index;
            int what;
        };
        auto* act = new Act{this, index, a.what};
        lv_obj_add_event_cb(
            b,
            [](lv_event_t* e) {
                auto* d = static_cast<Act*>(lv_event_get_user_data(e));
                if (lv_event_get_code(e) == LV_EVENT_DELETE) {
                    delete d;
                    return;
                }
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                MusicQueuePage* page = d->page;
                if (d->index < page->items_.size()) {
                    const MaQueueItem& item = page->items_[d->index];
                    if (d->what == 0) page->ma_.play_next(item.queue_item_id, page->first_index_ + static_cast<int>(d->index));
                    else if (d->what == 1) page->ma_.start_radio(item.uri);
                    else page->ma_.remove_queue_item(item.queue_item_id);
                }
                lv_obj_delete_async(page->sheet_);
                page->sheet_ = nullptr;
            },
            LV_EVENT_ALL, act);
    }
}

} // namespace app::ui
