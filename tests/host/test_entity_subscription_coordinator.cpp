// Contract tests for EntitySubscriptionCoordinator (Phase 5 of
// docs/ARCHITECTURE.md §2). Drives the full stack — FakeWebSocketTransport
// -> WebSocketConnection -> HaClient -> EntityStore/Coordinator — so these
// tests exercise the real dedup/resubscribe/debounce logic, not a mock of it.

#include "../../app/cpp/home_assistant/EntitySubscriptionCoordinator.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <string>

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
    std::unique_ptr<HaClient> client;
    EntityStore store;
    std::unique_ptr<EntitySubscriptionCoordinator> coordinator;
};

std::unique_ptr<Fixture> make_fixture() {
    auto fx = std::make_unique<Fixture>();
    auto transport = std::make_unique<FakeWebSocketTransport>();
    fx->transport = transport.get();
    auto connection = std::make_unique<WebSocketConnection>("ha", std::move(transport), fx->clock);
    fx->client = std::make_unique<HaClient>(std::move(connection), fx->clock, "token");
    fx->coordinator = std::make_unique<EntitySubscriptionCoordinator>(
        fx->store, *fx->client, HaInstanceId{"homeassistant"}, fx->clock, /*unsubscribe_debounce_ms=*/2000);
    return fx;
}

void reach_ready(Fixture& fx) {
    fx.client->start("ws://fake/api/websocket");
    fx.transport->simulate_open();
    fx.transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx.transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    fx.coordinator->on_ha_ready();
}

void reply_to_subscribe(Fixture& fx, int id, bool success = true) {
    std::string result = R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":)" +
                          (success ? "true" : "false") + "}";
    fx.transport->simulate_fragment(result, true);
}

void test_two_local_observers_yield_one_remote_subscription() {
    auto fx = make_fixture();
    reach_ready(*fx);

    int a_calls = 0, b_calls = 0;
    auto handle_a = fx->coordinator->observe("light.x", [&](const EntityState&) { ++a_calls; });
    auto handle_b = fx->coordinator->observe("light.x", [&](const EntityState&) { ++b_calls; });

    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 1,
           "two local observers of the same entity result in exactly one subscribe_entities call");

    reply_to_subscribe(*fx, /*id=*/1);
    expect(fx->coordinator->diagnostics().active_remote_subscriptions == 1,
           "exactly one active remote subscription is tracked after the Result confirms it");

    fx->transport->simulate_fragment(
        R"({"id":1,"type":"event","event":{"a":{"light.x":{"s":"on","a":{},"lc":1000.0}}}})", true);

    expect(a_calls == 1 && b_calls == 1,
           "both local observers are notified independently from the single underlying store record");
}

void test_debounced_unsubscribe() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe("light.x", [](const EntityState&) {});
    reply_to_subscribe(*fx, 1);
    expect(fx->coordinator->diagnostics().active_remote_subscriptions == 1, "subscription is active before drop");

    handle.reset(); // last local observer goes away

    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "unsubscribe_events") == 0,
           "losing the last local observer does not unsubscribe immediately (debounce)");

    fx->clock.advance(2500); // past the 2000ms debounce
    fx->coordinator->poll(fx->clock.now_ms());
    expect(count_occurrences(fx->transport->sent_messages, "unsubscribe_events") == 1,
           "after the debounce window elapses, exactly one unsubscribe_events is sent");
    expect(fx->transport->sent_messages.back().find("\"subscription\":1") != std::string::npos,
           "the unsubscribe references the real subscription id returned by the original Result");
}

void test_new_observer_cancels_pending_unsubscribe() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe("light.x", [](const EntityState&) {});
    reply_to_subscribe(*fx, 1);

    handle.reset(); // schedules a debounced unsubscribe at now+2000

    fx->clock.advance(500);
    auto handle2 = fx->coordinator->observe("light.x", [](const EntityState&) {}); // observed again before debounce fires

    fx->clock.advance(2000); // now well past the ORIGINAL debounce deadline
    fx->coordinator->poll(fx->clock.now_ms());

    expect(count_occurrences(fx->transport->sent_messages, "unsubscribe_events") == 0,
           "a new observer arriving before the debounce fires cancels the pending unsubscribe");
    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 1,
           "re-observing before the debounce fires reuses the still-active subscription, no duplicate subscribe");
}

void test_resubscribe_after_reconnect() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe("light.x", [](const EntityState&) {});
    reply_to_subscribe(*fx, 1);
    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 1, "initial subscribe sent");

    // Disconnect.
    fx->transport->simulate_closed("network blip");
    fx->coordinator->on_ha_disconnected();
    expect(fx->coordinator->diagnostics().active_remote_subscriptions == 0,
           "disconnect drops remote-subscription bookkeeping (the old connection is gone)");
    expect(fx->store.find(EntityKey{HaInstanceId{"homeassistant"}, "light.x"})->state.availability ==
               Availability::stale,
           "disconnect marks the still-locally-observed entity stale in the store");

    // Reconnect.
    fx->clock.advance(60000);
    fx->client->poll(fx->clock.now_ms());
    fx->transport->simulate_open();
    fx->transport->simulate_fragment(R"({"type":"auth_required"})", true);
    fx->transport->simulate_fragment(R"({"type":"auth_ok"})", true);
    fx->coordinator->on_ha_ready();

    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 2,
           "reconnect triggers exactly one fresh subscribe_entities for the still-observed entity");
}

void test_failed_subscribe_does_not_wedge_active_state() {
    auto fx = make_fixture();
    reach_ready(*fx);

    auto handle = fx->coordinator->observe("light.x", [](const EntityState&) {});
    reply_to_subscribe(*fx, 1, /*success=*/false);

    expect(fx->coordinator->diagnostics().active_remote_subscriptions == 0,
           "a failed subscribe_entities does not get recorded as active");

    // A subsequent on_ha_ready() (e.g. after a later successful reconnect)
    // must be able to retry it.
    fx->coordinator->on_ha_ready();
    expect(count_occurrences(fx->transport->sent_messages, "subscribe_entities") == 2,
           "a failed subscription is retried on the next on_ha_ready()");
}

} // namespace

int main() {
    test_two_local_observers_yield_one_remote_subscription();
    test_debounced_unsubscribe();
    test_new_observer_cancels_pending_unsubscribe();
    test_resubscribe_after_reconnect();
    test_failed_subscribe_does_not_wedge_active_state();

    if (g_failures == 0) {
        std::printf("All EntitySubscriptionCoordinator contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
