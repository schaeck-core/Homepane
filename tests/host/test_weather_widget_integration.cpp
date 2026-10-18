// End-to-end integration test for Phase 9 of CPP_MIGRATION_PLAN.md: the
// first widget combining two data sources — the weather entity's own
// state (EntityStore/EntitySubscriptionCoordinator, unchanged since
// Phase 5/7) and its forecast (QueryCoordinator, new in Phase 9) —
// through the full real stack down to real LVGL objects. Only the
// network socket is faked.

#include "app/i18n.h"
#include "lvgl/lvgl.h"

#include "app/fonts.h"

#include "app/cpp/dashboard/DashboardParser.h"
#include "app/cpp/dashboard/DashboardSession.h"
#include "app/cpp/dashboard/DefaultWidgetRegistration.h"
#include "app/cpp/home_assistant/EntitySubscriptionCoordinator.h"
#include "app/cpp/home_assistant/HaCommandService.h"
#include "app/cpp/home_assistant/QueryCoordinator.h"
#include "dashboard_test_helpers.h"
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
    lv_display_t* disp = lv_display_create(320, 240);
    static uint8_t buf[320 * 10 * 4];
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

// WeatherWidget's LVGL tree (create() in WeatherWidget.cpp):
// wrapper -> [0]=head -> [0]=top_row -> [0]=icon, [1]=temperature
//                       [1]=current_label
//            [1]=forecast_row (one day group per day)
std::string read_current_label_text(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* widget_root = dashboard_test::widget_root_at(session_root, widget_index);
    if (!widget_root) return "<no widget>";
    lv_obj_t* current_label = lv_obj_get_child(lv_obj_get_child(widget_root, 0), 1);
    if (!current_label) return "<no current label>";
    return lv_label_get_text(current_label);
}

lv_obj_t* forecast_row_of(lv_obj_t* session_root, uint32_t widget_index) {
    lv_obj_t* widget_root = dashboard_test::widget_root_at(session_root, widget_index);
    return lv_obj_get_child(widget_root, 1);
}

DashboardModel make_two_weather_cards_model(const std::string& entity_id) {
    DashboardModel model;
    DashboardParser::ParseError err;
    std::string json = R"({"views":[{"type":"sections","sections":[{"type":"grid","cards":[)"
                        R"({"type":"weather-forecast","entity":")" +
                        entity_id + R"("},)"
                        R"({"type":"weather-forecast","entity":")" +
                        entity_id + R"("})"
                        R"(]}]}]})";
    DashboardParser::parse(json, model, err);
    return model;
}

void test_two_widgets_dedup_both_entity_and_forecast_subscriptions() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_weather_cards_model("weather.home");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);

    expect(session.widget_count() == 2, "two weather cards for the same entity yield two real widget instances");
    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 1,
           "two WeatherWidgets observing the same entity still result in exactly one subscribe_entities call");
    expect(count_occurrences(fx->transport->sent_messages, "weather/subscribe_forecast") == 2,
           "two WeatherWidgets observing the same entity: one daily and one hourly forecast subscription, not four");
    expect(fx->transport->sent_messages.back().find("\"entity_id\":\"weather.home\"") != std::string::npos ||
               count_occurrences(fx->transport->sent_messages, "\"entity_id\":\"weather.home\"") >= 1,
           "the forecast subscribe targets the correct entity_id");

    session.shutdown();
}

void test_initial_state_shows_loading_placeholder() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_weather_cards_model("weather.home");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    expect(read_current_label_text(session_root, 0) == "…",
           "the widget shows the loading placeholder before any entity data has arrived");
    expect(lv_obj_get_child_count(forecast_row_of(session_root, 0)) == 0,
           "the forecast row is empty before any forecast event has arrived");

    session.shutdown();
}

void test_entity_snapshot_renders_current_conditions() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_weather_cards_model("weather.home");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true);

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{"weather.home":{"s":"sunny","a":{)"
        R"("temperature":21.5,"temperature_unit":"°C","humidity":60,"friendly_name":"Home"},"lc":1000.0}}}})",
        true);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);
    // Matches the legacy widget's labelHumidity (weather_forecast.c's
    // initialRender): high/low temperature + humidity, not the condition
    // name — high/low are still unset here since no forecast event has
    // arrived yet.
    expect(read_current_label_text(session_root, 0) == "– / – · 60 %",
           "the summary label shows the entity's humidity even before any forecast data has arrived");

    session.shutdown();
}

void test_forecast_event_populates_summary_line_and_forecast_row() {
    auto fx = make_ready_fixture();
    lv_obj_t* screen = make_headless_screen();

    DashboardModel model = make_two_weather_cards_model("weather.home");
    DashboardSession session(std::move(model), fx->factory, app::core::DashboardGeneration(1), fx->coordinator.get(),
                              fx->query_coordinator.get());
    session.create(screen);
    fx->transport->simulate_fragment(R"({"id":1,"type":"result","success":true})", true); // entity subscribe ack
    fx->transport->simulate_fragment(R"({"id":2,"type":"result","success":true})", true); // forecast subscribe ack

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{"weather.home":{"s":"sunny","a":{)"
        R"("temperature":21.5,"temperature_unit":"°C","humidity":60,"friendly_name":"Home"},"lc":1000.0}}}})",
        true);

    // days[0] is "today" — consumed by the summary line, not the forecast
    // strip (matches the legacy widget's own offset: callback_subscribtion_forecast's
    // index 0 -> weatherData->high/low_temperature, index 1.. -> daily_forecast[]).
    fx->transport->simulate_fragment(
        R"({"id":2,"type":"event","event":{"forecast":[)"
        R"({"condition":"sunny","datetime":"2026-01-01T00:00:00+00:00","temperature":15.0,"templow":5.0},)"
        R"({"condition":"rainy","datetime":"2026-01-02T00:00:00+00:00","temperature":10.0,"templow":2.0},)"
        R"({"condition":"cloudy","datetime":"2026-01-03T00:00:00+00:00","temperature":8.5,"templow":1.0})"
        R"(]}})",
        true);

    lv_obj_t* session_root = lv_obj_get_child(screen, 0);

    expect(read_current_label_text(session_root, 0) == "15° / 5° · 60 %",
           "the summary label uses today's (days[0]) high/low once the forecast event arrives");

    lv_obj_t* row = forecast_row_of(session_root, 0);
    expect(lv_obj_get_child_count(row) == 2,
           "the forecast row gets one day-group per forecast day after today (3 sent - 1 today = 2)");

    // Each day-group (matching the legacy app/widgets/weather_forecast.c
    // layout): [0]=short weekday, [1]=condition icon, [2]=high, [3]=low.
    lv_obj_t* day0 = lv_obj_get_child(row, 0);
    expect(std::string(lv_label_get_text(lv_obj_get_child(day0, 0))) == "Fr",
           "the strip's first day is days[1] (2026-01-02, a Friday), not today");
    expect(std::string(lv_label_get_text(lv_obj_get_child(day0, 2))) == "10°",
           "the first shown day's high-temperature label rounds to the nearest degree");
    expect(std::string(lv_label_get_text(lv_obj_get_child(day0, 3))) == "2°",
           "the first shown day's low-temperature label rounds to the nearest degree");

    lv_obj_t* day1 = lv_obj_get_child(row, 1);
    expect(std::string(lv_label_get_text(lv_obj_get_child(day1, 0))) == "Sa",
           "the strip's second day is days[2] (2026-01-03, a Saturday)");
    expect(std::string(lv_label_get_text(lv_obj_get_child(day1, 2))) == "8°" &&
               std::string(lv_label_get_text(lv_obj_get_child(day1, 3))) == "1°",
           "the second shown day's labels are independent of the first");

    // Both widgets (same entity) receive the same forecast update.
    lv_obj_t* row2 = forecast_row_of(session_root, 1);
    expect(lv_obj_get_child_count(row2) == 2,
           "the second widget for the same entity receives the same forecast update");

    session.shutdown();
}

} // namespace

int main() {
    i18n_set_language("de"); // the expected texts are German
    lv_init();
    fonts_init(); // widgets now use app/fonts.h's shared fonts; nothing else in a bare test binary calls this.

    test_two_widgets_dedup_both_entity_and_forecast_subscriptions();
    test_initial_state_shows_loading_placeholder();
    test_entity_snapshot_renders_current_conditions();
    test_forecast_event_populates_summary_line_and_forecast_row();

    if (g_failures == 0) {
        std::printf("All WeatherWidget end-to-end integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
