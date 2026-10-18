// Integration test for Widget/WidgetFactory/DashboardSession against a
// REAL (headless) LVGL instance — no SDL, no display hardware, no
// network. This is the one test target in this project that links LVGL
// itself, to prove object creation/teardown actually works, not just
// that the C++ scaffolding compiles (CPP_MIGRATION_PLAN.md Phase 6 DoD).

#include "app/i18n.h"
#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/dashboard/DashboardParser.h"
#include "app/cpp/dashboard/DashboardSession.h"
#include "app/cpp/ui/ClockWidget.h"
#include "dashboard_test_helpers.h"

#include <cjson/cJSON.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace app::dashboard;
using namespace app::ui;

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

std::string load_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string extract_result_object(const std::string& full_message_json) {
    cJSON* root = cJSON_Parse(full_message_json.c_str());
    if (!root) return {};
    std::string out;
    cJSON* result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (result) {
        char* txt = cJSON_PrintUnformatted(result);
        if (txt) {
            out = txt;
            cJSON_free(txt);
        }
    }
    cJSON_Delete(root);
    return out;
}

// No-op flush: this display is never actually rendered to anything, it
// only exists so lv_obj_create() has a valid screen to attach to.
void noop_flush_cb(lv_display_t* disp, const lv_area_t*, uint8_t*) { lv_display_flush_ready(disp); }

lv_obj_t* make_headless_screen() {
    lv_display_t* disp = lv_display_create(320, 240);
    static uint8_t buf[320 * 10 * 4]; // small partial buffer, never actually read
    lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, noop_flush_cb);
    return lv_display_get_screen_active(disp);
}

WidgetFactory make_factory_with_clock_registered() {
    WidgetFactory factory;
    factory.register_type("clock", [](const DashboardCard&) { return std::make_unique<ClockWidget>(); });
    return factory;
}

void test_clock_and_unsupported_widgets_create_real_lvgl_objects() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory = make_factory_with_clock_registered();

    DashboardModel model;
    DashboardParser::ParseError err;
    bool ok = DashboardParser::parse(
        R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
        R"({"type":"clock"},{"type":"light","entity":"light.x"})"
        R"(]}]}]})",
        model, err);
    expect(ok, "synthetic two-card dashboard parses");

    DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(1));
    session.create(screen);

    expect(session.widget_count() == 2, "one real Widget instance per card, in order");

    uint32_t child_count = lv_obj_get_child_count(screen);
    expect(child_count == 1, "the session creates exactly one root container under the given parent");

    // session_root -> page[view 0] -> row -> cell[tile] -> widget root
    // (DashboardSession::create(), the design guide's tile grid).
    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(lv_obj_get_child_count(session_root) == 1, "one view yields exactly one paged child (the view's page)");

    lv_obj_t* page = lv_obj_get_child(session_root, 0);
    expect(lv_obj_get_child_count(page) == 1, "two one-tile cards share one tile row");

    expect(dashboard_test::widget_root_at(session_root, 0) != nullptr &&
               dashboard_test::widget_root_at(session_root, 1) != nullptr,
           "both widget roots are reachable through the real grid-cell nesting");

    session.shutdown();
    expect(session.widget_count() == 0, "shutdown() releases all Widget instances");
    expect(lv_obj_get_child_count(screen) == 0, "shutdown() deletes the real LVGL subtree, not just the C++ objects");
}

void test_unknown_card_type_is_crash_free() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory; // nothing registered at all

    DashboardModel model;
    DashboardParser::ParseError err;
    DashboardParser::parse(
        R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
        R"({"type":"some-widget-nobody-wrote-yet"})"
        R"(]}]}]})",
        model, err);

    DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(1));
    session.create(screen); // must not crash
    expect(session.widget_count() == 1, "an unregistered card type still yields a real (Unsupported) widget instance");
    session.shutdown();
    expect(true, "creating and tearing down an UnsupportedWidget does not crash");
}

void test_real_lovelace_fixture_renders_without_crashing() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory = make_factory_with_clock_registered(); // only "clock" is migrated so far

    std::string full = load_file("tests/fixtures/ha/lovelace_config_result.json");
    std::string result_json = extract_result_object(full);

    DashboardModel model;
    DashboardParser::ParseError err;
    bool ok = DashboardParser::parse(result_json, model, err);
    expect(ok, "the real captured dashboard parses");

    DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(1));
    session.create(screen); // clock -> ClockWidget, everything else -> UnsupportedWidget; must not crash

    // First ("Home") view has 11 sections; one card (statistics-graph) is
    // a background/floating graph — it still becomes a real widget
    // instance (Phase 9: rendered behind the grid, see DashboardSession),
    // just not a grid cell. Non-background-graph-card widgets by section:
    // 2 (clock+weather) + 1 (light) + 1 (thermostat) + 1 (cover tile) + 1
    // (cover tile) + 6 (sensor tiles) + 1 (tile next to the graph) + 1
    // (light) + 1 (thermostat) + 1 (thermostat) + 3 (sensor tiles) = 19,
    // plus the 1 background-graph widget itself = 20. Second ("Test")
    // view adds 3 more "heading" cards (4 sections: one empty, three with
    // one heading each) — every real view is now rendered, not just the
    // first. The three "heading" cards are title rows of the tile grid,
    // not widgets. Total: 20.
    expect(session.widget_count() == 20,
           "every card in every real view becomes a real widget instance (Unsupported or Clock), headings become "
           "title rows");

    session.shutdown();
    expect(lv_obj_get_child_count(screen) == 0, "the full real dashboard tears down cleanly");
}

void test_statistics_graph_card_is_a_tile_of_its_own() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory = make_factory_with_clock_registered(); // "statistics-graph" -> UnsupportedWidget here

    DashboardModel model;
    DashboardParser::ParseError err;
    DashboardParser::parse(
        R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
        R"({"type":"clock"},{"type":"statistics-graph","entities":["sensor.x"]})"
        R"(]}]}]})",
        model, err);

    DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(1));
    session.create(screen);

    expect(session.widget_count() == 2, "both the clock and the graph become real widget instances");
    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    lv_obj_t* graph_cell = dashboard_test::cell_at(session_root, 0, 1);
    expect(graph_cell != nullptr && !lv_obj_has_flag(graph_cell, LV_OBJ_FLAG_FLOATING) &&
               lv_obj_get_child(graph_cell, 0) != nullptr,
           "the statistics graph is a tile in the grid (design guide: chart tile), not a background layer");

    session.shutdown();
}

void test_multiple_views_become_separate_paged_children() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory = make_factory_with_clock_registered();

    DashboardModel model;
    DashboardParser::ParseError err;
    DashboardParser::parse(
        R"({"views":[)"
        R"({"type":"sections","sections":[{"type":"grid","cards":[{"type":"clock"}]}]},)"
        R"({"type":"sections","sections":[{"type":"grid","cards":[{"type":"clock"},{"type":"clock"}]}]})"
        R"(]})",
        model, err);

    DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(1));
    session.create(screen);

    expect(session.widget_count() == 3, "3 clock cards total across both views");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(lv_obj_get_child_count(session_root) == 2,
           "two views become two separate paged children (real horizontal per-view paging, not one flattened list)");

    expect(dashboard_test::widget_root_at(session_root, 0, 0, 0) != nullptr,
           "the first view's single card is reachable through its own page");
    expect(dashboard_test::widget_root_at(session_root, 1, 0, 0) != nullptr &&
               dashboard_test::widget_root_at(session_root, 1, 0, 1) != nullptr,
           "the second view's two cards are both reachable through their own page, independent of the first view");

    session.shutdown();
}

void test_cards_wrap_to_next_row_when_columns_overflow() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory = make_factory_with_clock_registered();

    DashboardModel model;
    DashboardParser::ParseError err;
    DashboardParser::parse(
        R"({"views":[{"type":"sections","max_columns":3,"sections":[{"type":"grid","column_span":3,"cards":[)"
        R"({"type":"clock","grid_options":{"columns":12}},)"
        R"({"type":"clock","grid_options":{"columns":12}},)"
        R"({"type":"clock","grid_options":{"columns":24}})"
        R"(]}]}]})",
        model, err);

    DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(1));
    session.create(screen);
    lv_obj_update_layout(screen); // resolve real x/y before reading them below

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    // lv_obj_get_x/y() is always parent-relative (LVGL's own contract),
    // and each card's widget root sits inside its *own* empty grid cell
    // (see DashboardSession::create()) — so comparing widget roots
    // directly would compare positions relative to different parents.
    // The cells themselves are the real grid items and true siblings
    // under the same section_content parent, so compare those instead.
    lv_obj_t* cell0 = lv_obj_get_parent(dashboard_test::widget_root_at(session_root, 0, 0, 0));
    lv_obj_t* cell1 = lv_obj_get_parent(dashboard_test::widget_root_at(session_root, 0, 0, 1));
    lv_obj_t* cell2 = lv_obj_get_parent(dashboard_test::widget_root_at(session_root, 0, 0, 2));

    // This proves DashboardSession actually wires DashboardLayout's math
    // into real lv_obj_set_grid_cell() calls — the pure-logic
    // test_dashboard_layout.cpp already covers the arithmetic in
    // isolation, this covers the real LVGL result end to end.
    // One section: its cells are siblings in one row of sections.
    expect(lv_obj_get_parent(cell0) == lv_obj_get_parent(cell1), "two one-tile cards share a row");
    expect(lv_obj_get_x(cell1) > lv_obj_get_x(cell0),
           "the second tile sits to the right of the first, not stacked under it");
    expect(lv_obj_get_y(cell2) > lv_obj_get_y(cell0) && lv_obj_get_x(cell2) == lv_obj_get_x(cell0),
           "a two-tile card that no longer fits next to them (1+1+2 > 3 columns) wraps to the next row");

    session.shutdown();
}

void test_repeated_dashboard_switches_do_not_leak_lvgl_objects() {
    lv_obj_t* screen = make_headless_screen();
    WidgetFactory factory = make_factory_with_clock_registered();

    for (int i = 0; i < 100; ++i) {
        DashboardModel model;
        DashboardParser::ParseError err;
        DashboardParser::parse(
            R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
            R"({"type":"clock"},{"type":"light","entity":"light.x"},{"type":"tile","entity":"sensor.y"})"
            R"(]}]}]})",
            model, err);

        DashboardSession session(std::move(model), factory, app::core::DashboardGeneration(i));
        session.create(screen);
        session.shutdown();
    }

    expect(lv_obj_get_child_count(screen) == 0,
           "100 repeated dashboard switches leave zero LVGL objects behind (CPP_MIGRATION_PLAN.md Phase 6 DoD)");
}

} // namespace

int main() {
    i18n_set_language("de"); // the expected texts are German
    lv_init();
    fonts_init(); // ClockWidget (and others) use app/fonts.h's shared fonts, unlike main.c's own
                  // startup path nothing calls this for a bare test binary otherwise.

    test_clock_and_unsupported_widgets_create_real_lvgl_objects();
    test_unknown_card_type_is_crash_free();
    test_real_lovelace_fixture_renders_without_crashing();
    test_statistics_graph_card_is_a_tile_of_its_own();
    test_multiple_views_become_separate_paged_children();
    test_cards_wrap_to_next_row_when_columns_overflow();
    test_repeated_dashboard_switches_do_not_leak_lvgl_objects();

    if (g_failures == 0) {
        std::printf("All DashboardSession/LVGL integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
