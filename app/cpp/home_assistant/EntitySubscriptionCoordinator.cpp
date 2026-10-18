#include "EntitySubscriptionCoordinator.h"

#include <utility>

namespace app::home_assistant {

EntitySubscriptionCoordinator::EntitySubscriptionCoordinator(EntityStore& store, HaClient& ha_client,
                                                               HaInstanceId instance,
                                                               app::core::IMonotonicClock& clock,
                                                               uint32_t unsubscribe_debounce_ms)
    : store_(store),
      ha_client_(ha_client),
      instance_(std::move(instance)),
      clock_(clock),
      unsubscribe_debounce_ms_(unsubscribe_debounce_ms) {
    ha_client_.add_event_listener([this](const DecodedMessage& msg) { on_ha_event(msg); });
}

void EntitySubscriptionCoordinator::SubscriptionHandle::reset() {
    if (coordinator_) {
        coordinator_->on_local_observer_removed(entity_id_);
        coordinator_ = nullptr;
    }
    store_handle_.unsubscribe();
}

EntitySubscriptionCoordinator::SubscriptionHandle EntitySubscriptionCoordinator::observe(
    const std::string& entity_id, EntityStore::ObserverFn fn) {
    EntityKey k{instance_, entity_id};
    store_.ensure(k);

    auto& remote = remote_subs_[entity_id];
    ++remote.local_observer_count;
    remote.pending_unsubscribe_at_ms = 0; // a new observer cancels any scheduled unsubscribe

    ensure_remote_subscription(entity_id);

    SubscriptionHandle handle;
    handle.store_handle_ = store_.observe(k, std::move(fn));
    handle.coordinator_ = this;
    handle.entity_id_ = entity_id;
    return handle;
}

void EntitySubscriptionCoordinator::deliver_current(const std::string& entity_id,
                                                      const SubscriptionHandle& handle) {
    EntityKey k{instance_, entity_id};
    store_.deliver_current(k, handle.store_handle_);
}

void EntitySubscriptionCoordinator::on_local_observer_removed(const std::string& entity_id) {
    auto it = remote_subs_.find(entity_id);
    if (it == remote_subs_.end()) return;
    if (it->second.local_observer_count > 0) --it->second.local_observer_count;
    if (it->second.local_observer_count == 0) {
        it->second.pending_unsubscribe_at_ms = clock_.now_ms() + unsubscribe_debounce_ms_;
    }
}

void EntitySubscriptionCoordinator::ensure_remote_subscription(const std::string& entity_id) {
    auto& remote = remote_subs_[entity_id];
    if (remote.has_active_remote_subscription || remote.subscribe_in_flight) return;
    send_subscribe(entity_id);
}

void EntitySubscriptionCoordinator::send_subscribe(const std::string& entity_id) {
    auto& remote = remote_subs_[entity_id];
    remote.subscribe_in_flight = true;
    std::string cmd = "{\"type\":\"subscribe_entities\",\"entity_ids\":[\"" + entity_id + "\"]}";
    ha_client_.send_command(cmd, [this, entity_id](const ResultMessage& result) {
        auto it = remote_subs_.find(entity_id);
        if (it == remote_subs_.end()) return; // no local observers left anymore
        it->second.subscribe_in_flight = false;
        if (result.success) {
            it->second.has_active_remote_subscription = true;
            it->second.remote_subscription_id = result.id;
        }
        // On failure: left inactive. The next observe() or on_ha_ready()
        // retries via ensure_remote_subscription(); no automatic retry
        // loop yet (Phase 5 scope).
    });
}

void EntitySubscriptionCoordinator::on_ha_ready() {
    for (auto& [entity_id, remote] : remote_subs_) {
        remote.has_active_remote_subscription = false;
        remote.subscribe_in_flight = false;
        if (remote.local_observer_count > 0) {
            ensure_remote_subscription(entity_id);
        }
    }
}

void EntitySubscriptionCoordinator::on_ha_disconnected() {
    for (auto& [entity_id, remote] : remote_subs_) {
        remote.has_active_remote_subscription = false;
        remote.subscribe_in_flight = false;
        remote.pending_unsubscribe_at_ms = 0; // moot: the connection carrying it is already gone
    }
    store_.mark_all_stale(instance_);
}

void EntitySubscriptionCoordinator::poll(uint64_t now_ms) {
    for (auto& [entity_id, remote] : remote_subs_) {
        if (remote.local_observer_count != 0) continue;
        if (remote.pending_unsubscribe_at_ms == 0) continue;
        if (now_ms < remote.pending_unsubscribe_at_ms) continue;

        if (remote.has_active_remote_subscription) {
            std::string cmd = "{\"type\":\"unsubscribe_events\",\"subscription\":" +
                               std::to_string(remote.remote_subscription_id) + "}";
            ha_client_.send_command(cmd, [](const ResultMessage&) {});
        }
        remote.has_active_remote_subscription = false;
        remote.subscribe_in_flight = false;
        remote.pending_unsubscribe_at_ms = 0;
    }
}

void EntitySubscriptionCoordinator::on_ha_event(const DecodedMessage& msg) {
    for (const auto& change : msg.entity_changes) {
        EntityKey k{instance_, change.entity_id};
        if (msg.kind == DecodedMessageKind::EntitySnapshot) {
            store_.apply_snapshot(k, change, ha_client_.epoch());
        } else if (msg.kind == DecodedMessageKind::EntityDelta) {
            store_.apply_delta(k, change, ha_client_.epoch());
        }
    }
}

EntitySubscriptionCoordinator::Diagnostics EntitySubscriptionCoordinator::diagnostics() const {
    Diagnostics d;
    d.tracked_entities = remote_subs_.size();
    for (const auto& [id, r] : remote_subs_) {
        if (r.has_active_remote_subscription) ++d.active_remote_subscriptions;
    }
    return d;
}

} // namespace app::home_assistant
