// End-to-end integration test for Phase 9 of CPP_MIGRATION_PLAN.md:
// StatisticsWidget — periodic recorder/statistics_during_period queries
// via QueryCoordinator, deduped by full query (not request id), rendered
// as a real LVGL lv_chart series — through the full real stack down to
// real LVGL objects. Only the network socket is faked.
//
// Every real captured Lovelace "statistics-graph" card is background/
// floating (DashboardCard.is_background_graph is true for this type
// unconditionally, matching legacy app/views/dashboard.c) — so, unlike
// every other widget integration test, these widgets are NOT reachable
// via the shared grid-cell dashboard_test::widget_root_at() helper; they
// sit directly under section_content (see DashboardSession::create()).

#include "app/i18n.h"
#include "dashboard_test_helpers.h"
#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/dashboard/DashboardParser.h"
#include "app/cpp/dashboard/DashboardSession.h"
#include "app/cpp/dashboard/DefaultWidgetRegistration.h"
#include "app/cpp/home_assistant/EntitySubscriptionCoordinator.h"
#include "app/cpp/home_assistant/HaCommandService.h"
#include "app/cpp/home_assistant/QueryCoordinator.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace app::dashboard;
using namespace app::home_assistant;
using app::core::IMonotonicClock;
using app::core::IWallClock;
using app::net::WebSocketConnection;
using test_fakes::FakeWebSocketTransport;

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

class FakeClock final : public IMonotonicClock {
public:
    uint64_t now_ms() const override { return now_; }
    void advance(uint64_t ms) { now_ += ms; }
    uint64_t now_ = 1000;
};

class FakeWallClock final : public IWallClock {
public:
    double now_epoch_s() const override { return 1'700'000'000.0; }
};

void noop_flush_cb(lv_display_t* disp, const lv_area_t*, uint8_t*) { lv_display_flush_ready(disp); }

lv_obj_t* make_headless_screen() {
    lv_display_t* disp = lv_display_create(720, 720);
    static uint8_t buf[720 * 10 * 4];
    lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, noop_flush_cb);
    return lv_display_get_screen_active(disp);
}

int count_occurrences(const std::vector<std::string>& haystack, const std::string& needle) {
    int n = 0;
    for (const auto& s : haystack) {
        if (s.find(needle) != std::string::npos) ++n;
    }
    return n;
}

struct Fixture {
    FakeClock clock;
    FakeWallClock wall_clock;
    FakeWebSocketTransport* transport;
    std::unique_ptr<HaClient> client;
    EntityStore store;
    std::unique_ptr<EntitySubscriptionCoordinator> coordinator;
    std::unique_ptr<QueryCoordinator> query_coordinator;
    std::unique_ptr<HaCommandService> commands;
    WidgetFactory factory;
};

std::unique_ptr<Fixture> make_ready_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    auto connection = std::make_unique<WebSocketConnection>("ha", std::move(transport), fx->clock);
    fx->client = std::make_unique<HaClient>(std::move(connection), fx->clock, "token");
    fx->coordinator = std::make_unique<EntitySubscriptionCoordinator>(fx->store, *fx->client,
                                                                       HaInstanceId{"homeassistant"}, fx->clock);
    fx->query_coordinator = std::make_unique<QueryCoordinator>(*fx->client, fx->clock, fx->wall_clock);
    fx->commands = std::make_unique<HaCommandService>(*fx->client);
    register_default_widgets(fx->factory, HaInstanceId{"homeassistant"}, *fx->commands);

    fx->client->start("ws://fake/api/websocket");
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    fx->coordinator->on_ha_ready();
    fx->query_coordinator->on_ha_ready();
    return fx;
}

// ChartTile's LVGL tree (ChartTile::create()):
//   tile -> [0]=title, [1]=value row -> [0]=value, [1]=unit, [2]=bars (one child per bar), [3]=empty state
lv_obj_t* nth_background_widget_root(lv_obj_t* session_root, uint32_t index) {
    return dashboard_test::widget_root_at(session_root, index);
}

uint32_t bar_count(lv_obj_t* tile) { return lv_obj_get_child_count(lv_obj_get_child(tile, 2)); }
bool empty_state_shown(lv_obj_t* tile) { return !lv_obj_has_flag(lv_obj_get_child(tile, 3), LV_OBJ_FLAG_HIDDEN); }
uint32_t last_bar_color(lv_obj_t* tile) {
    lv_obj_t* bars = lv_obj_get_child(tile, 2);
    return lv_color_to_int(lv_obj_get_style_bg_color(lv_obj_get_child(bars, -1), LV_PART_MAIN));
}

// The id of the last statistics request (the tile's entity subscription takes an id too).
int statistics_request_id(Fixture& fx) {
    for (auto it = fx.transport->sent_messages.rbegin(); it != fx.transport->sent_messages.rend(); ++it) {
        if (it->find("recorder/statistics_during_period") == std::string::npos) continue;
        const size_t p = it->find("\"id\":");
        return p == std::string::npos ? -1 : std::stoi(it->substr(p + 5));
    }
    return -1;
}

DashboardModel make_two_statistics_cards_model(const std::string& entity_id) {
    DashboardModel model;
    DashboardParser::ParseError err;
    std::string card = R"({"type":"statistics-graph","entities":[")" + entity_id + R"("],)"
                        R"("period":"hour","stat_types":["mean"],"days_to_show":1,"title":"Power"})";
    std::string json = R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)" + card + "," + card +
                        R"(]}]}]})";
    DashboardParser::parse(json, model, err);
    return model;
}

void reply_statistics(Fixture& fx, int id, const std::string& entity_id, const std::string& buckets_json) {
    std::string result = "{\"" + entity_id + "\":" + buckets_json + "}";
    std::string msg =
        R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":true,"result":)" + result + "}";
    fx.transport->simulate_fragment(msg, true);
}

void test_two_widgets_with_identical_query_dedup_to_one_request() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_statistics_cards_model("sensor.power");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);
    fx->query_coordinator->poll(fx->clock.now_ms());

    expect(session.widget_count() == 2, "two statistics cards with the same query yield two real widget instances");
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 1,
           "two chart tiles with an identical query result in exactly one request");

    session.shutdown();
}

void test_initial_state_then_bars() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_statistics_cards_model("sensor.power");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    lv_obj_t* widget_a = nth_background_widget_root(session_root, 0);
    lv_obj_t* widget_b = nth_background_widget_root(session_root, 1);
    expect(bar_count(widget_a) == 0 && !empty_state_shown(widget_a),
           "before the first response: no bars and no empty state (still loading)");

    fx->query_coordinator->poll(fx->clock.now_ms());
    reply_statistics(*fx, statistics_request_id(*fx), "sensor.power",
                     R"([{"start":1700000000,"mean":1.5},{"start":1700003600,"mean":2.5}])");

    expect(bar_count(widget_a) == 24 && bar_count(widget_b) == 24,
           "always 24 bars (hours without data as minimal bars), on both tiles");
    expect(last_bar_color(widget_a) == 0xD6B98A, "the last bar (now) is sand");
    expect(!empty_state_shown(widget_a), "with data the empty state stays hidden");

    session.shutdown();
}

void test_single_point_and_no_data() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_statistics_cards_model("sensor.power");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);
    fx->query_coordinator->poll(fx->clock.now_ms());
    reply_statistics(*fx, statistics_request_id(*fx), "sensor.power", R"([{"start":1700000000,"mean":1.5}])");

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    lv_obj_t* widget_a = nth_background_widget_root(session_root, 0);
    expect(bar_count(widget_a) == 24, "a single point keeps the bar width: 23 minimal bars before it");

    fx->clock.advance(301000);
    fx->query_coordinator->poll(fx->clock.now_ms());
    reply_statistics(*fx, statistics_request_id(*fx), "sensor.power", "[]");
    expect(bar_count(widget_a) == 0 && empty_state_shown(widget_a),
           "no statistics at all: \"Noch keine Daten\" instead of an empty chart");

    session.shutdown();
}

void test_current_value_from_the_entity() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_statistics_cards_model("sensor.power");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);
    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{"sensor.power":{"s":"1240","a":{"unit_of_measurement":"W"},"lc":1000.0}}}})",
        true);
    lv_obj_t* tile = nth_background_widget_root(lv_obj_get_child(screen, 0), 0);
    lv_obj_t* row = lv_obj_get_child(tile, 1);
    expect(std::string(lv_label_get_text(lv_obj_get_child(row, 0))) == "1,24" &&
               std::string(lv_label_get_text(lv_obj_get_child(row, 1))) == "kW",
           "the entity's current value is shown, 1240 W as 1,24 kW");
    session.shutdown();
}

void test_periodic_refresh_reissues_after_interval() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_statistics_cards_model("sensor.power");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);

    fx->query_coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 1,
           "the first poll() issues the initial request");
    reply_statistics(*fx, statistics_request_id(*fx), "sensor.power", R"([{"start":1700000000,"mean":1.5}])");

    fx->clock.advance(301000); // past the chart tile's 5 min refresh interval
    fx->query_coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 2,
           "polling after the refresh interval elapses re-issues the request");

    session.shutdown();
}

void test_dashboard_switch_during_in_flight_request_is_crash_free() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_statistics_cards_model("sensor.power");
    auto session = std::make_unique<DashboardSession>(std::move(model), fx->factory,
                                                        app::core::DashboardGeneration(1), fx->coordinator.get(),
                                                        fx->query_coordinator.get());
    session->create(screen);
    fx->query_coordinator->poll(fx->clock.now_ms()); // request id=1 in flight

    session->shutdown(); // dashboard switch while the request is still in flight
    session.reset();

    // The (now stale) response arrives after every widget/observer is
    // gone — QueryCoordinator/StatisticsWidget must not crash.
    reply_statistics(*fx, statistics_request_id(*fx), "sensor.power", R"([{"start":1700000000,"mean":1.5}])");
    expect(true, "a response arriving for a query whose widgets were torn down mid-request does not crash");
}

} // namespace

int main() {
    i18n_set_language("de"); // the expected texts are German
    lv_init();
    fonts_init(); // widgets now use app/fonts.h's shared fonts; nothing else in a bare test binary calls this.

    test_two_widgets_with_identical_query_dedup_to_one_request();
    test_initial_state_then_bars();
    test_single_point_and_no_data();
    test_current_value_from_the_entity();
    test_periodic_refresh_reissues_after_interval();
    test_dashboard_switch_during_in_flight_request_is_crash_free();

    if (g_failures == 0) {
        std::printf("All chart tile end-to-end integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
