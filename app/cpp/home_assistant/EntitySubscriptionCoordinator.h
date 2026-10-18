#ifndef APP_CPP_HOME_ASSISTANT_ENTITYSUBSCRIPTIONCOORDINATOR_H
#define APP_CPP_HOME_ASSISTANT_ENTITYSUBSCRIPTIONCOORDINATOR_H

#include "../core/IMonotonicClock.h"
#include "EntityStore.h"
#include "HaClient.h"

#include <cstdint>
#include <map>
#include <string>

namespace app::home_assistant {

// Bridges local widget demand (EntityStore::observe) and the network
// (HaClient): N local observers of the same entity collapse to at most
// one active `subscribe_entities` call per connection/epoch
// (docs/ARCHITECTURE.md §2 EntitySubscriptionCoordinator).
//
// Deliberate deviation from ARCHITECTURE.md §6's literal wording ("HaClient markiert
// ... stale"): HaClient stays decoupled from EntityStore (it only knows
// WebSocketConnection + decoded messages), so it is this Coordinator —
// which already holds both — that reacts to HaClient's state changes and
// calls EntityStore::mark_all_stale(). The owner of both HaClient and
// this Coordinator (a future Application/DashboardSession, not built
// until a later phase) is expected to wire:
//   ha_client.set_on_state_change([&](HaClientState s) {
//       if (s == HaClientState::Ready) coordinator.on_ha_ready();
//       else if (s != HaClientState::Connecting &&
//                s != HaClientState::TransportConnected &&
//                s != HaClientState::Authenticating) coordinator.on_ha_disconnected();
//   });
class EntitySubscriptionCoordinator {
public:
    EntitySubscriptionCoordinator(EntityStore& store, HaClient& ha_client, HaInstanceId instance,
                                   app::core::IMonotonicClock& clock, uint32_t unsubscribe_debounce_ms = 2000);

    // Move-only handle combining the EntityStore observer with this
    // Coordinator's local-observer refcount for the entity. Destruction
    // decrements the count and, if it reaches zero, schedules a debounced
    // remote unsubscribe (ARCHITECTURE.md §2: "Bei letztem lokalen Consumer darf nach
    // Debounce unsubscribed werden") instead of unsubscribing immediately.
    class SubscriptionHandle {
    public:
        SubscriptionHandle() = default;
        SubscriptionHandle(SubscriptionHandle&&) noexcept = default;
        SubscriptionHandle& operator=(SubscriptionHandle&&) noexcept = default;
        SubscriptionHandle(const SubscriptionHandle&) = delete;
        SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;
        ~SubscriptionHandle() { reset(); }
        void reset();

    private:
        friend class EntitySubscriptionCoordinator;
        EntityStore::SubscriptionHandle store_handle_;
        EntitySubscriptionCoordinator* coordinator_ = nullptr;
        std::string entity_id_;
    };

    SubscriptionHandle observe(const std::string& entity_id, EntityStore::ObserverFn fn);

    // Delivers the entity's current state (possibly still `loading`) to
    // exactly this one handle's observer, synchronously, on demand.
    // Callers invoke this once they are ready to receive it (e.g. right
    // after a widget's create() returns) — never implicitly from
    // observe() itself (ARCHITECTURE.md §5.2: must not run inline into a
    // half-constructed widget).
    void deliver_current(const std::string& entity_id, const SubscriptionHandle& handle);

    // Re-issues subscribe_entities for every entity that still has local
    // observers. Call on every transition into HaClientState::Ready,
    // including the first one (ARCHITECTURE.md §6).
    void on_ha_ready();

    // Call on any HaClient transition out of Ready that is not a step
    // toward Ready (BackoffWait, Stopped, AuthFailed): drops now-invalid
    // remote subscription bookkeeping and marks the store stale.
    void on_ha_disconnected();

    // Drives the debounced unsubscribe; call periodically (e.g. from
    // Application::poll()).
    void poll(uint64_t now_ms);

    struct Diagnostics {
        size_t tracked_entities = 0;
        size_t active_remote_subscriptions = 0;
    };
    Diagnostics diagnostics() const;

private:
    struct RemoteSubscription {
        int local_observer_count = 0;
        bool has_active_remote_subscription = false;
        bool subscribe_in_flight = false;
        int remote_subscription_id = 0;
        uint64_t pending_unsubscribe_at_ms = 0; // 0 = not pending
    };

    void on_local_observer_removed(const std::string& entity_id);
    void ensure_remote_subscription(const std::string& entity_id);
    void send_subscribe(const std::string& entity_id);
    void on_ha_event(const DecodedMessage& msg);

    EntityStore& store_;
    HaClient& ha_client_;
    HaInstanceId instance_;
    app::core::IMonotonicClock& clock_;
    uint32_t unsubscribe_debounce_ms_;
    std::map<std::string, RemoteSubscription> remote_subs_;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_ENTITYSUBSCRIPTIONCOORDINATOR_H
