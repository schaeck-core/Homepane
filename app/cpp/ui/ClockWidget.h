#ifndef APP_CPP_UI_CLOCKWIDGET_H
#define APP_CPP_UI_CLOCKWIDGET_H

#include "Widget.h"

// Forward declaration matching lvgl/src/others/timer/lv_timer.h's
// `typedef struct _lv_timer_t lv_timer_t;` (same reasoning as Widget.h's
// lv_obj_t forward declaration: keep the header LVGL-free).
struct _lv_timer_t;
using lv_timer_t = _lv_timer_t;
struct _lv_font_t;
using lv_font_t = _lv_font_t;

namespace app::ui {

// First real, non-entity widget (C++ migration phase 6 ARCHITECTURE.md §5.3:
// "Clock: keine HA-Subscription; ... lokaler UI-Timer mit RAII"). Proves
// the Widget/Factory/Session pattern end-to-end before Phase 7 adds the
// first entity-bound widget.
class ClockWidget final : public Widget {
public:
    void create(lv_obj_t* parent) override;
    void shutdown_ui() override;

    // Public only so the free function in ClockWidget.cpp (matching
    // LVGL's exact lv_timer_cb_t signature) can forward to it.
    static void update_trampoline(void* timer);

private:
    void update();

    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* meridiem_label_ = nullptr; // AM/PM (12-hour format)
    lv_obj_t* date_label_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    const lv_font_t* digits_font_ = nullptr;
    const lv_font_t* smaller_font_ = nullptr;
    int32_t width_ = 0; // for the time row
};

} // namespace app::ui

#endif // APP_CPP_UI_CLOCKWIDGET_H
