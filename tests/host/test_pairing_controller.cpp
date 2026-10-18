// Integration test for PairingController (CPP_MIGRATION_PLAN.md Phase
// 10's last consumer, the OAuth device-pairing flow's WebSocket half) —
// the full real stack (FakeWebSocketTransport -> WebSocketConnection ->
// HaClient) through the same entry point app_facade.cpp uses live. No
// LVGL: this class never touches a widget tree.

#include "app/cpp/application/PairingController.h"
#include "app/cpp/home_assistant/HaUrl.h"
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
    std::unique_ptr<PairingController> controller;
};

std::unique_ptr<Fixture> make_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    fx->controller = std::make_unique<PairingController>(fx->clock, nullptr, std::move(transport));
    return fx;
}

void reach_ready(Fixture& fx) {
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok"})", true);
}

void reply_token_request(Fixture& fx, int id, const std::string& result_json) {
    fx.transport->simulate_fragment(
        R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":true,"result":)" + result_json + "}", true);
}

void test_connects_and_authenticates_with_the_given_url_and_token() {
    auto fx = make_fixture();

    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "short-lived-token", "LVGL 192.0.2.105", [](std::string) {}, [](std::string) {});

    expect(fx->transport->connect_calls.size() == 1, "exchange_long_lived_token() connects the transport exactly once");
    expect(fx->transport->connect_calls[0] == "ws://ha.local:8123/api/websocket",
           "the transport is connected to the exact ws_url passed in");

    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    expect(count_occurrences(fx->transport->sent_messages, "\"access_token\":\"short-lived-token\"") == 1,
           "the auth frame carries the exact (short-lived) access_token passed in, not any long-lived token");
}

void test_ready_requests_long_lived_token_and_returns_it() {
    auto fx = make_fixture();

    bool success_called = false;
    std::string received_token;
    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "short-lived-token", "LVGL 192.0.2.105",
        [&](std::string token) {
            success_called = true;
            received_token = std::move(token);
        },
        [](std::string) {});
    reach_ready(*fx);

    expect(count_occurrences(fx->transport->sent_messages, "\"type\":\"auth/long_lived_access_token\"") == 1,
           "reaching Ready sends exactly one auth/long_lived_access_token request");
    expect(count_occurrences(fx->transport->sent_messages, "\"client_name\":\"LVGL 192.0.2.105\"") == 1,
           "the request carries the exact client_name passed in");
    expect(count_occurrences(fx->transport->sent_messages, "\"lifespan\":3650") == 1,
           "the request asks for a 3650-day (~10 year) lifespan, matching the legacy convention");

    reply_token_request(*fx, 1, R"("the-new-long-lived-token")");
    expect(success_called, "on_success fires once the auth/long_lived_access_token result arrives");
    expect(received_token == "the-new-long-lived-token", "on_success receives the exact token string from the result");
}

void test_auth_failure_calls_on_error() {
    auto fx = make_fixture();

    bool error_fired = false;
    bool success_fired = false;
    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "bad-token", "LVGL 192.0.2.105", [&](std::string) { success_fired = true; },
        [&](std::string) { error_fired = true; });
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_invalid"})", true);

    expect(error_fired, "an auth_invalid response calls on_error");
    expect(!success_fired, "on_success never fires when auth fails");
}

void test_failed_token_request_calls_on_error() {
    auto fx = make_fixture();

    bool error_fired = false;
    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "short-lived-token", "LVGL 192.0.2.105", [](std::string) {},
        [&](std::string) { error_fired = true; });
    reach_ready(*fx);

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"result","success":false,"error":{"code":"unknown_error","message":"nope"}})", true);
    expect(error_fired, "a failed auth/long_lived_access_token result calls on_error");
}

void test_malformed_result_calls_on_error_not_crash() {
    auto fx = make_fixture();

    bool error_fired = false;
    bool success_fired = false;
    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "short-lived-token", "LVGL 192.0.2.105",
        [&](std::string) { success_fired = true; }, [&](std::string) { error_fired = true; });
    reach_ready(*fx);

    // HA's real response for this command is a JSON string, not an object —
    // a malformed/unexpected shape must fail cleanly, not crash or silently
    // "succeed" with garbage.
    reply_token_request(*fx, 1, "{}");
    expect(error_fired, "a non-string result calls on_error");
    expect(!success_fired, "on_success never fires for a malformed result");
}

void test_ha_base_url_is_converted_to_websocket_endpoint() {
    // app_facade.cpp feeds the user-facing haas_url through this before
    // connecting; passing "http://host:8123" straight to the transport made
    // the WS handshake fail and pairing hang forever on real hardware.
    using app::home_assistant::to_websocket_url;
    expect(to_websocket_url("http://192.0.2.30:8123") == "ws://192.0.2.30:8123/api/websocket",
           "http://host:port becomes ws://host:port/api/websocket");
    expect(to_websocket_url("https://ha.example.com/") == "wss://ha.example.com/api/websocket",
           "https with trailing slash becomes wss://.../api/websocket");
    expect(to_websocket_url("ws://ha.local:8123/api/websocket") == "ws://ha.local:8123/api/websocket",
           "an already-complete ws URL is left unchanged (idempotent)");
    expect(to_websocket_url("ha.local:8123") == "ws://ha.local:8123/api/websocket",
           "a URL without scheme is treated as plain http");
}

void test_connection_failure_calls_on_error_and_closes() {
    auto fx = make_fixture();

    bool error_fired = false;
    bool success_fired = false;
    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "short-lived-token", "LVGL 192.0.2.105",
        [&](std::string) { success_fired = true; }, [&](std::string) { error_fired = true; });
    fx->transport->simulate_error("connect failed");

    expect(error_fired, "a failed connect calls on_error instead of retrying silently forever");
    expect(!success_fired, "on_success never fires after a failed connect");

    int closes_before = fx->transport->close_calls;
    fx->clock.advance(60000);
    fx->controller->poll(fx->clock.now_ms());
    expect(fx->transport->connect_calls.size() == 1, "no automatic reconnect attempt after the one-shot failure");
    expect(fx->transport->close_calls > closes_before, "the one-shot connection is closed on the next poll");
}

void test_hung_exchange_times_out() {
    auto fx = make_fixture();

    int error_count = 0;
    fx->controller->exchange_long_lived_token(
        "ws://ha.local:8123/api/websocket", "short-lived-token", "LVGL 192.0.2.105", [](std::string) {},
        [&](std::string) { ++error_count; });
    fx->transport->simulate_open(); // connected, but HA never answers

    fx->clock.advance(5000);
    fx->controller->poll(fx->clock.now_ms());
    expect(error_count == 0, "no error while still within the timeout");

    fx->clock.advance(30000);
    fx->controller->poll(fx->clock.now_ms());
    fx->controller->poll(fx->clock.now_ms());
    expect(error_count == 1, "a hung exchange calls on_error exactly once after the timeout");
}

} // namespace

int main() {
    test_connects_and_authenticates_with_the_given_url_and_token();
    test_ready_requests_long_lived_token_and_returns_it();
    test_auth_failure_calls_on_error();
    test_failed_token_request_calls_on_error();
    test_malformed_result_calls_on_error_not_crash();
    test_ha_base_url_is_converted_to_websocket_endpoint();
    test_connection_failure_calls_on_error_and_closes();
    test_hung_exchange_times_out();

    if (g_failures == 0) {
        std::printf("All PairingController integration tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
