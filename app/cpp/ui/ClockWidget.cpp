#include "ClockWidget.h"

#include "lvgl/lvgl.h"
#include "../core/DateText.h"

#include "../../fonts.h"
#include "../../ui_kit.h"

#include <cstdio>
#include <cstring>

#include <ctime>

namespace app::ui {

namespace {
void raw_timer_cb(lv_timer_t* timer) { ClockWidget::update_trampoline(timer); }
} // namespace

void ClockWidget::create(lv_obj_t* parent) {
    // Clock block of the design guide (Main artboard): 96 px time, date
    // below in text 2. In a single tile the time uses a smaller size so
    // "23:59" fits.
    lv_obj_t* wrapper = ui_box(parent);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(wrapper, 30, 0); // the digits as far from the edge as the other blocks' content
    lv_obj_set_style_pad_left(wrapper, 20, 0);
    lv_obj_set_style_pad_right(wrapper, 16, 0);
    lv_obj_set_style_pad_bottom(wrapper, 16, 0);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wrapper, 8, 0);

    lv_obj_update_layout(parent); // its width is only known after the layout ran
    const bool narrow = lv_obj_get_width(parent) < 260;
    // Big while the clock has the block to itself (no timers / alarms shown yet).
    // the digits with a small AM/PM beside them (12-hour format only)
    lv_obj_t* row = ui_box(wrapper);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 8, 0);
    digits_font_ = narrow ? font_manrope_600_72 : font_manrope_600_96;
    smaller_font_ = narrow ? font_manrope_600_56 : font_manrope_600_72; // "12:59 PM" in the width
    width_ = lv_obj_get_content_width(parent) - 36;
    time_label_ = ui_label(row, digits_font_, UI_TEXT, "");
    lv_obj_set_style_text_letter_space(time_label_, narrow ? -5 : -4, 0); // "23:59" fits
    meridiem_label_ = ui_label(row, narrow ? font_manrope_600_26 : font_manrope_600_32, UI_TEXT, "");
    lv_obj_set_style_pad_bottom(meridiem_label_, lv_font_get_line_height(digits_font_) / 6, 0);
    date_label_ = ui_label(wrapper, UI_FONT_BODY, UI_TEXT2, "");
    lv_obj_set_style_pad_top(date_label_, narrow ? 0 : 6, 0);
    lv_obj_set_width(date_label_, LV_PCT(100));
    lv_label_set_long_mode(date_label_, LV_LABEL_LONG_WRAP);

    update();
    timer_ = lv_timer_create(raw_timer_cb, 1000, this);

    track_root(wrapper);
}

void ClockWidget::shutdown_ui() {
    // Explicit teardown in the UI context (ARCHITECTURE.md §8) — do not rely on the
    // destructor to reach this; LVGL timers are not tied to LV_EVENT_DELETE
    // of any object, so track_root()'s automatic root_ nulling does not
    // clean this up on its own.
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
}

void ClockWidget::update_trampoline(void* timer) {
    auto* t = reinterpret_cast<lv_timer_t*>(timer);
    auto* self = reinterpret_cast<ClockWidget*>(lv_timer_get_user_data(t));
    if (self) self->update();
}

void ClockWidget::update() {
    const struct tm local_time = app::core::local_tm(static_cast<double>(time(nullptr)));
    // "Mi. 7. Okt." / "Wed, Oct 7": language and date format (settings)
    const std::string date = app::core::date_text(local_time, app::core::DateStyle::weekday_day_month);
    const std::string digits = app::core::clock_digits(local_time);
    const char* meridiem = app::core::clock_meridiem(local_time);
    if (time_label_ && meridiem_label_) {
        // with AM/PM the digits get smaller when both do not fit
        const lv_font_t* font = digits_font_;
        if (meridiem[0]) {
            lv_point_t d, m;
            lv_text_get_size(&d, digits.c_str(), digits_font_, lv_obj_get_style_text_letter_space(time_label_, 0), 0,
                             LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_text_get_size(&m, meridiem, lv_obj_get_style_text_font(meridiem_label_, 0), 0, 0, LV_COORD_MAX,
                             LV_TEXT_FLAG_NONE);
            if (d.x + 8 + m.x > width_) font = smaller_font_;
        }
        // only on a change: setting the same text redraws it anyway (and
        // would keep the dashboard's swipe pictures from ever being current)
        if (lv_obj_get_style_text_font(time_label_, 0) != font) lv_obj_set_style_text_font(time_label_, font, 0);
        if (digits != lv_label_get_text(time_label_)) lv_label_set_text(time_label_, digits.c_str());
        if (std::strcmp(meridiem, lv_label_get_text(meridiem_label_)) != 0) lv_label_set_text(meridiem_label_, meridiem);
    }
    if (date_label_ && date != lv_label_get_text(date_label_)) lv_label_set_text(date_label_, date.c_str());
}

} // namespace app::ui
