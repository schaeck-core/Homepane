#include "MusicTrackPopover.h"

#include "../../../lucide_icons.h"
#include "../../../ui_kit.h"
#include "../../../ui_theme.h"

namespace app::ui {

namespace {
constexpr int32_t kHeight = 88;
constexpr int32_t kCover = 64;
constexpr uint32_t kShownMs = 5000;
constexpr uint32_t kSlideMs = 220;
constexpr uint32_t kTickMs = 200;

MusicTrackPopover* self_of(lv_event_t* e) { return static_cast<MusicTrackPopover*>(lv_event_get_user_data(e)); }
void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }

// a swipe also ends in a click: a swipe up sends it away, a tap opens the player
bool g_swiped = false;
void raw_input_cb(lv_event_t* e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_swiped = false;
    } else if (code == LV_EVENT_GESTURE) {
        g_swiped = true;
        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) self_of(e)->hide();
    } else if (code == LV_EVENT_CLICKED && !g_swiped) {
        self_of(e)->on_clicked();
    }
}
} // namespace

MusicTrackPopover::MusicTrackPopover(app::music_assistant::MaController& ma, std::string image_base,
                                     std::function<bool()> hidden, std::function<void()> open_player)
    : ma_(ma), covers_(std::move(image_base)), hidden_(std::move(hidden)), open_player_(std::move(open_player))
{
    listener_ = ma_.add_listener([this] { on_change(); });
    on_change();
}

MusicTrackPopover::~MusicTrackPopover()
{
    ma_.remove_listener(listener_);
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_t* root = root_;
    on_root_deleted();
    lv_obj_delete(root);
}

std::string MusicTrackPopover::track_key(const app::music_assistant::MaQueue* queue)
{
    if (!queue || !queue->current) return {};
    const auto& item = *queue->current;
    return queue->id + "\n" + item.title + "\n" + item.artist;
}

void MusicTrackPopover::on_change()
{
    if (!ma_.ready()) {
        known_ = false; // after a reconnect: the state then is no change
        return;
    }
    const auto* q = ma_.queue();
    const std::string key = track_key(q);
    const std::string queue = q ? q->id : std::string();
    if (key == key_ && queue == queue_) return;
    const bool another_track = known_ && queue == queue_ && !key.empty();
    known_ = true;
    key_ = key;
    queue_ = queue;
    if (!another_track || q->state != "playing" || (hidden_ && hidden_())) return;
    show(*q->current);
}

void MusicTrackPopover::show(const app::music_assistant::MaQueueItem& item)
{
    if (!root_) {
        root_ = ui_box(lv_layer_top());
        lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_GESTURE_BUBBLE); // swipes are ours, not lv_layer_top's
        lv_obj_set_size(root_, 720 - 2 * UI_EDGE, kHeight);
        lv_obj_set_style_radius(root_, UI_R_TILE, 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(root_, lv_color_hex(UI_SURFACE2), 0);
        lv_obj_set_style_bg_color(root_, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
        lv_obj_set_style_pad_left(root_, 12, 0);
        lv_obj_set_style_pad_right(root_, 24, 0);
        lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(root_, 16, 0);
        lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
        for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_GESTURE, LV_EVENT_CLICKED}) {
            lv_obj_add_event_cb(root_, raw_input_cb, c, this);
        }

        cover_ = ui_box(root_);
        lv_obj_set_size(cover_, kCover, kCover);
        lv_obj_set_style_radius(cover_, 16, 0);
        lv_obj_set_style_bg_opa(cover_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cover_, lv_color_hex(UI_SURFACE), 0);
        lv_obj_center(ui_label(cover_, UI_ICON_M, UI_TEXT3, LI_MUSIC));

        lv_obj_t* texts = ui_box(root_);
        lv_obj_set_height(texts, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(texts, 1);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(texts, 2, 0);
        title_ = ui_label_line(texts, UI_FONT_NAME, UI_TEXT, "");
        artist_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");
        ui_label(root_, UI_ICON_BUTTON, UI_ACCENT, LI_AUDIO_LINES);

        lv_obj_align(root_, LV_ALIGN_TOP_MID, 0, -kHeight);
        tick_ = lv_timer_create([](lv_timer_t* t) { static_cast<MusicTrackPopover*>(lv_timer_get_user_data(t))->on_tick(); },
                                kTickMs, this);
    } else {
        // the next track while it is shown: the old cover goes
        while (lv_obj_get_child_count(cover_) > 1) lv_obj_delete(lv_obj_get_child(cover_, 1));
    }
    lv_label_set_text(title_, item.title.c_str());
    lv_label_set_text(artist_, item.artist.c_str());
    covers_.put(cover_, item.image, kCover, 16);
    shown_at_ = lv_tick_get();
    leaving_ = false;
    slide(UI_EDGE, false);
}

void MusicTrackPopover::hide()
{
    if (!root_ || leaving_) return;
    leaving_ = true;
    slide(-kHeight, true);
}

void MusicTrackPopover::slide(int32_t to, bool delete_after)
{
    lv_anim_delete(root_, nullptr);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root_);
    lv_anim_set_values(&a, lv_obj_get_y(root_), to);
    lv_anim_set_duration(&a, kSlideMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(obj), v); });
    if (delete_after) lv_anim_set_completed_cb(&a, [](lv_anim_t* an) { lv_obj_delete(static_cast<lv_obj_t*>(an->var)); });
    lv_anim_start(&a);
}

void MusicTrackPopover::on_root_deleted()
{
    if (tick_) lv_timer_delete(tick_);
    tick_ = nullptr;
    root_ = cover_ = title_ = artist_ = nullptr;
    leaving_ = false;
}

void MusicTrackPopover::on_tick()
{
    // the player or the screensaver opened over it: gone at once
    if (hidden_ && hidden_()) {
        lv_obj_delete(root_); // its slide with it
        return;
    }
    if (lv_tick_elaps(shown_at_) >= kShownMs) hide();
}

void MusicTrackPopover::on_clicked()
{
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN); // not over the player; deleted after the slide
    hide();
    if (open_player_) open_player_();
}

} // namespace app::ui
