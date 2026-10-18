#ifndef APP_CPP_DASHBOARD_DASHBOARDSESSION_H
#define APP_CPP_DASHBOARD_DASHBOARDSESSION_H

#include "../core/DashboardGeneration.h"
#include "../core/ILogger.h"
#include "../home_assistant/EntitySubscriptionCoordinator.h"
#include "../home_assistant/QueryCoordinator.h"
#include "../ui/Widget.h"
#include "DashboardModel.h"
#include "WidgetFactory.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace app::dashboard {

// Owns one loaded dashboard's widget instances (docs/ARCHITECTURE.md
// §2/§3/§7). A dashboard switch means: shutdown() the old session in
// the UI context (widgets' shutdown_ui(), then the LVGL subtree, then
// the C++ objects — ARCHITECTURE.md §3 Shutdown-Reihenfolge), then construct+create() a
// new one with a higher DashboardGeneration.
class DashboardSession {
public:
    // `coordinator`/`query_coordinator` are each optional: sessions built
    // purely from widget types that don't need them (Clock, Unsupported)
    // can pass nullptr. Any widget whose requirements() returns a kind
    // whose coordinator is null simply never receives on_data() for that
    // requirement — a degenerate but crash-free case, not expected in
    // real use once a real composition root always provides both.
    DashboardSession(DashboardModel model, const WidgetFactory& factory,
                      app::core::DashboardGeneration generation,
                      app::home_assistant::EntitySubscriptionCoordinator* coordinator = nullptr,
                      app::home_assistant::QueryCoordinator* query_coordinator = nullptr,
                      app::core::ILogger* logger = nullptr);
    ~DashboardSession();

    DashboardSession(const DashboardSession&) = delete;
    DashboardSession& operator=(const DashboardSession&) = delete;

    // Tiles on one screen (config key dashboard_rows, see
    // DashboardLayout.h rows_on_screen()); applies to the next create().
    static void set_rows_setting(const std::string& setting);

    // Builds the LVGL tree for the first view's cards under `parent`.
    // UI context only; call exactly once.
    void create(lv_obj_t* parent);

    // Ordered teardown; UI context only. Idempotent — safe to call more
    // than once, and safe to skip only if create() was never called.
    void shutdown();

    app::core::DashboardGeneration generation() const { return generation_; }
    size_t widget_count() const { return widgets_.size(); }
    bool is_shut_down() const { return shut_down_; }

    // Views (swiped horizontally): count, title, the one on screen.
    int view_count() const { return static_cast<int>(model_.views.size()); }
    const std::string& view_title(int index) const { return model_.views[static_cast<size_t>(index)].title; }
    int current_view() const;
    void show_view(int index, bool animate = true);
    // the first view, every view scrolled to its top (the screensaver's reset)
    void show_home();
    // the shown view is scrolled to its top (the music player's pull-down)
    bool at_top() const;

private:
    void wire_requirements(app::ui::Widget& widget);

    // A section's LVGL grid descriptor arrays: lv_obj_set_grid_dsc_array()
    // stores the raw pointers, not a copy (docs/ARCHITECTURE.md §3 —
    // matches dashboard.c's malloc'd descriptors kept alive via a
    // delete-callback), so these must outlive the section_content object,
    // i.e. the whole session. Reserved up front in create() so the outer
    // vector never reallocates mid-build and never invalidates a pointer
    // already handed to LVGL.

    // Every requirement kind's handle is move-only and shaped differently
    // (EntityRequirement, ForecastRequirement, StatisticsRequirement —
    // Widget.h), so subscriptions_ holds a tagged union of all three
    // rather than needing a common base/type-erasure just to sit in one
    // vector for uniform teardown.
    using SubscriptionVariant = std::variant<app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle,
                                              app::home_assistant::QueryCoordinator::ForecastHandle,
                                              app::home_assistant::QueryCoordinator::StatisticsHandle>;

    DashboardModel model_;

public:
    const DashboardModel& model() const { return model_; }

private:
    const WidgetFactory& factory_;
    app::core::DashboardGeneration generation_;
    app::home_assistant::EntitySubscriptionCoordinator* coordinator_;
    app::home_assistant::QueryCoordinator* query_coordinator_;
    app::core::ILogger* logger_;

    std::vector<std::unique_ptr<app::ui::Widget>> widgets_;
    std::vector<SubscriptionVariant> subscriptions_;
    lv_obj_t* root_ = nullptr;
    bool shut_down_ = false;

    // Swiping between views moves still pictures of the pages instead of
    // redrawing every tile in every frame (DashboardSession.cpp).
    struct SwipeShots;
    std::unique_ptr<SwipeShots> swipe_;
};

} // namespace app::dashboard

#endif // APP_CPP_DASHBOARD_DASHBOARDSESSION_H
