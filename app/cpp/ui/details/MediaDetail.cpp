#include "MediaDetail.h"

#include "../tiles/MediaTile.h"
#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace app::ui {

namespace {
MediaDetail* self_of(lv_event_t* e) { return static_cast<MediaDetail*>(lv_event_get_user_data(e)); }
void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }
void raw_control_cb(lv_event_t* e)
{
    self_of(e)->on_control(static_cast<int>(lv_obj_get_index(lv_event_get_current_target_obj(e))));
}
void raw_volume_cb(lv_event_t* e) { self_of(e)->on_volume(lv_event_get_code(e) == LV_EVENT_RELEASED); }
void raw_tick_cb(lv_timer_t* t) { static_cast<MediaDetail*>(lv_timer_get_user_data(t))->tick(); }

// days since 1970-01-01 of a civil date (Howard Hinnant's algorithm)
long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

lv_obj_t* round_button(lv_obj_t* parent, int32_t size, uint32_t bg, const lv_font_t* font, const char* icon,
                       uint32_t fg)
{
    lv_obj_t* b = ui_box(parent);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, bg == 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_ACTIVE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_center(ui_label(b, font, fg, icon));
    return b;
}
} // namespace

std::string MediaDetail::clock_text(double seconds)
{
    const long s = seconds > 0 ? static_cast<long>(seconds) : 0;
    char buf[16];
    if (s >= 3600) std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof(buf), "%ld:%02ld", s / 60, s % 60);
    return buf;
}

std::optional<double> MediaDetail::parse_iso_epoch(const std::string& iso)
{
    int y, mo, d, h, mi;
    double sec;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6) return std::nullopt;
    double epoch = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400.0 + h * 3600.0 +
                   mi * 60.0 + sec;
    // offset: "+02:00" / "-05:00" / "Z" after the time
    const size_t t = iso.find('T');
    const size_t sign = iso.find_first_of("+-", t);
    if (sign != std::string::npos) {
        int oh = 0, om = 0;
        if (std::sscanf(iso.c_str() + sign + 1, "%d:%d", &oh, &om) >= 1) {
            const double off = oh * 3600.0 + om * 60.0;
            epoch += iso[sign] == '+' ? -off : off;
        }
    }
    return epoch;
}

MediaDetail::MediaDetail(Send send) : send_(std::move(send)) {}

MediaDetail::~MediaDetail()
{
    if (timer_) lv_timer_delete(timer_);
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_delete(root_);
}

void MediaDetail::open(const State& state)
{
    if (root_) return;
    state_ = state;
    build();
    refresh();
    timer_ = lv_timer_create(raw_tick_cb, 1000, this); // the progress moves while playing
}

void MediaDetail::update(const State& state)
{
    if (!root_ || dragging_) return;
    state_ = state;
    refresh();
}

void MediaDetail::on_root_deleted()
{
    root_ = nullptr;
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
}

void MediaDetail::tick() { refresh_progress(); }

void MediaDetail::build()
{
    ui_detail_t d = ui_detail_open(state_.name.c_str(), "");
    root_ = d.root;
    subtitle_ = d.subtitle;
    lv_obj_remove_flag(subtitle_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
    lv_obj_t* c = d.content;

    lv_obj_t* top = ui_box(c);
    lv_obj_set_size(top, LV_PCT(100), 208);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 20, 0);
    lv_obj_t* art = ui_box(top);
    lv_obj_set_size(art, 208, 208);
    lv_obj_set_style_radius(art, UI_R_TILE, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    art_icon_ = ui_label(art, UI_ICON_L, UI_MUTED, LI_MUSIC);
    lv_obj_center(art_icon_);
    lv_obj_t* info = ui_box(top);
    lv_obj_set_height(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_pad_ver(info, 8, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t* texts = ui_box(info);
    lv_obj_set_size(texts, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 6, 0);
    title_ = ui_label_line(texts, UI_FONT_TITLE, UI_TEXT, "");
    artist_ = ui_label_line(texts, UI_FONT_BODY, UI_TEXT2, "");
    lv_obj_t* progress = ui_box(info);
    lv_obj_set_size(progress, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(progress, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(progress, 8, 0);
    lv_obj_t* track = ui_box(progress);
    lv_obj_set_size(track, LV_PCT(100), 6);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    bar_ = ui_box(track);
    lv_obj_set_size(bar_, 0, LV_PCT(100));
    lv_obj_set_style_radius(bar_, 3, 0);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(UI_TEXT2), 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
    lv_obj_t* times = ui_box(progress);
    lv_obj_set_size(times, LV_PCT(100), LV_SIZE_CONTENT);
    elapsed_ = ui_label(times, UI_FONT_LABEL, UI_TEXT3, "");
    total_ = ui_label(times, UI_FONT_LABEL, UI_TEXT3, "");
    lv_obj_align(total_, LV_ALIGN_TOP_RIGHT, 0, 0);

    controls_ = ui_box(c);
    lv_obj_remove_flag(controls_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(controls_, LV_PCT(100), 112);
    lv_obj_set_flex_flow(controls_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls_, 24, 0);
    lv_obj_t* buttons[] = {
        round_button(controls_, 72, 0, UI_ICON_M, LI_SHUFFLE, UI_TEXT3),
        round_button(controls_, 88, UI_SURFACE, UI_ICON_M, LI_SKIP_BACK, UI_TEXT),
        round_button(controls_, 112, UI_TEXT, UI_ICON_VALUE, LI_PLAY, 0x000000),
        round_button(controls_, 88, UI_SURFACE, UI_ICON_M, LI_SKIP_FORWARD, UI_TEXT),
        round_button(controls_, 72, 0, UI_ICON_M, LI_REPEAT, UI_TEXT3),
    };
    lv_obj_set_style_bg_color(buttons[2], lv_color_hex(0xC9C6C0), LV_STATE_PRESSED);
    for (lv_obj_t* b : buttons) lv_obj_add_event_cb(b, raw_control_cb, LV_EVENT_CLICKED, this);

    volume_ = ui_slider_row(c, LI_VOLUME_2, tr(TR_MEDIA_VOLUME));
    lv_obj_add_event_cb(volume_, raw_volume_cb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(volume_, raw_volume_cb, LV_EVENT_RELEASED, this);
}

void MediaDetail::refresh()
{
    if (!root_) return;
    const State& s = state_;
    lv_label_set_text(subtitle_, MediaTile::status_text(s.state, "", "").c_str());
    lv_label_set_text(art_icon_, s.tv ? LI_TV : LI_MUSIC);
    lv_label_set_text(title_, s.title.empty() ? tr(s.state == "off" ? TR_STATE_OFF : TR_MEDIA_NOTHING) : s.title.c_str());
    std::string sub = s.artist;
    if (!s.album.empty()) sub += (sub.empty() ? "" : " · ") + s.album;
    lv_label_set_text(artist_, sub.c_str());
    lv_obj_t* play_icon = lv_obj_get_child(lv_obj_get_child(controls_, 2), 0);
    lv_label_set_text(play_icon, s.state == "playing" ? LI_PAUSE : s.state == "off" ? LI_POWER : LI_PLAY);
    lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(controls_, 0), 0),
                                lv_color_hex(s.shuffle ? UI_ACCENT : UI_TEXT3), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(controls_, 4), 0),
                                lv_color_hex(s.repeat == "all" || s.repeat == "one" ? UI_ACCENT : UI_TEXT3), 0);
    const int vol = static_cast<int>(std::lround(s.volume.value_or(0) * 100));
    ui_slider_row_set(volume_, vol, std::to_string(vol).c_str());
    refresh_progress();
}

void MediaDetail::refresh_progress()
{
    if (!root_) return;
    const State& s = state_;
    if (!s.duration || *s.duration <= 0) {
        lv_obj_set_width(bar_, 0);
        lv_label_set_text(elapsed_, "");
        lv_label_set_text(total_, "");
        return;
    }
    double pos = s.position.value_or(0);
    if (s.state == "playing" && s.position_updated_epoch_s) pos += static_cast<double>(std::time(nullptr)) - *s.position_updated_epoch_s;
    pos = std::fmax(0.0, std::fmin(*s.duration, pos));
    lv_obj_set_width(bar_, LV_PCT(static_cast<int32_t>(pos / *s.duration * 100)));
    lv_label_set_text(elapsed_, clock_text(pos).c_str());
    lv_label_set_text(total_, clock_text(*s.duration).c_str());
}

void MediaDetail::on_control(int index)
{
    switch (index) {
    case 0:
        state_.shuffle = !state_.shuffle;
        send_("shuffle_set", state_.shuffle ? "{\"shuffle\":true}" : "{\"shuffle\":false}");
        break;
    case 1: send_("media_previous_track", ""); break;
    case 2:
        if (state_.state == "off") {
            send_("turn_on", "");
        } else {
            state_.state = state_.state == "playing" ? "paused" : "playing";
            send_("media_play_pause", "");
        }
        break;
    case 3: send_("media_next_track", ""); break;
    case 4:
        state_.repeat = state_.repeat == "off" || state_.repeat.empty() ? "all" : state_.repeat == "all" ? "one" : "off";
        send_("repeat_set", "{\"repeat\":\"" + state_.repeat + "\"}");
        break;
    default: break;
    }
    refresh();
}

void MediaDetail::on_volume(bool released)
{
    const int v = ui_slider_row_value(volume_);
    dragging_ = !released;
    lv_label_set_text(lv_obj_get_child(volume_, 2), std::to_string(v).c_str());
    if (!released) return;
    state_.volume = v / 100.0;
    char json[40];
    std::snprintf(json, sizeof(json), "{\"volume_level\":%.2f}", v / 100.0);
    send_("volume_set", json);
}

} // namespace app::ui
