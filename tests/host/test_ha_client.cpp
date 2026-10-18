// Contract tests for HaClient (Phase 4 of CPP_MIGRATION_PLAN.md §7).
// No LVGL, no real sockets — FakeWebSocketTransport drives the scenario.

#include "../../app/cpp/home_assistant/HaClient.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <vector>

using app::core::IMonotonicClock;
using app::home_assistant::HaClient;
using app::home_assistant::HaClientState;
using app::home_assistant::ResultMessage;
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

struct Fixture {
    FakeClock clock;
    FakeWebSocketTransport* transport; // owned by the connection, owned by the client
    std::unique_ptr<HaClient> client;
};

Fixture make_fixture(std::string token = "test-token") {
    auto transport = std::make_unique<FakeWebSocketTransport>();
    FakeWebSocketTransport* raw = transport.get();
    Fixture fx;
    fx.transport = raw;
    auto connection = std::make_unique<WebSocketConnection>("ha", std::move(transport), fx.clock);
    fx.client = std::make_unique<HaClient>(std::move(connection), fx.clock, std::move(token));
    return fx;
}

void reach_ready(Fixture& fx) {
    fx.client->start("ws://fake/api/websocket");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required","ha_version":"2026.9.1"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok","ha_version":"2026.9.1"})", true);
}

void test_auth_success_flow() {
    auto fx = make_fixture();
    std::vector<HaClientState> states;
    fx.client->set_on_state_change([&](HaClientState s) { states.push_back(s); });

    fx.client->start("ws://fake/api/websocket");
    expect(fx.transport->connect_calls.size() == 1, "start() connects the underlying transport");

    fx.transport->simulate_open();
    expect(fx.client->state() == HaClientState::TransportConnected,
           "transport open moves HaClient to TransportConnected, not straight to Ready");
    // Proactive auth: on ESP32 HA's auth_required can be held back by
    // esp_websocket_client until HA's 10 s auth timeout closes the socket,
    // so the client must not wait for it.
    expect(fx.transport->sent_messages.size() == 1, "transport open sends the auth message right away");

    fx.transport->simulate_fragment(R"({"type":"auth_required","ha_version":"2026.9.1"})", true);
    expect(fx.client->state() == HaClientState::Authenticating, "auth_required moves HaClient to Authenticating");
    expect(fx.transport->sent_messages.size() == 1,
           "a later auth_required does not trigger a second auth message (exactly one per connection)");
    expect(fx.transport->sent_messages[0].find("\"type\":\"auth\"") != std::string::npos,
           "the outgoing message is an auth message");
    expect(fx.transport->sent_messages[0].find("test-token") != std::string::npos,
           "the outgoing auth message carries the configured access token");

    fx.transport->simulate_fragment(R"({"type":"auth_ok","ha_version":"2026.9.1"})", true);
    expect(fx.client->state() == HaClientState::Ready, "auth_ok moves HaClient to Ready");
}

void test_auth_resent_once_per_new_connection() {
    auto fx = make_fixture();
    fx.client->start("ws://fake/api/websocket");
    fx.transport->simulate_open();
    fx.transport->simulate_closed("dropped");
    fx.clock.advance(60000);
    fx.client->poll(fx.clock.now_ms());
    fx.transport->simulate_open();
    int auths = 0;
    for (const auto& m : fx.transport->sent_messages) if (m.find("\"type\":\"auth\"") != std::string::npos) ++auths;
    expect(auths == 2, "each new connection sends its own auth exactly once");
}

void test_auth_invalid_stops_retrying() {
    auto fx = make_fixture();
    fx.client->start("ws://fake/api/websocket");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_invalid","message":"Invalid access token"})", true);

    expect(fx.client->state() == HaClientState::AuthFailed, "auth_invalid moves HaClient to AuthFailed");
    expect(fx.transport->close_calls >= 1,
           "auth_invalid closes the underlying connection so it does not auto-retry a bad token");
}

void test_command_not_ready_fails_immediately() {
    auto fx = make_fixture();
    bool called = false;
    ResultMessage received;
    fx.client->send_command(R"({"type":"ping"})", [&](const ResultMessage& r) {
        called = true;
        received = r;
    });
    expect(called, "send_command() before Ready invokes the callback immediately");
    expect(received.error_code == "not_ready", "the immediate failure is reported as not_ready");
    expect(fx.transport->sent_messages.empty(), "nothing is sent to the transport while not Ready");
}

void test_command_result_correlation() {
    auto fx = make_fixture();
    reach_ready(fx);

    bool called = false;
    ResultMessage received;
    fx.client->send_command(R"({"type":"lovelace/config"})", [&](const ResultMessage& r) {
        called = true;
        received = r;
    });

    // reach_ready() already sent the auth message, so this is the second
    // message overall — the actual command.
    expect(fx.transport->sent_messages.size() == 2, "send_command() sends exactly one more message once Ready");
    expect(fx.transport->sent_messages[1].find("\"id\":1") != std::string::npos,
           "HaClient injects a fresh numeric id into the outgoing command");

    fx.transport->simulate_fragment(R"({"id":1,"type":"result","success":true,"result":{"ok":true}})", true);
    expect(called, "a matching Result completes the pending command");
    expect(received.success, "the completed result reports success");
    expect(received.result_json == "{\"ok\":true}", "the result payload is passed through as owned JSON text");
}

void test_command_timeout() {
    auto fx = make_fixture();
    reach_ready(fx);

    bool called = false;
    ResultMessage received;
    fx.client->send_command(
        R"({"type":"ping"})", [&](const ResultMessage& r) { called = true; received = r; }, /*timeout_ms=*/500);

    fx.clock.advance(600);
    fx.client->poll(fx.clock.now_ms());

    expect(called, "an expired command fires its callback via poll()");
    expect(received.error_code == "timeout", "the timeout is reported as such, distinct from a real HA error");
}

void test_disconnect_fails_pending_command() {
    auto fx = make_fixture();
    reach_ready(fx);

    bool called = false;
    ResultMessage received;
    fx.client->send_command(R"({"type":"ping"})", [&](const ResultMessage& r) {
        called = true;
        received = r;
    });

    fx.transport->simulate_closed("network blip");

    expect(called, "disconnect fails any still-pending command rather than leaving it hanging forever");
    expect(received.error_code == "disconnected", "the disconnect failure is reported as such");
}

void test_cancellation_suppresses_callback() {
    auto fx = make_fixture();
    reach_ready(fx);

    int call_count = 0;
    auto source = fx.client->send_command(R"({"type":"ping"})", [&](const ResultMessage&) { ++call_count; });
    source.cancel();

    fx.transport->simulate_fragment(R"({"id":1,"type":"result","success":true,"result":{}})", true);

    expect(call_count == 0, "a cancelled command's callback never fires, even if a late Result arrives");
}

void test_id_reuse_after_reconnect_does_not_cross_fire() {
    auto fx = make_fixture();
    reach_ready(fx);

    int first_call_count = 0;
    ResultMessage first_result;
    fx.client->send_command(R"({"type":"ping"})", [&](const ResultMessage& r) {
        ++first_call_count;
        first_result = r;
    }); // gets id 1

    // Disconnect: the pending id-1 request must fail exactly once, right now.
    fx.transport->simulate_closed("network blip");
    expect(first_call_count == 1, "the pre-disconnect command fails exactly once, at disconnect time");
    expect(first_result.error_code == "disconnected", "...and is reported as disconnected");

    // Reconnect (id counter resets to 1 on the new epoch) and reach Ready again.
    fx.clock.advance(60000);
    fx.client->poll(fx.clock.now_ms());
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    expect(fx.client->state() == HaClientState::Ready, "client reaches Ready again after reconnect");

    int second_call_count = 0;
    ResultMessage second_result;
    fx.client->send_command(R"({"type":"ping"})", [&](const ResultMessage& r) {
        ++second_call_count;
        second_result = r;
    }); // also gets id 1, in the new epoch

    fx.transport->simulate_fragment(R"({"id":1,"type":"result","success":true,"result":{}})", true);

    expect(first_call_count == 1, "the old (already-failed) id-1 request is not fired a second time");
    expect(second_call_count == 1, "the new id-1 request (post-reconnect) completes normally");
    expect(second_result.success, "the new request's result is the real success result, not a stale one");
}

} // namespace

int main() {
    test_auth_success_flow();
    test_auth_resent_once_per_new_connection();
    test_auth_invalid_stops_retrying();
    test_command_not_ready_fails_immediately();
    test_command_result_correlation();
    test_command_timeout();
    test_disconnect_fails_pending_command();
    test_cancellation_suppresses_callback();
    test_id_reuse_after_reconnect_does_not_cross_fire();

    if (g_failures == 0) {
        std::printf("All HaClient contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
