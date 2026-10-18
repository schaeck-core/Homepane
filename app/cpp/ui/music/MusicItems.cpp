#include "MusicItems.h"

#include "../../../i18n.h"
#include "../../../lucide_icons.h"
#include "../../../ui_kit.h"
#include "../../../ui_theme.h"

#include <cstdio>

namespace app::ui {

using app::music_assistant::MaQueueItem;
using Enqueue = app::music_assistant::MaController::Enqueue;

namespace {
constexpr int32_t kScreen = 720;
constexpr uint32_t kTones[] = {0x2A2622, 0x1F2428, 0x2A2228, 0x222823, 0x282420, 0x23222A};
constexpr uint32_t kPlaceholderIcon = 0x6F6B66;

struct Sheet {
    lv_obj_t* shade;
    MusicEnqueueDone done;
    bool closing = false; // deleted on the next tick: no second delete, no second choice
};
} // namespace

const char* music_type_label(const std::string& media_type)
{
    if (media_type == "track") return tr(TR_MUSIC_KIND_TRACK);
    if (media_type == "artist") return tr(TR_MUSIC_KIND_ARTIST);
    if (media_type == "album") return tr(TR_MUSIC_KIND_ALBUM);
    if (media_type == "playlist") return tr(TR_MUSIC_KIND_PLAYLIST);
    if (media_type == "radio") return tr(TR_MUSIC_KIND_RADIO);
    return "";
}

const char* music_type_icon(const std::string& media_type)
{
    if (media_type == "artist") return LI_USER;
    if (media_type == "album") return LI_DISC_3;
    if (media_type == "playlist") return LI_LIST_MUSIC;
    if (media_type == "radio") return LI_RADIO;
    return LI_MUSIC;
}

bool music_opens_page(const std::string& media_type)
{
    return media_type == "playlist" || media_type == "album" || media_type == "artist";
}

int32_t music_cover_radius(const std::string& media_type, int32_t size)
{
    if (media_type == "artist") return size / 2;
    return media_type == "radio" ? (size > 100 ? 18 : 14) : (size > 100 ? 14 : 12);
}

std::string music_item_sub(const MaQueueItem& it, bool with_type)
{
    std::string s = with_type ? music_type_label(it.media_type) : "";
    auto add = [&s](const std::string& part) {
        if (part.empty()) return;
        if (!s.empty()) s += " · ";
        s += part;
    };
    add(it.artist);
    if (it.media_type == "track") add(it.album);
    if (s.empty()) s = music_type_label(it.media_type);
    return s;
}

uint32_t music_tone(size_t n) { return kTones[n % 6]; }

lv_obj_t* music_placeholder(lv_obj_t* parent, int32_t size, const MaQueueItem& it, size_t n)
{
    lv_obj_t* box = ui_box(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_radius(box, music_cover_radius(it.media_type, size), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(music_tone(n)), 0);
    lv_obj_center(ui_label(box, size > 100 ? UI_ICON_TILE : UI_ICON_M, kPlaceholderIcon, music_type_icon(it.media_type)));
    return box;
}

std::string music_track_time(double seconds)
{
    const int s = static_cast<int>(seconds + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

std::string music_total_time(double seconds)
{
    const int minutes = static_cast<int>(seconds / 60 + 0.5);
    if (minutes < 60) return trf(TR_MUSIC_MINUTES, {std::to_string(minutes)});
    return trf(TR_MUSIC_HOURS, {std::to_string(minutes / 60), std::to_string(minutes % 60)});
}

void music_window_covers(lv_obj_t* scroller, lv_obj_t* list, const MusicCoverPut& put)
{
    if (!scroller || !list) return;
    lv_area_t view;
    lv_obj_get_coords(scroller, &view);
    const int32_t margin = kScreen / 2; // half a screen above and below
    for (uint32_t i = 0; i < lv_obj_get_child_count(list); ++i) {
        lv_obj_t* item = lv_obj_get_child(list, static_cast<int32_t>(i));
        lv_obj_t* box = lv_obj_get_child(item, 0);
        if (!box) continue;
        lv_area_t a;
        lv_obj_get_coords(item, &a);
        const bool near = a.y2 >= view.y1 - margin && a.y1 <= view.y2 + margin;
        lv_obj_t* cover = nullptr; // the picture (MusicCovers: an image in the box)
        for (uint32_t k = 0; k < lv_obj_get_child_count(box); ++k) {
            lv_obj_t* c = lv_obj_get_child(box, static_cast<int32_t>(k));
            if (lv_obj_check_type(c, &lv_image_class)) cover = c;
        }
        if (near && !cover) put(i, box);
        else if (!near && cover) lv_obj_delete(cover);
    }
}

void music_enqueue_sheet(lv_obj_t* root, const MaQueueItem& item, MusicEnqueueDone done)
{
    lv_obj_t* shade = ui_box(root);
    auto* sheet = new Sheet{shade, std::move(done), false};
    lv_obj_add_flag(shade, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(shade, -UI_EDGE, -UI_EDGE);
    lv_obj_set_size(shade, kScreen, kScreen);
    lv_obj_set_style_bg_opa(shade, 178, 0); // .7
    lv_obj_set_style_bg_color(shade, lv_color_black(), 0);
    lv_obj_add_event_cb(shade, [](lv_event_t* e) {
        auto* s = static_cast<Sheet*>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_DELETE) delete s;
        else if (lv_event_get_code(e) == LV_EVENT_CLICKED && lv_event_get_target(e) == s->shade && !s->closing) {
            s->closing = true; // a tap beside the sheet closes it
            lv_obj_delete_async(s->shade);
        }
    }, LV_EVENT_ALL, sheet);
    lv_obj_t* panel = ui_box(shade);
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
    lv_obj_t* head = ui_box(panel);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(head, 8, 0);
    lv_obj_set_style_pad_bottom(head, 6, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_COLUMN);
    ui_label_line(head, UI_FONT_NAME, UI_TEXT, item.title.c_str());
    ui_label_line(head, UI_FONT_LABEL, UI_TEXT2, music_item_sub(item, true).c_str());
    struct Option {
        const char* icon;
        tr_key_t text;
        Enqueue how;
    };
    const Option options[] = {{LI_PLAY, TR_MUSIC_ENQUEUE_PLAY, Enqueue::Play},
                              {LI_LIST_MUSIC, TR_MUSIC_ENQUEUE_REPLACE, Enqueue::Replace},
                              {LI_ARROW_UP, TR_MUSIC_ENQUEUE_NEXT, Enqueue::Next},
                              {LI_LIST_PLUS, TR_MUSIC_ENQUEUE_ADD, Enqueue::Add}};
    for (const Option& o : options) {
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
        ui_label(b, UI_ICON_BUTTON, UI_TEXT2, o.icon);
        ui_label(b, UI_FONT_BODY, UI_TEXT, tr(o.text));
        lv_obj_set_user_data(b, reinterpret_cast<void*>(static_cast<intptr_t>(o.how)));
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            auto* s = static_cast<Sheet*>(lv_event_get_user_data(e));
            if (s->closing) return;
            s->closing = true;
            lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            const auto how = static_cast<Enqueue>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
            const MusicEnqueueDone done = s->done; // the sheet goes; `done` may close the page too
            lv_obj_delete_async(s->shade);
            if (done) done(how);
        }, LV_EVENT_CLICKED, sheet);
    }
}

} // namespace app::ui
