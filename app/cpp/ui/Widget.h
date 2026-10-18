#ifndef APP_CPP_UI_WIDGET_H
#define APP_CPP_UI_WIDGET_H

#include "../home_assistant/EntityStore.h"
#include "../home_assistant/QueryTypes.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// Forward declaration matching lvgl/src/misc/lv_types.h's own
// `typedef struct _lv_obj_t lv_obj_t;` — keeps this header LVGL-free so
// pure logic (WidgetFactory lookup, tests) doesn't need to compile
// against LVGL at all. Concrete widgets' .cpp files include the real
// lvgl.h (C++ migration phase 6).
struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace app::ui {

// docs/ARCHITECTURE.md §5.1
struct EntityRequirement {
    app::home_assistant::HaInstanceId source;
    std::string entity_id;
};

// Phase 9: QueryCoordinator's two data shapes.
struct ForecastRequirement {
    app::home_assistant::HaInstanceId source;
    std::string entity_id;
    std::string forecast_type; // "daily" | "hourly" | "twice_daily"
};

struct StatisticsRequirement {
    app::home_assistant::HaInstanceId source;
    app::home_assistant::StatisticsQuery query;
    uint32_t refresh_interval_ms = 60000;
};

using DataRequirement = std::variant<EntityRequirement, ForecastRequirement, StatisticsRequirement>;

struct DataUpdate {
    size_t requirement_index = 0; // which requirements()[i] this update is for
    // Exactly one of these is non-null, matching which requirements()[i]
    // alternative this update corresponds to.
    const app::home_assistant::EntityState* entity_state = nullptr;
    const app::home_assistant::ForecastState* forecast_state = nullptr;
    const app::home_assistant::StatisticsResult* statistics_result = nullptr;
};

// Base class for every dashboard card widget. Construction takes only
// configuration (no network calls, no LVGL side effects — ARCHITECTURE.md §7 item 4);
// create() is where the LVGL tree is actually built.
class Widget {
public:
    virtual ~Widget() = default;

    virtual std::vector<DataRequirement> requirements() const { return {}; }

    // Builds this widget's LVGL tree under `parent`. Called only in the
    // UI context.
    virtual void create(lv_obj_t* parent) = 0;

    // Called only in the UI context when a subscribed requirement's data
    // changes.
    virtual void on_data(const DataUpdate&) {}

    // Explicit UI teardown (timers, LVGL objects), called in the UI
    // context during session shutdown, before the Widget itself is
    // destroyed. Destructors must not assume they still run in a safe
    // context to touch LVGL (ARCHITECTURE.md §8) — this is that safe place.
    virtual void shutdown_ui() {}

    lv_obj_t* root() const { return root_; }

    // Public only so the free function in Widget.cpp (matching LVGL's
    // exact lv_event_cb_t signature) can forward to it. Not part of the
    // widget API — do not call this directly.
    static void on_root_deleted_trampoline(void* event);

protected:
    // Concrete widgets call this once, right after creating their root
    // LVGL object, so the base class can null root_ automatically when
    // LVGL deletes it (ARCHITECTURE.md §3 ownership table: "Pointer nach LV_EVENT_DELETE
    // nullen").
    void track_root(lv_obj_t* root);

private:
    lv_obj_t* root_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_WIDGET_H
