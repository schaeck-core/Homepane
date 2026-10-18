#include "Screensaver.h"

#include "../media/ImageDsc.h"
#include "../core/DateText.h"
#include "../media/ImageLoader.h"
#include "tiles/TileWidget.h"
#include "tiles/ValueTile.h"
#include "../../i18n.h"

#include "../../ui_kit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace app::ui {

namespace {
using app::eventcalendar::Event;

constexpr uint32_t kPanel = 0x0C0C0B;  // the boxes over the picture
constexpr uint32_t kSoft = 0xE4E0D9;   // date, details
constexpr uint32_t kSofter = 0xC9C5BE; // hints
constexpr uint32_t kSun = 0xE9CF9F;    // weather icon
constexpr uint32_t kTickMs = 200;
constexpr uint32_t kRevealMs = 2500; // the longest wait for the first picture
constexpr int32_t kMusicCover = 56;

Screensaver* self_of(lv_event_t* e) { return static_cast<Screensaver*>(lv_event_get_user_data(e)); }
void raw_deleted_cb(lv_event_t* e) { self_of(e)->on_root_deleted(); }

// a swipe also ends in a click: only a press without a swipe ends the screensaver
bool g_swiped = false;
void raw_input_cb(lv_event_t* e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g_swiped = false;
    } else if (code == LV_EVENT_GESTURE) {
        g_swiped = true;
        self_of(e)->on_gesture(lv_indev_get_gesture_dir(lv_indev_active()));
    } else if (code == LV_EVENT_CLICKED && !g_swiped) {
        lv_obj_delete_async(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
    }
}

using app::core::DateStyle;
using app::core::local_tm;

// "Sa. 10. Okt." / "Samstag, 10. Okt." (in the order of the settings)
std::string date_text(double when, bool long_day)
{
    return app::core::date_text(when, long_day ? DateStyle::long_weekday : DateStyle::weekday_day_month);
}

// The clock's digits with a small AM/PM beside them (12-hour format only).
lv_obj_t* clock_row(lv_obj_t* parent, const lv_font_t* digits, const lv_font_t* small, int32_t space,
                    lv_obj_t** meridiem)
{
    lv_obj_t* row = ui_box(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_t* time = ui_label(row, digits, 0xFFFFFF, "");
    lv_obj_set_style_text_letter_space(time, space, 0);
    *meridiem = ui_label(row, small, 0xFFFFFF, "");
    lv_obj_set_style_pad_bottom(*meridiem, lv_font_get_line_height(digits) / 6, 0);
    return time;
}

// The add-on scales and crops an event's picture to the screen itself.
std::string event_picture_url(const std::string& entity)
{
    return "/api/eventcalendar/images/" + entity + "?width=720&height=720&fit=cover";
}

// The background is kept decoded between two screensavers; a signed HA
// media URL changes its query each time, so it is matched without it.
std::string cache_key(const std::string& url) { return url.substr(0, url.find('?')); }

// A box over the picture: dark, rounded, semi-transparent.
lv_obj_t* panel(lv_obj_t* parent, lv_opa_t opa, int32_t radius)
{
    lv_obj_t* p = ui_box(parent);
    lv_obj_set_style_radius(p, radius, 0);
    lv_obj_set_style_bg_opa(p, opa, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(kPanel), 0);
    return p;
}

// A vertical fade over the picture (for legibility): from `opa` to clear.
void fade(lv_obj_t* parent, bool top, int32_t height, lv_opa_t opa)
{
    lv_obj_t* f = ui_box(parent);
    lv_obj_set_size(f, LV_PCT(100), height);
    lv_obj_align(f, top ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(f, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(f, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(f, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(f, top ? opa : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(f, top ? LV_OPA_TRANSP : opa, 0);
}

lv_obj_t* icon_text(lv_obj_t* row, const char* icon, const lv_font_t* font, uint32_t color, lv_obj_t** label)
{
    lv_obj_t* box = ui_box(row);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 8, 0);
    ui_label(box, UI_ICON_S, color, icon);
    *label = ui_label(box, font, color, "");
    return box;
}
} // namespace

// ── texts ──

std::string Screensaver::chip_text(const Event& e)
{
    if (e.all_day && e.end > e.start + 86400) {
        return date_text(e.start, false) + " – " + date_text(e.end - 1, false); // the end is exclusive
    }
    return date_text(e.start, false);
}

std::string Screensaver::time_text(const Event& e)
{
    if (e.all_day) return tr(TR_CALENDAR_ALL_DAY);
    const struct tm a = local_tm(e.start), b = local_tm(e.end);
    if (e.end <= e.start) return app::core::time_oclock_text(a); // no end given
    if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) return app::core::time_range_text(a, b);
    return trf(TR_SCREENSAVER_TIME_UNTIL, {app::core::time_oclock_text(a), date_text(e.end, false)});
}

std::string Screensaver::distance_text(const Event& e)
{
    if (!e.distance_km) return {};
    return trf(TR_SCREENSAVER_DISTANCE, {format_number(*e.distance_km, 1)});
}

uint32_t Screensaver::waste_color(const std::string& title)
{
    std::string t;
    for (unsigned char c : title) t += static_cast<char>(std::tolower(c));
    auto has = [&t](const char* s) { return t.find(s) != std::string::npos; };
    if (has("gelb") || has("yellow")) return 0xD9B840;
    if (has("papier") || has("blau") || has("paper")) return 0x6C97D4;
    if (has("bio") || has("braun")) return 0x9A7A52;
    if (has("glas")) return 0x6FA884;
    return 0x8A8A88;
}

// ── life cycle ──

Screensaver::~Screensaver()
{
    pending_.cancel();
    if (!root_) return;
    lv_obj_remove_event_cb_with_user_data(root_, raw_deleted_cb, this);
    lv_obj_t* root = root_;
    on_root_deleted();
    lv_obj_delete(root);
}

void Screensaver::show(Sources sources)
{
    if (root_) hide();
    src_ = std::move(sources);
    card_ = first_card();
    card_event_.clear();
    picture_url_.clear();
    paused_until_ = 0;
    if (!src_.start_event.empty()) {
        const auto& events = src_.feed ? src_.feed->feed().events : std::vector<Event>{};
        for (size_t i = 0; i < events.size(); ++i) {
            if (events[i].id == src_.start_event) {
                card_ = static_cast<int>(i) + 1;
                card_event_ = events[i].id;
            }
        }
    }
    if (card_ > 0 && card_event_.empty()) card_event_ = src_.feed->feed().events[static_cast<size_t>(card_ - 1)].id;
    build();
    card_since_ = lv_tick_get();
    render();
}

void Screensaver::hide()
{
    if (root_) lv_obj_delete(root_);
}

void Screensaver::on_root_deleted()
{
    if (tick_) lv_timer_delete(tick_);
    tick_ = nullptr;
    pending_.cancel();
    if (image_) lv_image_cache_drop(&dsc_);
    picture_.reset();
    cover_load_.cancel();
    if (cover_) lv_image_cache_drop(&cover_dsc_);
    cover_.reset();
    cover_url_.clear();
    music_key_.clear();
    music_ = music_cover_ = music_image_ = music_title_ = music_artist_ = nullptr;
    root_ = image_ = segments_ = clock_box_ = time_ = meridiem_ = date_ = nullptr;
    big_box_ = big_time_ = big_meridiem_ = big_date_ = big_weather_icon_ = big_weather_ = nullptr;
    waste_ = waste_dot_ = waste_title_ = waste_hint_ = nullptr;
    own_card_ = own_weather_icon_ = own_weather_ = own_inside_ = own_next_ = nullptr;
    event_card_ = ev_chip_ = ev_title_ = ev_time_ = ev_place_ = ev_distance_ = symbol_badge_ = nullptr;
}

void Screensaver::build()
{
    root_ = ui_box(lv_layer_top());
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN); // until the picture is there: reveal()
    shown_at_ = lv_tick_get();
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_GESTURE_BUBBLE); // swipes are ours, not lv_layer_top's
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x1C1B19), 0);
    lv_obj_add_event_cb(root_, raw_deleted_cb, LV_EVENT_DELETE, this);
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_GESTURE, LV_EVENT_CLICKED}) {
        lv_obj_add_event_cb(root_, raw_input_cb, c, this);
    }

    // the picture, full screen, and fades for the boxes over it
    image_ = lv_image_create(root_);
    lv_obj_center(image_);
    fade(root_, true, 240, 158);  // .62
    fade(root_, false, 300, 128); // .5

    // progress segments (events view)
    segments_ = ui_box(root_);
    lv_obj_set_size(segments_, 672, 5);
    lv_obj_align(segments_, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_flex_flow(segments_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(segments_, 6, 0);

    // clock top left (events view)
    clock_box_ = panel(root_, 168, 28); // .66
    lv_obj_set_size(clock_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(clock_box_, 20, 36);
    lv_obj_set_style_pad_top(clock_box_, 18, 0);
    lv_obj_set_style_pad_hor(clock_box_, 22, 0);
    lv_obj_set_style_pad_bottom(clock_box_, 16, 0);
    lv_obj_set_flex_flow(clock_box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(clock_box_, 6, 0);
    time_ = clock_row(clock_box_, font_manrope_600_96, font_manrope_600_32, -4, &meridiem_);
    date_ = ui_label(clock_box_, UI_FONT_BODY, kSoft, "");

    // large clock bottom left (picture view)
    big_box_ = panel(root_, 168, 28);
    lv_obj_set_size(big_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(big_box_, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_pad_top(big_box_, 20, 0);
    lv_obj_set_style_pad_hor(big_box_, 26, 0);
    lv_obj_set_style_pad_bottom(big_box_, 22, 0);
    lv_obj_set_flex_flow(big_box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(big_box_, 4, 0);
    big_time_ = clock_row(big_box_, font_manrope_600_160, font_manrope_600_40, -8, &big_meridiem_);
    lv_obj_t* row = ui_box(big_box_);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_style_pad_left(row, 4, 0);
    big_date_ = ui_label(row, UI_FONT_BODY, kSoft, "");
    lv_obj_t* dot = ui_box(row);
    lv_obj_set_size(dot, 4, 4);
    lv_obj_set_style_radius(dot, 2, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(UI_TEXT3), 0);
    big_weather_icon_ = ui_label(row, UI_ICON_BUTTON, kSun, "");
    big_weather_ = ui_label(row, UI_FONT_BODY, kSoft, "");

    // waste pickup top right
    waste_ = panel(root_, 204, 40); // .8
    lv_obj_set_size(waste_, LV_SIZE_CONTENT, 80);
    lv_obj_set_style_pad_left(waste_, 12, 0);
    lv_obj_set_style_pad_right(waste_, 22, 0);
    lv_obj_set_flex_flow(waste_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(waste_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(waste_, 14, 0);
    waste_dot_ = ui_box(waste_);
    lv_obj_set_size(waste_dot_, 56, 56);
    lv_obj_set_style_radius(waste_dot_, 28, 0);
    lv_obj_set_style_bg_opa(waste_dot_, LV_OPA_COVER, 0);
    lv_obj_center(ui_label(waste_dot_, UI_ICON_M, UI_BG, LI_TRASH_2));
    lv_obj_t* wtexts = ui_box(waste_);
    lv_obj_set_size(wtexts, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wtexts, LV_FLEX_FLOW_COLUMN);
    waste_title_ = ui_label(wtexts, UI_FONT_NAME, 0xFFFFFF, "");
    waste_hint_ = ui_label(wtexts, UI_FONT_LABEL, kSofter, "");

    // the background picture's card (events view)
    own_card_ = panel(root_, 179, 28); // .7
    lv_obj_set_size(own_card_, 680, LV_SIZE_CONTENT);
    lv_obj_align(own_card_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_pad_ver(own_card_, 22, 0);
    lv_obj_set_style_pad_hor(own_card_, 24, 0);
    lv_obj_set_flex_flow(own_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(own_card_, 12, 0);
    lv_obj_t* wrow = ui_box(own_card_);
    lv_obj_set_size(wrow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wrow, 12, 0);
    own_weather_icon_ = ui_label(wrow, UI_ICON_M, kSun, "");
    own_weather_ = ui_label(wrow, UI_FONT_TITLE, 0xFFFFFF, "");
    own_inside_ = ui_label(wrow, UI_FONT_BODY, kSofter, "");
    own_next_ = ui_label_line(own_card_, UI_FONT_LABEL, kSofter, "");

    // an event's card
    event_card_ = panel(root_, 184, 28); // .72
    lv_obj_set_size(event_card_, 680, LV_SIZE_CONTENT);
    lv_obj_align(event_card_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_pad_top(event_card_, 22, 0);
    lv_obj_set_style_pad_hor(event_card_, 24, 0);
    lv_obj_set_style_pad_bottom(event_card_, 24, 0);
    lv_obj_set_flex_flow(event_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(event_card_, 10, 0);
    lv_obj_t* chip = ui_box(event_card_);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_radius(chip, 20, 0);
    lv_obj_set_style_bg_opa(chip, 41, 0); // .16
    lv_obj_set_style_bg_color(chip, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(chip, 16, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chip, 8, 0);
    ui_label(chip, UI_ICON_S, 0xFFFFFF, LI_CALENDAR);
    ev_chip_ = ui_label(chip, UI_FONT_LABEL, 0xFFFFFF, "");
    ev_title_ = ui_label(event_card_, font_manrope_700_44, 0xFFFFFF, "");
    lv_obj_set_width(ev_title_, LV_PCT(100));
    lv_label_set_long_mode(ev_title_, LV_LABEL_LONG_WRAP);
    lv_obj_t* drow = ui_box(event_card_);
    lv_obj_set_size(drow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(drow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(drow, 18, 0);
    lv_obj_set_style_pad_row(drow, 6, 0);
    icon_text(drow, LI_CLOCK, UI_FONT_BODY, kSoft, &ev_time_);
    icon_text(drow, LI_MAP_PIN, UI_FONT_BODY, kSoft, &ev_place_);
    // one line, cut with "…" (a long place would push the card up the screen)
    lv_label_set_long_mode(ev_place_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(ev_place_, 400, 0);
    lv_obj_set_height(ev_place_, lv_font_get_line_height(UI_FONT_BODY));
    ev_distance_ = ui_label(drow, UI_FONT_BODY, kSofter, "");
    symbol_badge_ = panel(root_, 179, 18);
    lv_obj_set_size(symbol_badge_, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_pad_hor(symbol_badge_, 14, 0);
    lv_obj_center(ui_label(symbol_badge_, UI_FONT_LABEL, kSofter, tr(TR_SCREENSAVER_SYMBOL_PICTURE)));

    // the track that plays, top right below the waste pickup
    music_ = panel(root_, 204, 40); // .8, like the pickup
    lv_obj_add_flag(music_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(music_, LV_SIZE_CONTENT, 80);
    lv_obj_set_style_pad_left(music_, 12, 0);
    lv_obj_set_style_pad_right(music_, 22, 0);
    lv_obj_set_flex_flow(music_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(music_, 14, 0);
    music_cover_ = ui_box(music_);
    lv_obj_set_size(music_cover_, kMusicCover, kMusicCover);
    lv_obj_set_style_radius(music_cover_, 16, 0);
    lv_obj_set_style_bg_opa(music_cover_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(music_cover_, lv_color_hex(UI_SURFACE2), 0);
    lv_obj_center(ui_label(music_cover_, UI_ICON_M, kSun, LI_MUSIC));
    music_image_ = lv_image_create(music_cover_);
    lv_obj_add_flag(music_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(music_image_, 16, 0); // the picture's own corners (no clip_corner layer)
    lv_obj_t* mtexts = ui_box(music_);
    lv_obj_set_size(mtexts, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mtexts, LV_FLEX_FLOW_COLUMN);
    music_title_ = ui_label_line(mtexts, UI_FONT_NAME, 0xFFFFFF, "");
    music_artist_ = ui_label_line(mtexts, UI_FONT_LABEL, kSofter, "");
    for (lv_obj_t* l : {music_title_, music_artist_}) {
        lv_obj_set_width(l, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(l, 240, 0); // clear of the clock top left
    }

    tick_ = lv_timer_create([](lv_timer_t* t) { static_cast<Screensaver*>(lv_timer_get_user_data(t))->on_tick(); },
                            kTickMs, this);
}

// ── state ──

bool Screensaver::event_view() const
{
    return src_.events_enabled && src_.feed && !src_.feed->feed().events.empty();
}

// With events only the events are shown; the picture card (weather, next
// pickup) only when there are none.
int Screensaver::first_card() const { return event_view() ? 1 : 0; }

int Screensaver::card_count() const
{
    return event_view() ? static_cast<int>(src_.feed->feed().events.size()) : 1;
}

void Screensaver::feed_changed()
{
    if (!root_) return;
    // keep the shown event when it is still there, else the first card
    card_ = first_card();
    const auto& events = src_.feed ? src_.feed->feed().events : std::vector<Event>{};
    for (size_t i = 0; event_view() && i < events.size(); ++i) {
        if (events[i].id == card_event_) card_ = static_cast<int>(i) + 1;
    }
    if (card_ == 0) card_event_.clear();
    else if (card_event_.empty()) card_event_ = events[static_cast<size_t>(card_ - 1)].id;
    render();
}

void Screensaver::set_background(const std::string& url)
{
    src_.background = url;
    if (root_ && card_ == 0) render_card();
}

void Screensaver::on_tick()
{
    if (lv_tick_elaps(shown_at_) >= kRevealMs) reveal();
    const time_t now = time(nullptr);
    const struct tm l = local_tm(static_cast<double>(now));
    const std::string digits = app::core::clock_digits(l);
    const char* meridiem = app::core::clock_meridiem(l);
    if (digits != lv_label_get_text(time_) || std::strcmp(meridiem, lv_label_get_text(meridiem_)) != 0) {
        lv_label_set_text(time_, digits.c_str());
        lv_label_set_text(big_time_, digits.c_str());
        lv_label_set_text(meridiem_, meridiem);
        lv_label_set_text(big_meridiem_, meridiem);
        const std::string date = date_text(static_cast<double>(now), true);
        lv_label_set_text(date_, date.c_str());
        lv_label_set_text(big_date_, date.c_str());
        render_card(); // weather / inside temperature, once a minute
    }
    const auto& feed = src_.feed ? src_.feed->feed() : app::eventcalendar::Feed{};
    const uint32_t card_ms = static_cast<uint32_t>(feed.card_seconds) * 1000;
    const uint32_t t = lv_tick_get();
    // card_since_ lies ahead while a swipe pauses the change: no unsigned wrap
    if (card_count() > 1 && t >= card_since_ && t - card_since_ >= card_ms) step(1);
    render_progress();
    render_music();
}

void Screensaver::on_gesture(lv_dir_t dir)
{
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    step(dir == LV_DIR_LEFT ? 1 : -1);
    const auto& feed = src_.feed ? src_.feed->feed() : app::eventcalendar::Feed{};
    paused_until_ = lv_tick_get() + static_cast<uint32_t>(feed.idle_seconds) * 1000; // a manual change pauses
    card_since_ = paused_until_;
}

void Screensaver::step(int delta)
{
    const int n = card_count();
    const int first = first_card();
    card_ = first + ((card_ - first + delta) % n + n) % n;
    const auto& events = src_.feed ? src_.feed->feed().events : std::vector<Event>{};
    card_event_ = card_ > 0 && card_ <= static_cast<int>(events.size()) ? events[card_ - 1].id : std::string();
    card_since_ = lv_tick_get();
    render_card();
}

// ── drawing ──

void Screensaver::render()
{
    if (!root_) return;
    const bool events = event_view();
    // events view: segments, clock top left, cards; picture view: big clock
    for (lv_obj_t* o : {segments_, clock_box_}) {
        if (events) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    if (events) lv_obj_add_flag(big_box_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(big_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(waste_, LV_ALIGN_TOP_RIGHT, -20, events ? 36 : 20);

    lv_obj_clean(segments_);
    for (int i = 0; events && i < card_count(); ++i) {
        lv_obj_t* seg = ui_box(segments_);
        lv_obj_set_height(seg, 5);
        lv_obj_set_flex_grow(seg, 1);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_set_style_bg_opa(seg, 115, 0); // .45
        lv_obj_set_style_bg_color(seg, lv_color_black(), 0);
        lv_obj_set_style_border_width(seg, 1, 0);
        lv_obj_set_style_border_color(seg, lv_color_white(), 0);
        lv_obj_set_style_border_opa(seg, 89, 0); // .35
        // the fill rounded itself: clip_corner would draw the segment through a
        // layer five times a second
        lv_obj_t* fill = ui_box(seg);
        lv_obj_set_size(fill, 0, 5);
        lv_obj_set_style_radius(fill, 3, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(fill, lv_color_white(), 0);
    }
    render_waste();
    music_key_.clear(); // placed anew below the pickup
    lv_label_set_text(time_, ""); // the tick below fills clock, date and weather
    render_card();
    on_tick();
}

void Screensaver::render_progress()
{
    if (!segments_ || lv_obj_has_flag(segments_, LV_OBJ_FLAG_HIDDEN)) return;
    const auto& feed = src_.feed ? src_.feed->feed() : app::eventcalendar::Feed{};
    const uint32_t card_ms = static_cast<uint32_t>(feed.card_seconds) * 1000;
    const uint32_t t = lv_tick_get();
    const uint32_t shown = t > card_since_ ? t - card_since_ : 0;
    const int32_t progress = std::min<int32_t>(100, static_cast<int32_t>(shown * 100 / (card_ms ? card_ms : 1)));
    for (uint32_t i = 0; i < lv_obj_get_child_count(segments_); ++i) {
        lv_obj_t* fill = lv_obj_get_child(lv_obj_get_child(segments_, static_cast<int32_t>(i)), 0);
        const int shown_i = card_ - first_card();
        const int32_t pct = static_cast<int>(i) < shown_i ? 100 : static_cast<int>(i) == shown_i ? progress : 0;
        if (lv_obj_get_style_width(fill, 0) != lv_pct(pct)) lv_obj_set_width(fill, lv_pct(pct));
    }
}

void Screensaver::render_waste()
{
    const auto& waste = src_.events_enabled && src_.feed ? src_.feed->feed().waste
                                                          : std::vector<app::eventcalendar::Pickup>{};
    if (waste.empty()) {
        lv_obj_add_flag(waste_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const auto& w = waste.front();
    std::string day = w.day_label;
    if (!day.empty()) day[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(day[0])));
    lv_label_set_text(waste_title_, (day.empty() ? w.title : w.title + " · " + day).c_str());
    lv_label_set_text(waste_hint_,
                      tr(w.day_label == "Heute" ? TR_SCREENSAVER_WASTE_TODAY : TR_SCREENSAVER_WASTE_TONIGHT));
    lv_obj_set_style_bg_color(waste_dot_, lv_color_hex(waste_color(w.title)), 0);
    lv_obj_remove_flag(waste_, LV_OBJ_FLAG_HIDDEN);
}

void Screensaver::render_music()
{
    const NowPlaying np = src_.now_playing ? src_.now_playing() : NowPlaying{};
    const std::string key = np.playing ? np.title + "\n" + np.artist + "\n" + np.cover_url : std::string();
    if (key == music_key_) return;
    music_key_ = key;
    if (!np.playing) {
        lv_obj_add_flag(music_, LV_OBJ_FLAG_HIDDEN);
        load_cover("");
        return;
    }
    lv_label_set_text(music_title_, np.title.c_str());
    lv_label_set_text(music_artist_, np.artist.c_str());
    if (np.artist.empty()) lv_obj_add_flag(music_artist_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(music_artist_, LV_OBJ_FLAG_HIDDEN);
    const int32_t top = event_view() ? 36 : 20; // as the pickup
    const bool waste = !lv_obj_has_flag(waste_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(music_, LV_ALIGN_TOP_RIGHT, -20, waste ? top + 80 + 12 : top);
    lv_obj_remove_flag(music_, LV_OBJ_FLAG_HIDDEN);
    load_cover(np.cover_url);
}

void Screensaver::load_cover(const std::string& url)
{
    if (url == cover_url_) return;
    cover_url_ = url;
    cover_load_.cancel();
    lv_obj_add_flag(music_image_, LV_OBJ_FLAG_HIDDEN); // the icon until the new one is there
    lv_image_set_src(music_image_, nullptr);
    if (cover_) lv_image_cache_drop(&cover_dsc_);
    cover_.reset();
    app::media::ImageLoader* loader = app::media::image_loader();
    if (url.empty() || !loader) return;
    cover_load_ = app::core::CancellationSource();
    app::media::ImageLoader::Options o;
    o.cache = true;
    loader->load(
        url, kMusicCover, kMusicCover, true, cover_load_.token(),
        [this, url](std::shared_ptr<app::media::DecodedImage> picture, const std::string&) {
            if (!music_image_ || url != cover_url_ || !picture) return;
            cover_ = std::move(picture);
            app::media::set_image_dsc(cover_dsc_, *cover_);
            lv_image_set_src(music_image_, &cover_dsc_);
            lv_obj_center(music_image_);
            lv_obj_remove_flag(music_image_, LV_OBJ_FLAG_HIDDEN);
        },
        o);
}

void Screensaver::render_card()
{
    if (!root_) return;
    const bool events = event_view();
    const auto& list = src_.feed ? src_.feed->feed().events : std::vector<Event>{};
    const Event* ev = events && card_ > 0 && card_ <= static_cast<int>(list.size()) ? &list[card_ - 1] : nullptr;

    // weather: the background card (events view) and the big clock (picture view)
    const Weather w = src_.weather ? src_.weather() : Weather{};
    const std::string temp = w.known ? format_number(w.temperature, 1) + "°" : std::string();
    const char* icon = w.known ? weather_icon(w.condition) : "";
    lv_label_set_text(big_weather_icon_, icon);
    lv_label_set_text(big_weather_, temp.c_str());
    lv_label_set_text(own_weather_icon_, icon);
    const std::string weather = w.known ? temp + " · " + weather_text_de(w.condition) : std::string();
    lv_label_set_text(own_weather_, weather.c_str());
    const float inside = src_.room_temperature ? src_.room_temperature() : NAN;
    lv_label_set_text(own_inside_,
                      std::isnan(inside) ? "" : trf(TR_SCREENSAVER_INSIDE, {format_number(inside, 1) + "°"}).c_str());
    if (!list.empty()) {
        const Event& next = list.front();
        std::string teaser =
            next.title + " · " + tr(static_cast<tr_key_t>(TR_DAY_ABBR_0 + local_tm(next.start).tm_wday % 7));
        if (next.distance_km) teaser += " · " + format_number(*next.distance_km, 1) + " km";
        lv_label_set_text(own_next_, trf(TR_SCREENSAVER_NEXT, {teaser}).c_str());
    }

    if (events && !ev) lv_obj_remove_flag(own_card_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(own_card_, LV_OBJ_FLAG_HIDDEN);
    if (ev) {
        lv_obj_remove_flag(event_card_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ev_chip_, chip_text(*ev).c_str());
        lv_label_set_text(ev_title_, ev->title.c_str());
        lv_label_set_text(ev_time_, time_text(*ev).c_str());
        lv_label_set_text(ev_place_, ev->location.c_str());
        lv_label_set_text(ev_distance_, distance_text(*ev).c_str());
        if (ev->image_kind == "symbol") {
            lv_obj_update_layout(event_card_);
            lv_obj_align_to(symbol_badge_, event_card_, LV_ALIGN_OUT_TOP_RIGHT, 0, -10);
            lv_obj_remove_flag(symbol_badge_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(symbol_badge_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(event_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(symbol_badge_, LV_OBJ_FLAG_HIDDEN);
    }

    // the picture: the event's, else the background
    const std::string entity = ev && src_.feed ? src_.feed->image_entity(ev->image_key) : std::string();
    load_picture(ev ? (entity.empty() ? std::string() : event_picture_url(entity)) : src_.background);
}

void Screensaver::load_picture(const std::string& url)
{
    if (url == picture_url_) return;
    picture_url_ = url;
    pending_.cancel();
    if (url.empty()) { // no picture (yet): the dark ground
        lv_image_set_src(image_, nullptr);
        return;
    }
    if (cached_ && cache_key(url) == cached_key_) {
        show_picture(cached_);
        return;
    }
    app::media::ImageLoader* loader = app::media::image_loader();
    if (!loader) {
        reveal();
        return;
    }
    pending_ = app::core::CancellationSource();
    loader->load(url, 720, 720, true, pending_.token(),
                 [this, url](std::shared_ptr<app::media::DecodedImage> picture, const std::string& error) {
                     if (!image_ || url != picture_url_) return;
                     if (!picture) {
                         // an older add-on without the sized pictures: its image entity
                         const std::string sized = "/api/eventcalendar/images/";
                         if (url.rfind(sized, 0) == 0) {
                             const std::string entity = url.substr(sized.size(), url.find('?') - sized.size());
                             picture_url_.clear();
                             load_picture("/api/image_proxy/" + entity);
                             return;
                         }
                         LV_LOG_WARN("screensaver picture: %s", error.c_str());
                         reveal(); // without it rather than not at all
                         return;
                     }
                     if (url == src_.background) {
                         cached_ = picture;
                         cached_key_ = cache_key(url);
                     }
                     show_picture(std::move(picture));
                 });
}

void Screensaver::show_picture(std::shared_ptr<app::media::DecodedImage> picture)
{
    lv_image_cache_drop(&dsc_);
    picture_ = std::move(picture);
    app::media::set_image_dsc(dsc_, *picture_);
    lv_image_set_src(image_, &dsc_);
    lv_obj_center(image_);
    reveal();
}

// The screensaver appears with its picture, not before (at most
// kRevealMs later than asked).
void Screensaver::reveal()
{
    if (root_ && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace app::ui
