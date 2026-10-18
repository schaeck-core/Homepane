// Integration test for SetupController (CPP_MIGRATION_PLAN.md Phase
// 10's "Setup" consumer) — the full real stack (FakeWebSocketTransport
// -> WebSocketConnection -> HaClient -> PanelListParser) through the
// SAME entry point app_facade.cpp uses live. No LVGL: unlike
// DashboardController, Setup never builds a widget tree.

#include "app/cpp/application/SetupController.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace app::application;
using namespace app::home_assistant;
using app::core::IMonotonicClock;
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

int count_occurrences(const std::vector<std::string>& haystack, const std::string& needle) {
    int n = 0;
    for (const auto& s : haystack) {
        if (s.find(needle) != std::string::npos) ++n;
    }
    return n;
}

struct Fixture {
    FakeClock clock;
    FakeWebSocketTransport* transport;
    std::unique_ptr<SetupController> controller;
};

std::unique_ptr<Fixture> make_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    fx->controller = std::make_unique<SetupController>(fx->clock, nullptr, std::move(transport));
    return fx;
}

void reach_ready(Fixture& fx) {
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok"})", true);
}

void reply_get_panels(Fixture& fx, int id, const std::string& result_json) {
    fx.transport->simulate_fragment(
        R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":true,"result":)" + result_json + "}",
        true);
}

constexpr const char* kOnePanel = R"({"lovelace":{"config":{"mode":"storage"},"title":"Home","url_path":"lovelace"}})";

void test_connects_and_authenticates_with_the_given_url_and_token() {
    auto fx = make_fixture();

    fx->controller->fetch_panels(
        "ws://ha.local:8123/api/websocket", "token", [](std::vector<PanelOption>) {}, [] {});

    expect(fx->transport->connect_calls.size() == 1, "fetch_panels() connects the transport exactly once");
    expect(fx->transport->connect_calls[0] == "ws://ha.local:8123/api/websocket",
           "the transport is connected to the exact ws_url passed to fetch_panels()");

    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    expect(count_occurrences(fx->transport->sent_messages, "\"access_token\":\"token\"") == 1,
           "the auth frame carries the exact access_token passed to fetch_panels()");
}

void test_ready_sends_get_panels_and_parses_the_result() {
    auto fx = make_fixture();

    std::vector<PanelOption> received;
    bool success_called = false;
    fx->controller->fetch_panels(
        "ws://ha.local:8123/api/websocket", "token",
        [&](std::vector<PanelOption> options) {
            success_called = true;
            received = std::move(options);
        },
        [] {});
    reach_ready(*fx);

    expect(count_occurrences(fx->transport->sent_messages, "\"type\":\"get_panels\"") == 1,
           "reaching Ready sends exactly one get_panels request");

    reply_get_panels(*fx, 1, kOnePanel);
    expect(success_called, "on_success fires once the get_panels result arrives");
    expect(received.size() == 1 && received[0].title == "Home" && received[0].url_path == "lovelace",
           "the parsed panel list matches PanelListParser's own output for the same JSON");
}

void test_auth_failure_calls_on_error() {
    auto fx = make_fixture();

    bool error_fired = false;
    fx->controller->fetch_panels(
        "ws://ha.local:8123/api/websocket", "bad-token", [](std::vector<PanelOption>) {}, [&] { error_fired = true; });
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_invalid"})", true);

    expect(error_fired, "an auth_invalid response before any fetch calls on_error");
}

void test_failed_get_panels_calls_on_error() {
    auto fx = make_fixture();

    bool error_fired = false;
    fx->controller->fetch_panels(
        "ws://ha.local:8123/api/websocket", "token", [](std::vector<PanelOption>) {}, [&] { error_fired = true; });
    reach_ready(*fx);

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"result","success":false,"error":{"code":"unknown_error","message":"nope"}})", true);
    expect(error_fired, "a failed get_panels Result calls on_error");
}

void test_empty_panel_list_is_not_an_error() {
    auto fx = make_fixture();

    bool success_called = false;
    bool error_fired = false;
    fx->controller->fetch_panels(
        "ws://ha.local:8123/api/websocket", "token",
        [&](std::vector<PanelOption> options) {
            success_called = true;
            expect(options.empty(), "a server with genuinely zero dashboards yields an empty (not null) list");
        },
        [&] { error_fired = true; });
    reach_ready(*fx);
    reply_get_panels(*fx, 1, "{}");

    expect(success_called && !error_fired, "an empty result is a successful fetch, not an error");
}

} // namespace

int main() {
    test_connects_and_authenticates_with_the_given_url_and_token();
    test_ready_sends_get_panels_and_parses_the_result();
    test_auth_failure_calls_on_error();
    test_failed_get_panels_calls_on_error();
    test_empty_panel_list_is_not_an_error();

    if (g_failures == 0) {
        std::printf("All SetupController integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
