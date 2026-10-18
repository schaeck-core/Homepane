// Pure-logic unit test for the Lovelace "sections" grid geometry
// (C++ migration phase 6/8) — no LVGL, no network. Verifies the
// arithmetic ported from app/views/dashboard.c's unit-based grid
// (compute_unit_size_x/compute_section_geometry) and the card
// left-to-right-then-wrap placement algorithm (place_cards/required_rows)
// that DashboardSession.cpp turns into a real lv_obj_t grid.

#include "app/cpp/dashboard/DashboardLayout.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace app::dashboard;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

DashboardCard make_card(int grid_columns = 0, int grid_rows = 0) {
    DashboardCard card;
    card.type = "tile";
    card.grid_columns = grid_columns;
    card.grid_rows = grid_rows;
    return card;
}

void test_compute_unit_size_x() {
    expect(compute_unit_size_x(1080, 3) == 30, "3 columns over 1080px: 1080 / (3*12) = 30px per unit");
    expect(compute_unit_size_x(1080, 0) == 30, "max_columns <= 0 falls back to 3, matching dashboard.c's default");
    expect(compute_unit_size_x(1080, -1) == 30, "negative max_columns also falls back to 3");
    expect(compute_unit_size_x(1080, 2) == 45, "2 columns over 1080px: 1080 / (2*12) = 45px per unit");
    expect(compute_unit_size_x(10, 3) == 1, "a container far too small to fit one unit clamps to 1px, never 0");
}

void test_compute_section_geometry() {
    SectionGeometry single = compute_section_geometry(30, 1);
    expect(single.max_units == 12, "column_span=1 spans 12 grid units");
    expect(single.pixel_width == 360, "column_span=1 at 30px/unit is 360px wide");

    SectionGeometry doubled = compute_section_geometry(30, 2);
    expect(doubled.max_units == 24, "column_span=2 spans 24 grid units");
    expect(doubled.pixel_width == 720, "column_span=2 at 30px/unit is 720px wide");

    SectionGeometry invalid = compute_section_geometry(30, 0);
    expect(invalid.max_units == 12, "column_span <= 0 clamps to 1 (12 units), never a degenerate 0-wide section");
}

void test_place_cards_full_width_cards_stack_one_per_row() {
    std::vector<DashboardCard> cards = {make_card(), make_card(), make_card()};
    auto placements = place_cards(cards, 12);

    expect(placements.size() == 3, "one placement per card");
    expect(placements[0].col_start == 0 && placements[0].row_start == 0,
           "first full-width card (no grid_options.columns) occupies row 0");
    expect(placements[0].col_span == 12, "a card without grid_options.columns defaults to the full section width");
    expect(placements[1].row_start == 1, "second full-width card wraps to row 1");
    expect(placements[2].row_start == 2, "third full-width card wraps to row 2");
}

void test_place_cards_half_width_cards_share_a_row() {
    std::vector<DashboardCard> cards = {make_card(6), make_card(6), make_card(6)};
    auto placements = place_cards(cards, 12);

    expect(placements[0].col_start == 0 && placements[0].row_start == 0, "first half-width card at column 0, row 0");
    expect(placements[1].col_start == 6 && placements[1].row_start == 0,
           "second half-width card shares row 0, starting at column 6 (12/2)");
    expect(placements[2].col_start == 0 && placements[2].row_start == 1,
           "third half-width card no longer fits in row 0 (6+6+6 > 12) and wraps to row 1");
}

void test_place_cards_clamps_columns_wider_than_the_section() {
    std::vector<DashboardCard> cards = {make_card(20)};
    auto placements = place_cards(cards, 12);
    expect(placements[0].col_span == 12, "grid_options.columns wider than the section clamps to max_units");
}

void test_place_cards_row_span_is_carried_through() {
    std::vector<DashboardCard> cards = {make_card(12, 2), make_card(12, 1)};
    auto placements = place_cards(cards, 12);
    expect(placements[0].row_span == 2, "a card's grid_options.rows becomes its placement's row_span");
    expect(placements[1].row_start == 1,
           "the next card still wraps by column overflow only, one row below the first (dashboard.c does not "
           "reflow around a taller card's row_span — matched here on purpose, not fixed)");
}

void test_required_rows() {
    expect(required_rows({}) == 1, "an empty section still reserves at least 1 row");

    std::vector<DashboardCard> three_full_width = {make_card(), make_card(), make_card()};
    expect(required_rows(place_cards(three_full_width, 12)) == 3, "three stacked full-width cards need 3 rows");

    std::vector<DashboardCard> tall_last_card = {make_card(6), make_card(6), make_card(12, 2)};
    auto placements = place_cards(tall_last_card, 12);
    // card 0: col 0 row 0; card 1: col 6 row 0; card 2 (full width, row_span 2):
    // doesn't fit next to card 1 (6+12>12) so wraps to row 1, spanning rows 1-2.
    expect(required_rows(placements) == 3, "a row_span=2 card starting at row 1 pushes required_rows to 3");
}

} // namespace

DashboardCard entity_card(const std::string& type, int grid_columns = 0, int grid_rows = 0) {
    DashboardCard card;
    card.type = type;
    card.entity_id = type == "clock" ? "" : "sensor.x";
    card.grid_columns = grid_columns;
    card.grid_rows = grid_rows;
    return card;
}

DashboardSection section(std::vector<DashboardCard> cards, int span = 1) {
    DashboardSection s;
    s.column_span = span;
    s.cards = std::move(cards);
    return s;
}

void test_grid_layout() {
    // The user's dashboard "xxTestxx" (view "Home", max_columns 3).
    DashboardView view;
    view.max_columns = 3;
    view.sections = {
        section({entity_card("clock", 15, 4), entity_card("weather-forecast", 21, 4)}, 3),
        section({entity_card("light", 12, 4)}),
        section({entity_card("thermostat")}),
        section({entity_card("tile")}),
        section({entity_card("tile", 12, 4)}),
        section({entity_card("tile"), entity_card("tile"), entity_card("tile"), entity_card("tile"),
                 entity_card("tile"), entity_card("tile")}),
        section({entity_card("statistics-graph"), entity_card("tile")}, 1),
    };
    std::vector<GridRow> rows = layout_grid(view);
    expect(rows.size() == 3, "sections fill rows of three columns");

    const GridRow& top = rows[0];
    expect(top.cells.size() == 2 && top.cells[0].w == 2 && top.cells[1].x == 2 && top.cells[1].w == 4,
           "clock (15 of 36) one column, weather (21 of 36) two");

    const GridRow& second = rows[1];
    expect(second.cells.size() == 3 && second.cells[2].kind == CellKind::Compact && second.cells[2].x == 4 &&
               second.cells[2].w == 1,
           "light, thermostat, then a small tile half a column wide in the third column");
    expect(second.height == kSlotsPerTile, "the row is as high as its biggest card");

    const GridRow& third = rows[2];
    int compact = 0;
    for (const auto& c : third.cells) compact += c.kind == CellKind::Compact;
    expect(third.cells[0].kind == CellKind::Tile && third.cells[0].w == 2,
           "a tile card with rows 4 is a big tile");
    expect(compact == 7, "six small tasmota tiles plus one under the chart");
    expect(third.cells[1].x == 2 && third.cells[2].x == 3 && third.cells[3].y == 1,
           "small tiles pair up, two per column, and wrap");
    expect(third.height == kSlotsPerTile + 1, "the chart (one tile) and its small tile below it");

    DashboardView four;
    four.max_columns = 0;
    expect(view_columns(four) == 4, "no max_columns: HA's default of 4");

    DashboardCard heading;
    heading.type = "heading";
    heading.raw_json = R"({"type":"heading","heading":"Neuer Abschnitt"})";
    DashboardView titled;
    titled.max_columns = 2;
    titled.sections = {section({heading, entity_card("tile"), entity_card("tile")})};
    rows = layout_grid(titled);
    expect(rows.size() == 1 && rows[0].cells[0].kind == CellKind::Title && rows[0].cells[0].w == 2 &&
               rows[0].cells[1].y == 1 && rows[0].cells[2].x == 1,
           "a heading spans its section, the tiles follow below it");
}

void test_rows_on_screen() {
    expect(rows_on_screen("", 3) == 2, "default: two rows");
    expect(rows_on_screen("3", 4) == 3, "a fixed number");
    expect(rows_on_screen("cols-1", 3) == 2 && rows_on_screen("cols-1", 4) == 3, "columns - 1");
    expect(rows_on_screen("cols", 4) == 4 && rows_on_screen("cols-1", 1) == 1, "like columns, never below one");
}

int main() {
    test_grid_layout();
    test_rows_on_screen();
    test_compute_unit_size_x();
    test_compute_section_geometry();
    test_place_cards_full_width_cards_stack_one_per_row();
    test_place_cards_half_width_cards_share_a_row();
    test_place_cards_clamps_columns_wider_than_the_section();
    test_place_cards_row_span_is_carried_through();
    test_required_rows();

    if (g_failures == 0) {
        std::printf("All DashboardLayout unit tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
