// Unit tests for DashboardParser against a real captured Lovelace
// "sections" dashboard config (Phase 6 of docs/ARCHITECTURE.md §7).
// No LVGL.

#include "../../app/cpp/dashboard/DashboardParser.h"

#include <cjson/cJSON.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string load_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The fixture is a full {"id":...,"type":"result",...,"result":{...}}
// message, matching what actually came off the wire. Real call sites get
// HaClient::ResultMessage::result_json, which is already just the inner
// object — replicate that stripping here via cJSON.
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

const DashboardCard* find_card_by_type(const DashboardView& view, const std::string& type) {
    for (const auto& section : view.sections) {
        for (const auto& card : section.cards) {
            if (card.type == type) return &card;
        }
    }
    return nullptr;
}

void test_real_lovelace_fixture() {
    std::string full = load_file("tests/fixtures/ha/lovelace_config_result.json");
    expect(!full.empty(), "lovelace_config_result.json fixture is readable (run tests from the repo root)");
    std::string result_json = extract_result_object(full);
    expect(!result_json.empty(), "the fixture's 'result' object was extracted");

    DashboardModel model;
    DashboardParser::ParseError error;
    bool ok = DashboardParser::parse(result_json, model, error);
    expect(ok, "the real dashboard config parses successfully");
    expect(model.views.size() == 2, "the real dashboard has exactly two views ('Home' and 'Test')");

    if (model.views.size() < 2) return;
    const DashboardView& home = model.views[0];
    expect(home.title == "Home", "first view title is 'Home'");
    expect(home.type == "sections", "first view type is 'sections'");
    expect(home.max_columns == 3, "first view max_columns is captured");
    expect(home.sections.size() == 11, "first view has all eleven real sections");

    const DashboardCard* clock = find_card_by_type(home, "clock");
    expect(clock != nullptr, "the clock card is found");
    if (clock) {
        expect(clock->grid_columns == 15 && clock->grid_rows == 4, "clock card grid_options are captured");
        expect(clock->raw_json.find("clock_style") != std::string::npos,
               "raw_json preserves widget-specific fields the model doesn't hand-model (clock_style)");
    }

    const DashboardCard* graph = find_card_by_type(home, "statistics-graph");
    expect(graph != nullptr, "the statistics-graph card is found");
    if (graph) {
        expect(graph->is_background_graph,
               "statistics-graph is flagged as a background/floating element (preserves the legacy dashboard.c semantics)");
    }

    const DashboardCard* light = find_card_by_type(home, "light");
    expect(light != nullptr && light->entity_id == "light.bedroom_lamp",
           "the light card's entity_id is extracted as a first-class field");

    const DashboardView& test_view = model.views[1];
    expect(test_view.title == "Test", "second view title is 'Test'");
    expect(test_view.sections.size() == 4, "second view has four sections, including one with zero cards");
    expect(test_view.sections[0].cards.empty(), "an empty section (real data: no cards yet) parses to zero cards, not a crash");
    expect(!test_view.sections[1].cards.empty() && test_view.sections[1].cards[0].type == "heading",
           "a 'heading' card (no entity, no HA subscription) parses like any other card type");
}

void test_invalid_json_fails_structurally() {
    DashboardModel model;
    DashboardParser::ParseError error;
    bool ok = DashboardParser::parse("not json", model, error);
    expect(!ok, "non-JSON input fails to parse rather than crashing");
    expect(!error.message.empty(), "a structural failure carries a human-readable message");
}

void test_missing_views_fails_with_path() {
    DashboardModel model;
    DashboardParser::ParseError error;
    bool ok = DashboardParser::parse(R"({"foo":"bar"})", model, error);
    expect(!ok, "a JSON object without a 'views' array fails to parse");
    expect(error.json_path == "views", "the error reports the JSON path of the missing field");
}

void test_unknown_extra_card_fields_are_tolerated() {
    DashboardModel model;
    DashboardParser::ParseError error;
    bool ok = DashboardParser::parse(
        R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
        R"({"type":"some-future-card-type","totally_unknown_field":{"nested":[1,2,3]}}]}]}]})",
        model, error);
    expect(ok, "a card with entirely unknown/future fields does not fail the whole parse");
    expect(model.views.size() == 1 && model.views[0].sections.size() == 1 &&
               model.views[0].sections[0].cards.size() == 1,
           "the card with unknown fields is still captured");
    if (!model.views.empty() && !model.views[0].sections.empty() && !model.views[0].sections[0].cards.empty()) {
        expect(model.views[0].sections[0].cards[0].type == "some-future-card-type",
               "the card's type is still captured even though its other fields are unmodeled");
    }
}

void test_flat_cards_fallback_for_non_sections_view() {
    DashboardModel model;
    DashboardParser::ParseError error;
    bool ok = DashboardParser::parse(
        R"({"views":[{"title":"Masonry","cards":[{"type":"clock"}]}]})", model, error);
    expect(ok, "a masonry-style view (flat 'cards', no 'sections') parses successfully");
    expect(!model.views.empty() && model.views[0].sections.empty() && model.views[0].cards.size() == 1,
           "flat cards land in DashboardView::cards when there is no 'sections' array");
}

void test_container_cards_are_flattened() {
    DashboardModel model;
    DashboardParser::ParseError error;
    bool ok = DashboardParser::parse(
        R"({"views":[{"type":"sections","sections":[{"type":"grid","title":"Flur","cards":[
            {"type":"vertical-stack","cards":[
                {"type":"tile","entity":"light.a"},
                {"type":"horizontal-stack","cards":[{"type":"tile","entity":"switch.b"},{"type":"button","entity":"scene.c"}]}
            ]},
            {"type":"grid","cards":[{"type":"tile","entity":"sensor.d"}]},
            {"type":"conditional","conditions":[],"card":{"type":"tile","entity":"lock.e"}}
        ]}]}]})",
        model, error);
    expect(ok, "a section with stacks, a grid card and a conditional card parses");
    const auto& cards = model.views[0].sections[0].cards;
    expect(cards.size() == 5, "stacks, the grid card and the conditional card are replaced by the cards inside");
    if (cards.size() == 5) {
        expect(cards[0].entity_id == "light.a" && cards[1].entity_id == "switch.b" && cards[2].type == "button" &&
                   cards[3].entity_id == "sensor.d" && cards[4].entity_id == "lock.e",
               "the inner cards keep their order, depth-first");
        expect(cards[2].section_title == "Flur", "inner cards carry the section title");
    }
}

} // namespace

void test_homepane_settings() {
    DashboardModel model;
    DashboardParser::ParseError error;
    const bool ok = DashboardParser::parse(
        R"({"homepane":{"rows":"3","orientation":"portrait"},"views":[)"
        R"({"type":"sections","homepane":{"rows":"cols"},"sections":[]},{"type":"sections","sections":[]},)"
        R"({"type":"sections","homepane":{"rows":4},"sections":[]}]})",
        model, error);
    expect(ok && model.rows == "3", "the dashboard's homepane.rows is read");
    expect(model.views[0].rows == "cols" && model.views[1].rows.empty() && model.views[2].rows == "4",
           "a view's homepane.rows is read, as text or number");
}

int main() {
    test_real_lovelace_fixture();
    test_homepane_settings();
    test_invalid_json_fails_structurally();
    test_missing_views_fails_with_path();
    test_unknown_extra_card_fields_are_tolerated();
    test_flat_cards_fallback_for_non_sections_view();
    test_container_cards_are_flattened();

    if (g_failures == 0) {
        std::printf("All DashboardParser unit tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
