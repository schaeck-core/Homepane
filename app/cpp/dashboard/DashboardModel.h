#ifndef APP_CPP_DASHBOARD_DASHBOARDMODEL_H
#define APP_CPP_DASHBOARD_DASHBOARDMODEL_H

#include <string>
#include <vector>

namespace app::dashboard {

// Owned, LVGL-free representation of a parsed Lovelace "sections"
// dashboard (CPP_MIGRATION_PLAN.md Phase 6). `raw_json` on each card
// keeps the full, owned card object so widget-specific fields (chart
// options, tile features, ...) don't need a hand-modeled struct per
// card type up front — matches "unbekannte optionale Felder sind
// erlaubt" (§8.2).
struct DashboardCard {
    std::string type;         // e.g. "clock", "light", "tile", "statistics-graph"
    std::string entity_id;    // convenience extraction of a top-level "entity" field, if present
    std::string raw_json;     // the full, owned card object
    int grid_columns = 0;     // 0 = not specified
    int grid_rows = 0;
    std::string section_title; // the title of the section the card is in ("" if none)

    // Preserves the existing dashboard.c semantics (§2.2): a
    // statistics-graph card is rendered as a background/floating element
    // during the first layout pass, not as a normal grid item. Computed
    // once here so downstream layout code never re-derives it from the
    // type string in multiple places.
    bool is_background_graph = false;
};

struct DashboardSection {
    std::string type; // "grid"
    std::string title;
    int column_span = 1;
    std::vector<DashboardCard> cards;
};

struct DashboardView {
    std::string title;
    std::string type; // "sections" (current format) or absent/other for older masonry views
    int max_columns = 0;
    bool subview = false;
    std::vector<DashboardSection> sections; // "sections"-style views
    std::vector<DashboardCard> cards;       // flat cards, for non-sections (masonry) views
    std::string rows;                       // "homepane": {"rows": ...} of the view, "" if none
};

// The dashboard's own settings for the panel, a "homepane" object in the
// Lovelace config (HA keeps unknown keys): {"homepane": {"rows": "3"}} on
// the dashboard, or on a view for that view. They win over the panel's
// settings, so a dashboard made for the panel shows as intended.
struct DashboardModel {
    std::vector<DashboardView> views;
    std::string rows; // tiles on one screen, as config key dashboard_rows; "" if none
};

} // namespace app::dashboard

#endif // APP_CPP_DASHBOARD_DASHBOARDMODEL_H
