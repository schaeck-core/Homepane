#ifndef APP_CPP_DASHBOARD_DASHBOARDLAYOUT_H
#define APP_CPP_DASHBOARD_DASHBOARDLAYOUT_H

#include "DashboardModel.h"

#include <cstdint>
#include <vector>

namespace app::dashboard {

// Pure geometry for the real Lovelace "sections" grid layout, ported
// from the legacy app/views/dashboard.c's unit-based grid (each section
// column is subdivided into 12 "grid units", matching Home Assistant's
// own section grid) — so a later live cutover swaps the widget rendering
// underneath an already-matching visual layout instead of also having to
// fix layout at the same time. LVGL-free and host-testable; the only
// thing DashboardSession.cpp adds on top is the actual lv_obj_t tree.

// Pixel width of one grid unit (1/12 of one section "column") for a view
// with `max_columns` columns (<= 0 falls back to 3, matching
// dashboard.c's default) laid out across `container_width_px`. Never
// returns <= 0 (clamped to 1) so callers can multiply/divide freely.
int32_t compute_unit_size_x(int32_t container_width_px, int max_columns);

struct SectionGeometry {
    int32_t pixel_width = 0;
    int32_t max_units = 12; // column_span * 12, column_span clamped to >= 1
};
SectionGeometry compute_section_geometry(int32_t unit_size_x, int column_span);

// One card's placement inside its section's LVGL grid, in grid-unit
// columns and row counts (both already clamped/wrapped).
struct CardPlacement {
    int32_t col_start = 0;
    int32_t col_span = 12;
    int32_t row_start = 0;
    int32_t row_span = 1;
};

// Places `cards` left-to-right within `max_units` grid-unit columns,
// wrapping to the next row on overflow — the same algorithm as
// dashboard.c. `cards` must already exclude background-graph cards: in
// both implementations those never consume grid space (rendered as a
// floating layer instead, once a real graph widget exists in Phase 9).
std::vector<CardPlacement> place_cards(const std::vector<DashboardCard>& cards, int32_t max_units);

// Highest (row_start + row_span) across `placements`, or 1 if empty —
// how many grid rows the section's LVGL grid descriptor needs.
int32_t required_rows(const std::vector<CardPlacement>& placements);

// ── Tile grid of the design guide (docs/design/DESIGN_GUIDE.md) ──
// The view's sections in Home Assistant's own arrangement: a grid of
// `max_columns` columns (HA's default 4) that the sections fill row by row,
// each `column_span` columns wide; a row of sections is as high as its
// highest section. Inside a section the cards are packed densely, like HA
// does (grid-auto-flow: row dense).
//
// Sizes are coarser than HA's: a big card is one tile (a whole number of
// columns wide, one tile high); a small card (HA's tile/entity card, at
// most 2 HA rows high) is a compact tile, half a column wide and a third
// of a tile high. Positions are in half columns (x, w) and thirds of a
// tile (y, h): a tile is kSlotsPerTile slots high.
constexpr int kSlotsPerTile = 3;

enum class CellKind {
    Tile,    // big card: one tile high
    Compact, // small card: one slot high
    Title,   // section title / heading card: one slot high
};

struct GridCell {
    const DashboardCard* card = nullptr; // null for a section title
    std::string title;                   // Title cells
    CellKind kind = CellKind::Tile;
    int x = 0, y = 0; // half columns / slots from the row's top left
    int w = 2, h = kSlotsPerTile;
};

struct GridRow {
    std::vector<GridCell> cells;
    int height = 0; // slots
};

// Columns of the view (max_columns, HA's default 4 when not set).
int view_columns(const DashboardView& view);
// A card shown as a compact tile.
bool is_compact(const DashboardCard& card);
// Width in half columns of a card in a section `section_halves` wide.
int card_halves(const DashboardCard& card, int section_halves);
std::vector<GridRow> layout_grid(const DashboardView& view);

// Tiles on one screen, top to bottom (config key dashboard_rows): "1" to
// "6", or relative to the view's columns ("cols", "cols-1") so tiles keep
// their shape in views with more columns. Default 2.
int rows_on_screen(const std::string& setting, int columns);

} // namespace app::dashboard

#endif // APP_CPP_DASHBOARD_DASHBOARDLAYOUT_H
