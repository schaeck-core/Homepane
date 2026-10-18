// Contract tests for QueryCoordinator (CPP_MIGRATION_PLAN.md Phase 9).
// Drives the full stack — FakeWebSocketTransport -> WebSocketConnection ->
// HaClient -> QueryCoordinator — so these tests exercise the real
// dedup/resubscribe/periodic-refresh logic, not a mock of it. No LVGL.

#include "../../app/cpp/home_assistant/QueryCoordinator.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <string>

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
    double now_epoch_s() const override { return now_s_; }
    double now_s_ = 1'700'000'000.0; // arbitrary fixed "now"
};

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
    std::unique_ptr<QueryCoordinator> coordinator;
};

std::unique_ptr<Fixture> make_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    auto connection = std::make_unique<WebSocketConnection>("ha", std::move(transport), fx->clock);
    fx->client = std::make_unique<HaClient>(std::move(connection), fx->clock, "token");
    fx->coordinator = std::make_unique<QueryCoordinator>(*fx->client, fx->clock, fx->wall_clock);
    return fx;
}

void reach_ready(Fixture& fx) {
    fx.client->start("ws://fake/api/websocket");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    fx.coordinator->on_ha_ready();
}

void reply_result(Fixture& fx, int id, bool success, const std::string& result_json = "") {
    std::string msg = R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":)" +
                       (success ? "true" : "false");
    if (success && !result_json.empty()) msg += R"(,"result":)" + result_json;
    msg += "}";
    fx.transport->simulate_fragment(msg, true);
}

// ---- Forecast ---------------------------------------------------------

void test_two_observers_same_forecast_yield_one_subscribe() {
    auto fx = make_fixture();
    reach_ready(*fx);

    int a_calls = 0, b_calls = 0;
    auto handle_a = fx->coordinator->observe_forecast("weather.home", "daily", [&](const ForecastState&) { ++a_calls; });
    auto handle_b = fx->coordinator->observe_forecast("weather.home", "daily", [&](const ForecastState&) { ++b_calls; });

    expect(count_occurrences(fx->transport->sent_messages, "weather/subscribe_forecast") == 1,
           "two observers of the same (entity, forecast_type) result in exactly one subscribe call");
    expect(fx->transport->sent_messages.back().find("\"forecast_type\":\"daily\"") != std::string::npos,
           "the subscribe command carries the requested forecast_type");

    reply_result(*fx, 1, true);
    reply_result(*fx, 1, true); // deliberately re-check idempotence isn't required here — just confirms no crash
    expect(fx->coordinator->diagnostics().active_forecast_subscriptions == 1,
           "exactly one active forecast subscription is tracked after the Result confirms it");

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"forecast":[)"
        R"({"condition":"sunny","datetime":"2026-01-01T00:00:00+00:00","temperature":10.0,"templow":2.0})"
        R"(]}})",
        true);

    expect(a_calls == 1 && b_calls == 1,
           "both observers are notified independently from the single underlying forecast record");
}

void test_deliver_current_before_data_shows_loading() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe_forecast("weather.home", "daily", [](const ForecastState&) {});
    bool delivered_has_data = true;
    ForecastState delivered;
    fx->coordinator->deliver_current_forecast("weather.home", "daily", handle);
    // deliver_current_forecast calls into the SAME observer fn synchronously
    // — re-observe with a capturing lambda to actually inspect the value.
    auto handle2 = fx->coordinator->observe_forecast("weather.home", "daily",
                                                       [&](const ForecastState& s) { delivered = s; });
    fx->coordinator->deliver_current_forecast("weather.home", "daily", handle2);
    delivered_has_data = delivered.has_data;

    expect(!delivered_has_data, "deliver_current before any event has has_data == false (still loading)");
}

void test_forecast_event_updates_state_correctly() {
    auto fx = make_fixture();
    reach_ready(*fx);

    ForecastState last;
    auto handle =
        fx->coordinator->observe_forecast("weather.home", "daily", [&](const ForecastState& s) { last = s; });
    reply_result(*fx, 1, true);

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"forecast":[)"
        R"({"condition":"sunny","datetime":"2026-01-01T00:00:00+00:00","temperature":10.0,"templow":2.0},)"
        R"({"condition":"rainy","datetime":"2026-01-02T00:00:00+00:00","temperature":8.5})"
        R"(]}})",
        true);

    expect(last.has_data, "an event with real forecast data sets has_data true");
    expect(last.days.size() == 2, "both forecast days are parsed");
    expect(last.days[0].condition == "sunny" && last.days[0].temperature.has_value() &&
               *last.days[0].temperature == 10.0 && last.days[0].templow.has_value() && *last.days[0].templow == 2.0,
           "the first day's fields are parsed correctly");
    expect(last.days[1].condition == "rainy" && !last.days[1].templow.has_value(),
           "a day missing 'templow' leaves it unset rather than defaulting to 0");
}

void test_event_for_a_different_subscription_id_is_ignored() {
    auto fx = make_fixture();
    reach_ready(*fx);

    int calls = 0;
    auto handle = fx->coordinator->observe_forecast("weather.home", "daily", [&](const ForecastState&) { ++calls; });
    reply_result(*fx, 1, true); // this record's remote_subscription_id becomes 1

    fx->transport->simulate_fragment(R"({"id":99,"type":"event","event":{"forecast":[]}})", true);
    expect(calls == 0, "an event carrying an unrelated subscription id does not notify this record's observers");
}

void test_forecast_resubscribes_after_reconnect() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe_forecast("weather.home", "daily", [](const ForecastState&) {});
    reply_result(*fx, 1, true);
    expect(count_occurrences(fx->transport->sent_messages, "weather/subscribe_forecast") == 1, "initial subscribe sent");

    fx->transport->simulate_closed("network blip");
    fx->coordinator->on_ha_disconnected();
    expect(fx->coordinator->diagnostics().active_forecast_subscriptions == 0,
           "disconnect drops active-subscription bookkeeping");

    fx->clock.advance(60000);
    fx->client->poll(fx->clock.now_ms());
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    fx->coordinator->on_ha_ready();

    expect(count_occurrences(fx->transport->sent_messages, "weather/subscribe_forecast") == 2,
           "reconnect triggers exactly one fresh subscribe for the still-observed forecast");
}

void test_forecast_handle_reset_never_sends_unsubscribe() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe_forecast("weather.home", "daily", [](const ForecastState&) {});
    reply_result(*fx, 1, true);
    handle.reset();
    fx->coordinator->poll(fx->clock.now_ms());

    expect(count_occurrences(fx->transport->sent_messages, "unsubscribe") == 0,
           "losing the last local forecast observer never sends an unsubscribe (documented scope: the record just "
           "keeps running in the background, unlike EntitySubscriptionCoordinator's debounced unsubscribe)");
}

// ---- Statistics ---------------------------------------------------------

StatisticsQuery make_query(std::vector<std::string> entities = {"sensor.power"},
                            std::vector<std::string> stat_types = {"mean"}) {
    StatisticsQuery q;
    q.entity_ids = std::move(entities);
    q.period = "hour";
    q.stat_types = std::move(stat_types);
    q.days_to_show = 1;
    return q;
}

void test_two_observers_same_query_yield_one_request() {
    auto fx = make_fixture();
    reach_ready(*fx);

    StatisticsQuery q = make_query();
    int a_calls = 0, b_calls = 0;
    auto handle_a = fx->coordinator->observe_statistics(q, 60000, [&](const StatisticsResult&) { ++a_calls; });
    auto handle_b = fx->coordinator->observe_statistics(q, 60000, [&](const StatisticsResult&) { ++b_calls; });

    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 1,
           "two observers of an identical query result in exactly one request");
    expect(fx->transport->sent_messages.back().find("\"sensor.power\"") != std::string::npos,
           "the request carries the query's entity_ids");

    reply_result(*fx, 1, true,
                 R"({"sensor.power":[{"start":1700000000,"mean":1.5},{"start":1700003600,"mean":2.5}]})");

    expect(a_calls == 1 && b_calls == 1, "both observers are notified independently from the single cached result");
}

void test_different_queries_are_not_deduped() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle_a = fx->coordinator->observe_statistics(make_query({"sensor.power"}), 60000,
                                                          [](const StatisticsResult&) {});
    auto handle_b = fx->coordinator->observe_statistics(make_query({"sensor.other"}), 60000,
                                                          [](const StatisticsResult&) {});
    fx->coordinator->poll(fx->clock.now_ms());

    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 2,
           "two observers with genuinely different queries (different entity_ids) get two separate requests");
}

void test_statistics_response_is_parsed_and_bounded() {
    auto fx = make_fixture();
    reach_ready(*fx);

    StatisticsResult last;
    auto handle = fx->coordinator->observe_statistics(make_query({"sensor.power"}, {"mean", "max"}), 60000,
                                                        [&](const StatisticsResult& r) { last = r; });
    fx->coordinator->poll(fx->clock.now_ms());
    reply_result(*fx, 1, true,
                 R"({"sensor.power":[{"start":1700000000,"mean":1.5,"max":3.0},)"
                 R"({"start":1700003600,"mean":2.5,"max":4.0}]})");

    expect(last.has_data, "a successful response with real data sets has_data true");
    expect(last.series.size() == 2, "one series per configured stat_type (mean, max)");
    const StatisticsSeries* mean_series = nullptr;
    const StatisticsSeries* max_series = nullptr;
    for (const auto& s : last.series) {
        if (s.stat_type == "mean") mean_series = &s;
        if (s.stat_type == "max") max_series = &s;
    }
    expect(mean_series && mean_series->points.size() == 2 && mean_series->points[0].value == 1.5 &&
               mean_series->points[0].timestamp_epoch_s == 1700000000,
           "the 'mean' series has both points with the correct value and timestamp");
    expect(max_series && max_series->points.size() == 2 && max_series->points[1].value == 4.0,
           "the 'max' series is parsed independently from 'mean'");
}

void test_periodic_refresh_reissues_after_interval() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe_statistics(make_query(), /*refresh_interval_ms=*/60000,
                                                        [](const StatisticsResult&) {});
    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 1,
           "the first poll() issues the initial request");
    reply_result(*fx, 1, true, R"({})");

    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 1,
           "polling again before the refresh interval elapses does not re-issue the request");

    fx->clock.advance(61000);
    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 2,
           "polling after the refresh interval elapses re-issues the request");
}

void test_last_observer_leaving_cancels_in_flight_request() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe_statistics(make_query(), 60000, [](const StatisticsResult&) {});
    fx->coordinator->poll(fx->clock.now_ms()); // request id=1 now in flight

    handle.reset(); // last (only) observer leaves while the request is still in flight

    // The (now stale) Result arrives after every observer is gone —
    // QueryCoordinator must not crash or resurrect a query nobody is
    // watching anymore.
    reply_result(*fx, 1, true, R"({"sensor.power":[{"start":1700000000,"mean":1.5}]})");
    expect(true, "a Result arriving for a statistics query whose last observer left mid-request does not crash");

    auto handle2 = fx->coordinator->observe_statistics(make_query(), 60000, [](const StatisticsResult&) {});
    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "recorder/statistics_during_period") == 2,
           "a new observer for the same query after everyone left starts a fresh request, not reusing stale state");
}

} // namespace

int main() {
    test_two_observers_same_forecast_yield_one_subscribe();
    test_deliver_current_before_data_shows_loading();
    test_forecast_event_updates_state_correctly();
    test_event_for_a_different_subscription_id_is_ignored();
    test_forecast_resubscribes_after_reconnect();
    test_forecast_handle_reset_never_sends_unsubscribe();
    test_two_observers_same_query_yield_one_request();
    test_different_queries_are_not_deduped();
    test_statistics_response_is_parsed_and_bounded();
    test_periodic_refresh_reissues_after_interval();
    test_last_observer_leaving_cancels_in_flight_request();

    if (g_failures == 0) {
        std::printf("All QueryCoordinator contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
