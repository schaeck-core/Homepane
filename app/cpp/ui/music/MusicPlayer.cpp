#include "MusicPlayer.h"

#include "MusicBrowsePage.h"
#include "MusicQueuePage.h"
#include "../../media/ImageDsc.h"
#include "../../media/ImageLoader.h"
#include "../../../i18n.h"
#include "../../../lucide_icons.h"
#include "../../../ui_kit.h"
#include "../../../ui_theme.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h" // lv_image_cache_drop(): private API since LVGL 9.4

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace app::ui {

using app::music_assistant::MaQueueItem;

namespace {
constexpr int32_t kScreen = 720;
constexpr int32_t kCover = 208;
constexpr uint32_t kMuted = 0x5E5A54;  // shuffle / repeat off
constexpr uint32_t kTrack = 0x1F1E1C;  // progress track
constexpr uint32_t kVolumeFill = 0x2B2A27;
constexpr uint32_t kRoomDotOff = 0x3A3833;
constexpr uint32_t kSlideMs = 220;

MusicPlayer* self_of(lv_event_t* e) { return static_cast<MusicPlayer*>(lv_event_get_user_data(e)); }

// a round or rounded button with one icon
lv_obj_t* icon_button(lv_obj_t* parent, int32_t size, int32_t radius, uint32_t bg, const lv_font_t* font,
                      const char* icon, uint32_t color, lv_obj_t** icon_out = nullptr)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, radius, 0);
    if (bg != UI_BG) {
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    }
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg == UI_TEXT ? 0xC9C6C0 : UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_t* l = ui_label(b, font, color, icon);
    lv_obj_center(l);
    if (icon_out) *icon_out = l;
    return b;
}

void on_click(lv_obj_t* obj, MusicPlayer* self, void (*fn)(lv_event_t*))
{
    lv_obj_add_event_cb(obj, fn, LV_EVENT_CLICKED, self);
}

app::music_assistant::MaController* g_ma = nullptr; // for the button callbacks
} // namespace

std::string music_time(double seconds)
{
    const long s = std::lround(std::max(0.0, seconds));
    char buf[16];
    if (s >= 3600) std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof(buf), "%ld:%02ld", s / 60, s % 60);
    return buf;
}

MusicPlayer::MusicPlayer(app::music_assistant::MaController& ma, std::string image_base)
    : ma_(ma), image_base_(std::move(image_base))
{
    g_ma = &ma_;
}

MusicPlayer::~MusicPlayer()
{
    drop_picture();
    if (root_) {
        lv_obj_t* root = root_;
        on_root_deleted();
        lv_obj_delete(root);
    }
}

void MusicPlayer::prepare()
{
    if (!root_) {
        build();
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool MusicPlayer::is_open() const { return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN); }

void MusicPlayer::open()
{
    prepare();
    drop_picture();
    lv_obj_set_y(root_, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    render();
}

void MusicPlayer::close()
{
    if (!is_open()) return;
    queue_page_.reset();
    browse_page_.reset();
    if (!make_picture()) { // no memory for the picture: just gone
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_y(picture_, 0);
    slide_to(-kScreen, false);
}

void MusicPlayer::close_now()
{
    if (!root_) return;
    queue_page_.reset();
    browse_page_.reset();
    drop_picture();
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void MusicPlayer::begin_pull()
{
    prepare();
    render();
    if (make_picture()) lv_obj_set_y(picture_, -kScreen);
    else open();
}

void MusicPlayer::pull(int32_t dy)
{
    if (picture_) lv_obj_set_y(picture_, std::min<int32_t>(0, -kScreen + dy));
}

void MusicPlayer::end_pull(int32_t dy, bool fast)
{
    if (!picture_) return;
    const bool open = dy > kScreen / 4 || (fast && dy > 60);
    slide_to(open ? 0 : -kScreen, open);
}

// The page drawn once into a picture: pulling and sliding move that, not the
// live page (which would be drawn anew in every frame).
bool MusicPlayer::make_picture()
{
    drop_picture();
    const bool hidden = lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(root_, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(root_);
    picture_buf_ = lv_snapshot_take(root_, lv_display_get_color_format(nullptr));
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    (void)hidden;
    if (!picture_buf_) return false;
    picture_ = lv_image_create(lv_layer_top());
    lv_obj_remove_flag(picture_, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(picture_, picture_buf_);
    lv_obj_set_pos(picture_, 0, -kScreen);
    return true;
}

void MusicPlayer::drop_picture()
{
    if (picture_) {
        lv_anim_delete(picture_, nullptr);
        lv_obj_delete(picture_);
    }
    picture_ = nullptr;
    if (picture_buf_) {
        lv_image_cache_drop(picture_buf_);
        lv_draw_buf_destroy(picture_buf_);
    }
    picture_buf_ = nullptr;
}

// the picture slides to y; at 0 the live page takes its place
void MusicPlayer::slide_to(int32_t y, bool show_after)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, picture_);
    lv_anim_set_user_data(&a, this);
    lv_anim_set_values(&a, lv_obj_get_y(picture_), y);
    lv_anim_set_duration(&a, kSlideMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(obj), v); });
    lv_anim_set_completed_cb(&a, show_after ? [](lv_anim_t* an) { static_cast<MusicPlayer*>(lv_anim_get_user_data(an))->open(); }
                                            : [](lv_anim_t* an) { static_cast<MusicPlayer*>(lv_anim_get_user_data(an))->drop_picture(); });
    lv_anim_start(&a);
}

void MusicPlayer::on_root_deleted()
{
    drop_picture();
    if (tick_) lv_timer_delete(tick_);
    tick_ = nullptr;
    if (listener_) ma_.remove_listener(listener_);
    listener_ = 0;
    cover_load_.cancel();
    if (cover_) lv_image_cache_drop(&cover_dsc_);
    cover_picture_.reset();
    cover_url_.clear();
    rooms_key_.clear();
    queue_page_.reset();
    browse_page_.reset();
    root_ = title_ = subtitle_ = cover_ = cover_icon_ = starting_ = track_ = artist_ = fav_ = nullptr;
    starting_shown_ = false;
    progress_ = progress_fill_ = elapsed_ = duration_ = shuffle_ = play_icon_ = repeat_ = nullptr;
    volume_ = volume_fill_ = volume_value_ = rooms_label_ = rooms_ = empty_ = nullptr;
}

void MusicPlayer::build()
{
    root_ = ui_box(lv_layer_top());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE); // nothing below reacts
    lv_obj_set_size(root_, kScreen, kScreen);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(root_, UI_EDGE, 0);
    lv_obj_set_style_pad_row(root_, 16, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(root_, [](lv_event_t* e) { self_of(e)->on_root_deleted(); }, LV_EVENT_DELETE, this);
    // a swipe up closes it (like pulling a notification shade back up)
    lv_obj_add_event_cb(
        root_,
        [](lv_event_t* e) {
            if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) self_of(e)->close();
        },
        LV_EVENT_GESTURE, this);

    // header: back, room and state, queue, search
    lv_obj_t* head = ui_box(root_);
    lv_obj_set_size(head, LV_PCT(100), 72);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 16, 0);
    on_click(icon_button(head, 64, UI_R_CONTROL, UI_SURFACE, UI_ICON_BUTTON, LI_CHEVRON_UP, UI_TEXT), this,
             [](lv_event_t* e) { self_of(e)->close(); });
    lv_obj_t* names = ui_box(head);
    lv_obj_set_flex_grow(names, 1);
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(names, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(names, 2, 0);
    title_ = ui_label_line(names, UI_FONT_TITLE, UI_TEXT, "");
    subtitle_ = ui_label_line(names, UI_FONT_LABEL, UI_TEXT2, "");
    on_click(icon_button(head, 64, UI_R_CONTROL, UI_SURFACE, UI_ICON_BUTTON, LI_LIST_MUSIC, UI_TEXT), this,
             [](lv_event_t* e) {
                 auto* self = self_of(e);
                 self->queue_page_ = std::make_unique<MusicQueuePage>(*g_ma, self->image_base_, self->root_);
             });
    on_click(icon_button(head, 64, UI_R_CONTROL, UI_SURFACE, UI_ICON_BUTTON, LI_SEARCH, UI_TEXT), this,
             [](lv_event_t* e) {
                 auto* self = self_of(e);
                 self->browse_page_ = std::make_unique<MusicBrowsePage>(*g_ma, self->image_base_, self->root_);
             });

    // now playing: cover, title / artist / favourite, position
    lv_obj_t* now = ui_box(root_);
    lv_obj_set_size(now, LV_PCT(100), kCover);
    lv_obj_set_flex_flow(now, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(now, 20, 0);
    lv_obj_t* art = ui_box(now);
    lv_obj_set_size(art, kCover, kCover);
    lv_obj_set_style_radius(art, UI_R_TILE, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(kTrack), 0);
    cover_icon_ = ui_label(art, UI_ICON_L, UI_MUTED, LI_MUSIC);
    lv_obj_center(cover_icon_);
    cover_ = lv_image_create(art);
    lv_obj_set_style_radius(cover_, UI_R_TILE, 0); // the picture's corners (no clip_corner layer)
    lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(cover_);
    // music asked for that MA still loads (Spotify, YouTube: often seconds):
    // the logo loader over its cover, so the tap visibly took
    starting_ = ui_box(art);
    lv_obj_set_size(starting_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(starting_, UI_R_TILE, 0);
    lv_obj_set_style_bg_color(starting_, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(starting_, LV_OPA_60, 0);
    lv_obj_add_flag(starting_, LV_OBJ_FLAG_HIDDEN); // the loader in it while shown (render)

    lv_obj_t* info = ui_box(now);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, LV_PCT(100));
    lv_obj_set_style_pad_ver(info, 8, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t* top = ui_box(info);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 12, 0);
    lv_obj_t* texts = ui_box(top);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 6, 0);
    track_ = ui_label_line(texts, UI_FONT_TITLE, UI_TEXT, "");
    artist_ = ui_label_line(texts, UI_FONT_BODY, UI_TEXT2, "");
    fav_ = icon_button(top, 64, 32, UI_BG, UI_ICON_M, LI_HEART, UI_TEXT3);
    lv_obj_set_style_margin_top(fav_, -8, 0);
    on_click(fav_, this, [](lv_event_t* e) {
        auto* self = self_of(e);
        self->fav_pending_ = true; // shown at once; the server's update confirms it
        g_ma->favorite_current();
        self->render();
    });

    lv_obj_t* pos = ui_box(info);
    lv_obj_set_size(pos, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pos, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pos, 8, 0);
    progress_ = ui_box(pos);
    lv_obj_set_size(progress_, LV_PCT(100), 6);
    lv_obj_set_style_radius(progress_, 3, 0);
    lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progress_, lv_color_hex(kTrack), 0);
    lv_obj_add_flag(progress_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(progress_, 20); // a 6 px bar: easier to hit
    lv_obj_add_event_cb(
        progress_,
        [](lv_event_t* e) { // tap: jump there
            lv_obj_t* bar = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            const auto* q = g_ma->queue();
            if (!q || !q->current || q->current->duration <= 0) return;
            lv_point_t p;
            lv_indev_get_point(lv_indev_active(), &p);
            lv_area_t a;
            lv_obj_get_coords(bar, &a);
            const double f = std::clamp(static_cast<double>(p.x - a.x1) / lv_area_get_width(&a), 0.0, 1.0);
            g_ma->seek(f * q->current->duration);
        },
        LV_EVENT_CLICKED, this);
    progress_fill_ = ui_box(progress_);
    lv_obj_set_size(progress_fill_, 0, 6);
    lv_obj_set_style_radius(progress_fill_, 3, 0);
    lv_obj_set_style_bg_opa(progress_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progress_fill_, lv_color_hex(UI_TEXT2), 0);
    lv_obj_t* times = ui_box(pos);
    lv_obj_set_size(times, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(times, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(times, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    elapsed_ = ui_label(times, UI_FONT_LABEL, UI_TEXT3, "");
    duration_ = ui_label(times, UI_FONT_LABEL, UI_TEXT3, "");

    // controls: shuffle, previous, play / pause, next, repeat
    lv_obj_t* controls = ui_box(root_);
    lv_obj_set_size(controls, LV_PCT(100), 112);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 24, 0);
    lv_obj_t* shuffle = icon_button(controls, 72, 36, UI_BG, UI_ICON_BUTTON, LI_SHUFFLE, kMuted, &shuffle_);
    on_click(shuffle, this, [](lv_event_t*) {
        const auto* q = g_ma->queue();
        g_ma->set_shuffle(!(q && q->shuffle));
    });
    on_click(icon_button(controls, 88, 44, UI_SURFACE, UI_ICON_M, LI_SKIP_BACK, UI_TEXT), this,
             [](lv_event_t*) { g_ma->previous(); });
    on_click(icon_button(controls, 112, 56, UI_TEXT, UI_ICON_VALUE, LI_PLAY, UI_BG, &play_icon_), this,
             [](lv_event_t*) { g_ma->play_pause(); });
    on_click(icon_button(controls, 88, 44, UI_SURFACE, UI_ICON_M, LI_SKIP_FORWARD, UI_TEXT), this,
             [](lv_event_t*) { g_ma->next(); });
    lv_obj_t* repeat = icon_button(controls, 72, 36, UI_BG, UI_ICON_BUTTON, LI_REPEAT, kMuted, &repeat_);
    on_click(repeat, this, [](lv_event_t*) { g_ma->cycle_repeat(); });

    // volume (drag along the bar) and the sound settings
    lv_obj_t* vrow = ui_box(root_);
    lv_obj_set_size(vrow, LV_PCT(100), 72);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(vrow, 10, 0);
    volume_ = ui_box(vrow);
    lv_obj_set_height(volume_, LV_PCT(100));
    lv_obj_set_flex_grow(volume_, 1);
    lv_obj_set_style_radius(volume_, UI_R_TILE, 0);
    lv_obj_set_style_bg_opa(volume_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(volume_, lv_color_hex(UI_SURFACE), 0);
    lv_obj_add_flag(volume_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(volume_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    volume_fill_ = ui_box(volume_);
    lv_obj_add_flag(volume_fill_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(volume_fill_, 0, LV_PCT(100));
    lv_obj_set_style_radius(volume_fill_, UI_R_TILE, 0); // rounded itself: no clip_corner layer
    lv_obj_set_style_bg_opa(volume_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(volume_fill_, lv_color_hex(kVolumeFill), 0);
    lv_obj_t* vlabels = ui_box(volume_);
    lv_obj_set_size(vlabels, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(vlabels, 24, 0);
    lv_obj_set_flex_flow(vlabels, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vlabels, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* vleft = ui_box(vlabels);
    lv_obj_set_size(vleft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(vleft, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vleft, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(vleft, 12, 0);
    ui_label(vleft, UI_ICON_BUTTON, UI_TEXT2, LI_VOLUME_2);
    ui_label(vleft, UI_FONT_BODY, UI_TEXT, tr(TR_MUSIC_VOLUME));
    volume_value_ = ui_label(vlabels, UI_FONT_NAME, UI_TEXT, "");
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED}) {
        lv_obj_add_event_cb(volume_, [](lv_event_t* e) { self_of(e)->on_volume_drag(e); }, c, this);
    }
    lv_obj_t* eq = icon_button(vrow, 72, UI_R_TILE, UI_SURFACE, UI_ICON_BUTTON, LI_SLIDERS_VERTICAL, UI_TEXT);
    lv_obj_set_height(eq, LV_PCT(100));
    lv_obj_add_flag(eq, LV_OBJ_FLAG_HIDDEN); // sound settings: a later step

    // tabs: the rooms it can play in too / switch to another player
    lv_obj_t* also = ui_box(root_);
    lv_obj_set_width(also, LV_PCT(100));
    lv_obj_set_flex_grow(also, 1);
    lv_obj_set_flex_flow(also, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(also, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(also, 10, 0);
    const char* tabs[] = {tr(TR_MUSIC_TAB_GROUP), tr(TR_MUSIC_TAB_SWITCH)};
    rooms_label_ = ui_segmented(also, tabs, 2);
    lv_obj_set_height(rooms_label_, 48);
    lv_obj_add_event_cb(rooms_label_, [](lv_event_t* e) {
        auto* self = self_of(e);
        self->rooms_tab_ = ui_segmented_selected(self->rooms_label_);
        self->rooms_key_.clear(); // rebuilt for the other tab
        self->render();
    }, LV_EVENT_VALUE_CHANGED, this);
    rooms_ = ui_box(also); // one row, scrolled sideways
    lv_obj_set_size(rooms_, LV_PCT(100), 72);
    lv_obj_add_flag(rooms_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rooms_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(rooms_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(rooms_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(rooms_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rooms_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rooms_, 10, 0);

    listener_ = ma_.add_listener([this] { render(); });
    tick_ = lv_timer_create([](lv_timer_t* t) { static_cast<MusicPlayer*>(lv_timer_get_user_data(t))->on_tick(); }, 500,
                            this);
}

void MusicPlayer::on_volume_drag(lv_event_t* e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(volume_, &a);
    const int v = std::clamp(static_cast<int>(std::lround(100.0 * (p.x - a.x1) / lv_area_get_width(&a))), 0, 100);
    dragged_volume_ = v;
    lv_obj_set_width(volume_fill_, lv_area_get_width(&a) * v / 100);
    lv_label_set_text_fmt(volume_value_, "%d", v);
    const bool released = lv_event_get_code(e) == LV_EVENT_RELEASED;
    // while dragging at most every 250 ms, the last value on release
    if (released || lv_tick_elaps(volume_sent_at_) >= 250) {
        ma_.set_volume(v);
        volume_sent_at_ = lv_tick_get();
    }
    if (released) dragged_volume_ = -1;
}

void MusicPlayer::on_tick()
{
    // the position, between the server's updates
    if ((ma_.starting() != nullptr) != starting_shown_) render(); // it played, or gave up
    const auto* q = ma_.queue();
    const double dur = starting_shown_ ? 0 : q && q->current ? q->current->duration : 0;
    const double el = starting_shown_ ? 0 : ma_.elapsed();
    lv_label_set_text(elapsed_, dur > 0 || el > 0 ? music_time(el).c_str() : "");
    lv_label_set_text(duration_, dur > 0 ? music_time(dur).c_str() : "");
    lv_obj_update_layout(progress_);
    const int32_t w = lv_obj_get_width(progress_);
    lv_obj_set_width(progress_fill_, dur > 0 ? static_cast<int32_t>(w * std::min(1.0, el / dur)) : 0);
}

void MusicPlayer::render()
{
    if (!root_) return;
    using app::music_assistant::MaPlayer;
    const MaPlayer* p = ma_.player();
    const auto* q = ma_.queue();
    // music asked for that does not play yet: shown already, with a loader
    const MaQueueItem* starting = ma_.starting();
    const MaQueueItem* item = starting ? starting : q && q->current ? &*q->current : nullptr;
    if (starting && !starting_shown_) {
        lv_obj_center(ui_loader(starting_, 22)); // it builds up, then pulses
        lv_obj_remove_flag(starting_, LV_OBJ_FLAG_HIDDEN);
    } else if (!starting && starting_shown_) {
        lv_obj_add_flag(starting_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(starting_); // its animations go with it
    }
    starting_shown_ = starting != nullptr;

    const char* state = starting                                        ? tr(TR_MUSIC_STATE_STARTING)
                        : !ma_.ready()                                   ? tr(TR_MUSIC_STATE_OFFLINE)
                        : q && q->state == "playing"                    ? tr(TR_MUSIC_STATE_PLAYING)
                        : q && q->state == "paused"                     ? tr(TR_MUSIC_STATE_PAUSED)
                                                                       : tr(TR_MUSIC_STATE_IDLE);
    lv_label_set_text(title_, p ? p->name.c_str() : "Music Assistant");
    lv_label_set_text(subtitle_, trf(TR_MUSIC_SUBTITLE, {state}).c_str());

    if (item) {
        lv_label_set_text(track_, item->title.c_str());
        std::string sub = item->artist;
        if (!item->album.empty() && item->album != item->title) sub += (sub.empty() ? "" : " · ") + item->album;
        lv_label_set_text(artist_, sub.c_str());
    } else {
        lv_label_set_text(track_, tr(ma_.ready() ? TR_MUSIC_NOTHING : TR_MUSIC_STATE_OFFLINE));
        lv_label_set_text(artist_, tr(ma_.ready() ? TR_MUSIC_NOTHING_HINT : TR_MUSIC_OFFLINE_HINT));
    }
    const bool fav = item && (item->favorite || fav_pending_);
    if (item && item->favorite) fav_pending_ = false;
    lv_obj_set_style_text_color(lv_obj_get_child(fav_, 0), lv_color_hex(fav ? UI_ACCENT : UI_TEXT3), 0);
    if (item) lv_obj_remove_flag(fav_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(fav_, LV_OBJ_FLAG_HIDDEN);
    render_cover(item);

    lv_label_set_text(play_icon_, q && q->state == "playing" ? LI_PAUSE : LI_PLAY);
    lv_obj_set_style_text_color(shuffle_, lv_color_hex(q && q->shuffle ? UI_TEXT : kMuted), 0);
    lv_label_set_text(repeat_, q && q->repeat == "one" ? LI_REPEAT_1 : LI_REPEAT);
    lv_obj_set_style_text_color(repeat_, lv_color_hex(q && q->repeat != "off" ? UI_TEXT : kMuted), 0);

    if (dragged_volume_ < 0) {
        const int v = p && p->volume ? *p->volume : 0;
        lv_label_set_text(volume_value_, p && p->volume ? std::to_string(v).c_str() : "–");
        lv_obj_update_layout(volume_);
        lv_obj_set_width(volume_fill_, lv_obj_get_width(volume_) * v / 100);
    }

    // the chips below the tabs: rebuilt only when they change
    //  tab 0: the rooms MA can group with this player (tap: joins / leaves)
    //  tab 1: every available player (tap: this page controls that one)
    std::string key = std::to_string(rooms_tab_) + "|" + ma_.player_id() + "|";
    std::vector<const MaPlayer*> shown;
    if (rooms_tab_ == 0 && p) {
        for (const auto& id : p->can_group_with) {
            const MaPlayer* o = ma_.store().player(id);
            if (o && o->available) shown.push_back(o);
        }
    } else if (rooms_tab_ == 1) {
        const MaPlayer* current = ma_.player(); // first: the one controlled now
        if (current && current->available) shown.push_back(current);
        for (const auto& o : ma_.store().players()) {
            if (o.available && &o != current) shown.push_back(&o);
        }
    }
    for (const MaPlayer* o : shown) key += o->id + (ma_.is_member(o->id) ? "+" : "-") + o->name + ";";
    if (key != rooms_key_) {
        rooms_key_ = key;
        lv_obj_clean(rooms_);
        for (const MaPlayer* o : shown) {
            const bool on = rooms_tab_ == 0 ? ma_.is_member(o->id) : o->id == ma_.player_id();
            lv_obj_t* b = ui_box(rooms_);
            lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLL_CHAIN);
            lv_obj_set_size(b, (kScreen - 2 * UI_EDGE - 2 * 10) / 3, 72);
            lv_obj_set_style_radius(b, 22, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(on ? UI_ACTIVE : UI_SURFACE), 0);
            lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_hor(b, 12, 0);
            lv_obj_set_style_pad_column(b, 10, 0);
            lv_obj_t* dot = ui_box(b);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_radius(dot, 5, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(on ? UI_ACCENT : kRoomDotOff), 0);
            lv_obj_t* l = ui_label_line(b, UI_FONT_LABEL, on ? UI_TEXT : UI_TEXT2, o->name.c_str());
            lv_obj_set_flex_grow(l, 1);
            lv_obj_set_style_max_width(l, 160, 0);
            auto* id = new std::string(o->id);
            lv_obj_set_user_data(b, reinterpret_cast<void*>(static_cast<intptr_t>(rooms_tab_)));
            lv_obj_add_event_cb(
                b,
                [](lv_event_t* e) {
                    const auto* pid = static_cast<std::string*>(lv_event_get_user_data(e));
                    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
                        delete pid;
                        return;
                    }
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return; // only a tap
                    lv_obj_t* chip = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                    if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(chip)) == 0) {
                        g_ma->set_member(*pid, !g_ma->is_member(*pid));
                    } else {
                        g_ma->set_player(*pid); // this page only; the room's default stays in the settings
                    }
                },
                LV_EVENT_ALL, id);
        }
        // none to group with: MA cannot (e.g. a Sonos only with other Sonos) - said
        if (rooms_tab_ == 0 && shown.empty() && p) {
            lv_obj_t* hint = ui_label(rooms_, UI_FONT_LABEL, UI_TEXT3, tr(TR_MUSIC_NO_GROUP));
            lv_obj_set_width(hint, LV_PCT(100));
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_pad_left(hint, 4, 0);
        }
    }
    on_tick();
}

void MusicPlayer::render_cover(const MaQueueItem* item)
{
    const std::string url = item ? app::music_assistant::ma_image_url(image_base_, item->image, kCover) : std::string();
    if (url == cover_url_) return;
    cover_url_ = url;
    cover_load_.cancel();
    if (url.empty()) {
        lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cover_icon_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    app::media::ImageLoader* loader = app::media::image_loader();
    if (!loader) return;
    cover_load_ = app::core::CancellationSource();
    app::media::ImageLoader::Options o;
    o.cache = true;
    loader->load(
        url, kCover, kCover, true, cover_load_.token(),
        [this, url](std::shared_ptr<app::media::DecodedImage> picture, const std::string&) {
            if (!root_ || url != cover_url_ || !picture) return;
            lv_image_cache_drop(&cover_dsc_);
            cover_picture_ = std::move(picture);
            app::media::set_image_dsc(cover_dsc_, *cover_picture_);
            lv_image_set_src(cover_, &cover_dsc_);
            lv_obj_center(cover_);
            lv_obj_remove_flag(cover_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(cover_icon_, LV_OBJ_FLAG_HIDDEN);
        },
        o);
}

} // namespace app::ui
