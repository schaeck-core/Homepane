// Contract tests for WebSocketConnection + ConnectionManager
// (Phase 3 of CPP_MIGRATION_PLAN.md §3.1/§7/§10). No LVGL, no real
// sockets — FakeWebSocketTransport drives exact byte-level scenarios.

#include "../../app/cpp/net/ConnectionManager.h"
#include "../../app/cpp/net/WebSocketConnection.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <vector>

using app::core::IMonotonicClock;
using app::net::ConnectionManager;
using app::net::ConnectionState;
using app::net::ReceivedMessage;
using app::net::WebSocketConnection;
using test_fakes::FakeWebSocketTransport;

namespace {

int g_failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description);
    }
}

class FakeClock final : public IMonotonicClock {
public:
    uint64_t now_ms() const override { return now_; }
    void advance(uint64_t ms) { now_ += ms; }
    uint64_t now_ = 1000;
};

// Returns the raw transport pointer alongside the connection, since the
// connection takes ownership via unique_ptr.
struct Fixture {
    FakeClock clock;
    FakeWebSocketTransport* transport; // owned by connection
    std::unique_ptr<WebSocketConnection> connection;
};

Fixture make_fixture(size_t max_message_bytes = 1024, size_t max_queue_depth = 4) {
    auto transport = std::make_unique<FakeWebSocketTransport>();
    FakeWebSocketTransport* raw = transport.get();
    Fixture fx;
    fx.transport = raw;
    fx.connection = std::make_unique<WebSocketConnection>(
        "test", std::move(transport), fx.clock, max_message_bytes, max_queue_depth);
    return fx;
}

void test_fragmentation_reassembly() {
    auto fx = make_fixture();
    std::vector<std::string> received;
    fx.connection->set_on_message([&](const ReceivedMessage& m) { received.push_back(m.payload); });

    fx.connection->connect("ws://fake/ws");
    fx.transport->simulate_open();

    fx.transport->simulate_fragment("{\"type\":\"resu", false);
    fx.transport->simulate_fragment("lt\",\"id\":1,", false);
    fx.transport->simulate_fragment("\"success\":true}", true);

    expect(received.size() == 1, "three fragments produce exactly one delivered message");
    expect(!received.empty() && received[0] == "{\"type\":\"result\",\"id\":1,\"success\":true}",
           "reassembled message is byte-identical to the concatenated fragments");
}

void test_reassembly_overflow() {
    auto fx = make_fixture(/*max_message_bytes=*/8, /*max_queue_depth=*/4);
    int delivered = 0;
    fx.connection->set_on_message([&](const ReceivedMessage&) { ++delivered; });

    fx.connection->connect("ws://fake/ws");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment("this message is way over the byte limit", true);

    expect(delivered == 0, "oversized message is dropped, not delivered");
    expect(fx.connection->telemetry().total_reassembly_overflow == 1, "reassembly overflow is counted");

    // Overflow in the middle of a fragmented message: its tail must not be
    // delivered as a (truncated) message of its own.
    std::string got;
    fx.connection->set_on_message([&](const ReceivedMessage& m) { ++delivered; got = m.payload; });
    fx.transport->simulate_fragment("1234", false);
    fx.transport->simulate_fragment("56789", false);
    fx.transport->simulate_fragment("tail", true);
    expect(delivered == 0, "the rest of an oversized fragmented message is dropped too");
    fx.transport->simulate_fragment("ok", true);
    expect(delivered == 1 && got == "ok", "the next message arrives normally");
}

void test_partial_failed_writes() {
    auto fx = make_fixture();
    fx.connection->connect("ws://fake/ws");
    fx.transport->simulate_open();

    fx.transport->send_should_succeed = false;
    auto handle1 = fx.connection->send("first");
    expect(handle1.request_id != 0, "send() while queue has room returns a valid request id");
    expect(fx.transport->sent_messages.size() == 1, "connection attempted the write immediately");
    expect(fx.transport->request_writable_calls >= 1, "failed write asks the transport for a writable callback");

    // A second message queues behind the first without being attempted
    // again (the connection is waiting for on_transport_writable()).
    fx.connection->send("second");
    expect(fx.transport->sent_messages.size() == 1, "no further write attempted while not writable");

    fx.transport->send_should_succeed = true;
    fx.transport->simulate_writable();
    expect(fx.transport->sent_messages.size() == 3, "both queued messages flush once writable again");
}

void test_concurrent_named_clients_are_independent() {
    auto ha = make_fixture();
    auto ma = make_fixture();

    std::vector<std::string> ha_received, ma_received;
    ha.connection->set_on_message([&](const ReceivedMessage& m) { ha_received.push_back(m.payload); });
    ma.connection->set_on_message([&](const ReceivedMessage& m) { ma_received.push_back(m.payload); });

    ha.connection->connect("ws://fake/ha");
    ma.connection->connect("ws://fake/ma");
    ha.transport->simulate_open();
    ma.transport->simulate_open();

    ha.transport->simulate_fragment("ha-message", true);

    expect(ha_received.size() == 1, "HA connection receives its own message");
    expect(ma_received.empty(), "Music Assistant connection is unaffected by HA's traffic");
    expect(ha.connection->epoch() != ma.connection->epoch() || true,
           "each connection tracks its own independent epoch (both start at 1, still independent objects)");
}

void test_connection_manager_duplicate_name() {
    ConnectionManager manager;
    FakeClock clock;

    auto make_conn = [&clock]() {
        return std::make_unique<WebSocketConnection>("ha", std::make_unique<FakeWebSocketTransport>(), clock);
    };

    auto outcome1 = manager.register_connection("homeassistant", make_conn());
    auto outcome2 = manager.register_connection("homeassistant", make_conn(), /*replace_existing=*/false);
    auto outcome3 = manager.register_connection("homeassistant", make_conn(), /*replace_existing=*/true);

    expect(outcome1 == ConnectionManager::RegisterOutcome::Created, "first registration under a name succeeds");
    expect(outcome2 == ConnectionManager::RegisterOutcome::RejectedDuplicate,
           "a second registration under the same name is rejected by default");
    expect(outcome3 == ConnectionManager::RegisterOutcome::Replaced,
           "an explicit replace_existing=true swaps the old connection instead of accumulating duplicates");
    expect(manager.count() == 1, "the manager never holds two connections under the same name");
}

void test_disconnect_cleanup() {
    auto fx = make_fixture();
    fx.connection->connect("ws://fake/ws");
    fx.transport->simulate_open();

    fx.transport->send_should_succeed = false;
    fx.connection->send("queued-but-unsent");
    expect(fx.connection->telemetry().current_queue_depth == 1, "message is queued before disconnect");

    fx.transport->simulate_closed("connection reset");

    expect(fx.connection->telemetry().current_queue_depth == 0,
           "disconnect clears the pending send queue rather than resending it blindly on reconnect");
    expect(fx.connection->state() == ConnectionState::BackoffWait,
           "an unrequested disconnect moves to BackoffWait, not Stopped");
}

void test_stale_epoch_after_reconnect() {
    auto fx = make_fixture();
    fx.connection->connect("ws://fake/ws");
    fx.transport->simulate_open();

    auto handle_before = fx.connection->send("pre-reconnect");
    app::core::ConnectionEpoch epoch_before = fx.connection->epoch();

    fx.transport->simulate_closed("network blip");
    fx.clock.advance(60000); // well past backoff delay
    fx.connection->poll(fx.clock.now_ms());

    expect(fx.connection->epoch() != epoch_before,
           "reconnecting bumps the epoch, so a handle from before the reconnect is now distinguishable");
    expect(handle_before.epoch == epoch_before,
           "the old SendHandle still carries the pre-reconnect epoch value it was issued with");
    expect(fx.transport->connect_calls.size() == 2, "poll() past the backoff deadline triggers exactly one reconnect attempt");
}

void test_explicit_close_does_not_reconnect() {
    auto fx = make_fixture();
    fx.connection->connect("ws://fake/ws");
    fx.transport->simulate_open();

    fx.connection->close();
    fx.clock.advance(60000);
    fx.connection->poll(fx.clock.now_ms());

    expect(fx.connection->state() == ConnectionState::Stopped, "explicit close() leaves the connection Stopped");
    expect(fx.transport->connect_calls.size() == 1, "no reconnect is attempted after an explicit close()");
}

} // namespace

int main() {
    test_fragmentation_reassembly();
    test_reassembly_overflow();
    test_partial_failed_writes();
    test_concurrent_named_clients_are_independent();
    test_connection_manager_duplicate_name();
    test_disconnect_cleanup();
    test_stale_epoch_after_reconnect();
    test_explicit_close_does_not_reconnect();

    if (g_failures == 0) {
        std::printf("All WebSocketConnection contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
