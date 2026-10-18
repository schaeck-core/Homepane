// Contract tests for MaClient — Music Assistant's own auth handshake
// (schema-gated, command/result-based, username/password with automatic
// cached-token-first-then-fallback) and generic send_command. No LVGL, no
// real sockets — FakeWebSocketTransport drives the scenario, same pattern
// as test_ha_client.cpp.

#include "../../app/cpp/music_assistant/MaClient.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <vector>

using app::core::IMonotonicClock;
using app::music_assistant::MaClient;
using app::music_assistant::MaClientState;
using app::music_assistant::MaCredentials;
using app::music_assistant::MaResultMessage;
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
    FakeWebSocketTransport* transport; // owned by the connection, owned by the client
    std::unique_ptr<MaClient> client;
};

Fixture make_fixture(std::string username = "alice", std::string password = "secret",
                      std::string cached_token = "") {
    auto transport = std::make_unique<FakeWebSocketTransport>();
    FakeWebSocketTransport* raw = transport.get();
    Fixture fx;
    fx.transport = raw;
    auto connection = std::make_unique<WebSocketConnection>("music_assistant", std::move(transport), fx.clock);
    MaCredentials creds;
    creds.username = std::move(username);
    creds.password = std::move(password);
    fx.client = std::make_unique<MaClient>(std::move(connection), fx.clock, creds, std::move(cached_token));
    return fx;
}

constexpr const char* kServerInfoAuthRequired =
    R"({"server_id":"abc","server_version":"2.6.0","schema_version":29,"min_supported_schema_version":25})";
constexpr const char* kServerInfoNoAuthRequired =
    R"({"server_id":"abc","server_version":"1.9.0","schema_version":20,"min_supported_schema_version":15})";

void test_old_schema_requires_no_auth() {
    auto fx = make_fixture();
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoNoAuthRequired, true);

    expect(fx.client->state() == MaClientState::Ready, "a schema < 28 ServerInfo moves straight to Ready");
    expect(fx.transport->sent_messages.empty(), "no auth/login command is sent for a pre-28 server");
}

void test_cached_token_success() {
    auto fx = make_fixture("alice", "secret", "cached-token-123");
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoAuthRequired, true);

    expect(fx.client->state() == MaClientState::Authenticating, "schema >= 28 moves to Authenticating");
    expect(count_occurrences(fx.transport->sent_messages, "\"command\":\"auth\"") == 1,
           "a cached token tries the plain 'auth' command first, not auth/login");
    expect(count_occurrences(fx.transport->sent_messages, "\"token\":\"cached-token-123\"") == 1,
           "the auth frame carries the exact cached token");
    expect(count_occurrences(fx.transport->sent_messages, "\"message_id\":\"1\"") == 1,
           "message_id is sent as a JSON string, not a bare number");

    fx.transport->simulate_fragment(R"({"message_id":"1","result":{}})", true);
    expect(fx.client->state() == MaClientState::Ready, "a successful token auth reaches Ready");
}

void test_cached_token_rejected_falls_back_to_login() {
    auto fx = make_fixture("alice", "secret", "stale-token");
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoAuthRequired, true);
    fx.transport->simulate_fragment(R"({"message_id":"1","error_code":401,"details":"expired"})", true);

    expect(fx.client->state() == MaClientState::Authenticating,
           "a rejected cached token is not itself a failure — still Authenticating, not AuthFailed");
    expect(count_occurrences(fx.transport->sent_messages, "\"command\":\"auth/login\"") == 1,
           "a rejected token silently falls back to auth/login, with no caller-visible error");
    expect(count_occurrences(fx.transport->sent_messages, "\"username\":\"alice\"") == 1,
           "the fallback login carries the configured username");
    expect(count_occurrences(fx.transport->sent_messages, "\"password\":\"secret\"") == 1,
           "the fallback login carries the configured password");

    // auth/login only issues a fresh access_token — confirmed against a
    // real server that it does NOT itself authenticate the connection —
    // so MaClient must still send a follow-up "auth" with it.
    fx.transport->simulate_fragment(R"({"message_id":"2","result":{"access_token":"fresh-token"}})", true);
    expect(fx.client->state() == MaClientState::Authenticating,
           "a successful auth/login alone does not yet reach Ready");
    expect(count_occurrences(fx.transport->sent_messages, "\"command\":\"auth\"") == 2,
           "a successful login is followed by an explicit 'auth' with the freshly issued token "
           "(the cached-token attempt was the first 'auth', this is the second)");
    expect(count_occurrences(fx.transport->sent_messages, "\"token\":\"fresh-token\"") == 1,
           "the follow-up auth carries the exact token the login just returned");

    fx.transport->simulate_fragment(R"({"message_id":"3","result":{}})", true);
    expect(fx.client->state() == MaClientState::Ready, "the follow-up auth succeeding reaches Ready");
}

void test_login_success_stores_new_token() {
    auto fx = make_fixture();
    bool token_issued = false;
    std::string issued_token;
    fx.client->set_on_token_issued([&](const std::string& token) {
        token_issued = true;
        issued_token = token;
    });
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoAuthRequired, true);

    expect(count_occurrences(fx.transport->sent_messages, "\"command\":\"auth/login\"") == 1,
           "no cached token means auth/login is sent immediately, no wasted 'auth' attempt");

    // Real field name confirmed against a live server: "access_token", not
    // the earlier speculative "token".
    fx.transport->simulate_fragment(R"({"message_id":"1","result":{"access_token":"brand-new-token"}})", true);
    expect(fx.client->state() == MaClientState::Authenticating,
           "a successful login alone does not yet reach Ready — the follow-up auth is still pending");
    expect(token_issued, "on_token_issued fires as soon as auth/login returns an access_token");
    expect(issued_token == "brand-new-token", "on_token_issued receives the exact access_token from the result");
    expect(count_occurrences(fx.transport->sent_messages, "\"command\":\"auth\"") == 1,
           "the login is immediately followed by an explicit auth with the new token");

    fx.transport->simulate_fragment(R"({"message_id":"2","result":{}})", true);
    expect(fx.client->state() == MaClientState::Ready, "the follow-up auth succeeding reaches Ready");
}

void test_login_success_but_no_access_token_is_auth_failed() {
    // Defensive case: if the server's login response ever lacks
    // access_token (unexpected shape), MaClient must fail cleanly rather
    // than silently "succeeding" with an empty token.
    auto fx = make_fixture();
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoAuthRequired, true);
    fx.transport->simulate_fragment(R"({"message_id":"1","result":{"user":{"username":"alice"}}})", true);

    expect(fx.client->state() == MaClientState::AuthFailed,
           "a successful auth/login result with no access_token field is treated as AuthFailed");
}

void test_login_failure_sets_auth_failed() {
    auto fx = make_fixture();
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoAuthRequired, true);
    fx.transport->simulate_fragment(R"({"message_id":"1","error_code":401,"details":"invalid credentials"})", true);

    expect(fx.client->state() == MaClientState::AuthFailed,
           "a rejected username/password login (not a cached-token attempt) sets AuthFailed");
    expect(fx.transport->close_calls >= 1, "AuthFailed closes the connection so it does not auto-retry bad credentials");
}

void test_command_not_ready_fails_immediately() {
    auto fx = make_fixture();
    bool called = false;
    MaResultMessage received;
    fx.client->send_command("player_queues/play", R"({"queue_id":"p1"})", [&](const MaResultMessage& r) {
        called = true;
        received = r;
    });
    expect(called, "send_command() before Ready invokes the callback immediately");
    expect(received.error_message == "not_ready", "the immediate failure is reported as not_ready");
    expect(fx.transport->sent_messages.empty(), "nothing is sent to the transport while not Ready");
}

void reach_ready_no_auth(Fixture& fx) {
    fx.client->start("ws://ma.local:8095/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(kServerInfoNoAuthRequired, true);
}

void test_command_result_correlation() {
    auto fx = make_fixture();
    reach_ready_no_auth(fx);

    bool called = false;
    MaResultMessage received;
    fx.client->send_command("player_queues/play", R"({"queue_id":"p1"})", [&](const MaResultMessage& r) {
        called = true;
        received = r;
    });

    expect(fx.transport->sent_messages.size() == 1, "send_command() sends exactly one message once Ready");
    expect(fx.transport->sent_messages[0].find("\"command\":\"player_queues/play\"") != std::string::npos,
           "the outgoing message carries the exact command name");
    expect(fx.transport->sent_messages[0].find("\"queue_id\":\"p1\"") != std::string::npos,
           "the outgoing message carries the exact args");
    expect(fx.transport->sent_messages[0].find("\"message_id\":\"1\"") != std::string::npos,
           "MaClient injects a fresh string message_id into the outgoing command");

    fx.transport->simulate_fragment(R"({"message_id":"1","result":{"queue_id":"p1"}})", true);
    expect(called, "a matching Result completes the pending command");
    expect(received.success, "the completed result reports success");
    expect(received.result_json == R"({"queue_id":"p1"})", "the result payload is passed through as owned JSON text");
}

void test_command_timeout() {
    auto fx = make_fixture();
    reach_ready_no_auth(fx);

    bool called = false;
    MaResultMessage received;
    fx.client->send_command(
        "player_queues/play", "{}", [&](const MaResultMessage& r) { called = true; received = r; },
        /*timeout_ms=*/500);

    fx.clock.advance(600);
    fx.client->poll(fx.clock.now_ms());

    expect(called, "an expired command fires its callback via poll()");
    expect(received.error_message == "timeout", "the timeout is reported as such, distinct from a real MA error");
}

void test_disconnect_fails_pending_command() {
    auto fx = make_fixture();
    reach_ready_no_auth(fx);

    bool called = false;
    MaResultMessage received;
    fx.client->send_command("player_queues/pause", "{}", [&](const MaResultMessage& r) {
        called = true;
        received = r;
    });

    fx.transport->simulate_closed("network blip");

    expect(called, "disconnect fails any still-pending command rather than leaving it hanging forever");
    expect(received.error_message == "disconnected", "the disconnect failure is reported as such");
}

} // namespace

int main() {
    test_old_schema_requires_no_auth();
    test_cached_token_success();
    test_cached_token_rejected_falls_back_to_login();
    test_login_success_stores_new_token();
    test_login_success_but_no_access_token_is_auth_failed();
    test_login_failure_sets_auth_failed();
    test_command_not_ready_fails_immediately();
    test_command_result_correlation();
    test_command_timeout();
    test_disconnect_fails_pending_command();

    if (g_failures == 0) {
        std::printf("All MaClient contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
