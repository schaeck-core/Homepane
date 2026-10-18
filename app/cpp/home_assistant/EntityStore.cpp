#include "EntityStore.h"

#include <algorithm>

namespace app::home_assistant {

using app::core::ConnectionEpoch;

// ---- SubscriptionHandle ----------------------------------------------

EntityStore::SubscriptionHandle& EntityStore::SubscriptionHandle::operator=(SubscriptionHandle&& other) noexcept {
    if (this == &other) return *this;
    unsubscribe();
    store_ref_ = std::move(other.store_ref_);
    slot_id_ = other.slot_id_;
    active_ = other.active_;
    other.active_ = false;
    other.slot_id_ = 0;
    return *this;
}

void EntityStore::SubscriptionHandle::unsubscribe() {
    if (!active_) return;
    active_ = false;
    if (auto locked = store_ref_.lock()) {
        EntityStore* store = *locked;
        if (store) store->remove_observer(slot_id_);
    }
    slot_id_ = 0;
}

// ---- EntityStore --------------------------------------------------------

EntityStore::EntityStore() : self_ref_(std::make_shared<EntityStore*>(this)) {}

EntityStore::~EntityStore() {
    // Invalidate outstanding handles' weak_ptr target without touching
    // slots_ (nobody will call remove_observer() on a dead EntityStore).
    *self_ref_ = nullptr;
}

const EntityState& EntityStore::ensure(const EntityKey& key) {
    auto it = records_.find(key);
    if (it != records_.end()) return it->second.state;
    EntityRecord rec;
    rec.state.entity_id = key.entity_id;
    rec.state.availability = Availability::loading;
    return records_.emplace(key, std::move(rec)).first->second.state;
}

EntityStore::SubscriptionHandle EntityStore::observe(const EntityKey& key, ObserverFn fn) {
    uint64_t id = next_slot_id_++;
    ObserverSlot slot;
    slot.key = key;
    slot.fn = std::move(fn);
    slots_.emplace(id, std::move(slot));
    slots_by_key_[key].push_back(id);
    return SubscriptionHandle(self_ref_, id);
}

void EntityStore::deliver_current(const EntityKey& key, const SubscriptionHandle& handle) {
    if (!handle.active()) return;
    auto slot_it = slots_.find(handle.slot_id_);
    if (slot_it == slots_.end() || slot_it->second.removed) return;
    auto rec_it = records_.find(key);
    if (rec_it == records_.end()) return;
    slot_it->second.fn(rec_it->second.state);
}

void EntityStore::remove_observer(uint64_t slot_id) {
    auto it = slots_.find(slot_id);
    if (it == slots_.end()) return;
    EntityKey key = it->second.key;
    slots_.erase(it);
    auto by_key = slots_by_key_.find(key);
    if (by_key != slots_by_key_.end()) {
        auto& ids = by_key->second;
        ids.erase(std::remove(ids.begin(), ids.end(), slot_id), ids.end());
        if (ids.empty()) slots_by_key_.erase(by_key);
    }
}

void EntityStore::notify(const EntityKey& key) {
    auto by_key = slots_by_key_.find(key);
    if (by_key == slots_by_key_.end()) return;
    // Snapshot the id list: a callback may add/remove observers for this
    // same key (including unsubscribing itself) without corrupting the
    // notification pass (§6.2 reentrancy rule).
    std::vector<uint64_t> ids = by_key->second;

    auto rec_it = records_.find(key);
    if (rec_it == records_.end()) return;
    const EntityState& state = rec_it->second.state;

    for (uint64_t id : ids) {
        auto slot_it = slots_.find(id);
        if (slot_it == slots_.end() || slot_it->second.removed) continue;
        slot_it->second.fn(state);
    }
}

namespace {
bool states_semantically_equal(const EntityState& a, const EntityState& b) {
    return a.state == b.state && a.attributes == b.attributes && a.availability == b.availability;
}

Availability availability_from_state_string(const std::string& s) {
    if (s == "unavailable") return Availability::unavailable;
    if (s == "unknown") return Availability::unknown;
    return Availability::available;
}
} // namespace

bool EntityStore::apply_change_to_state(EntityState& state, const EntityChange& change, bool is_snapshot,
                                         ConnectionEpoch epoch) {
    EntityState before = state;

    if (is_snapshot) {
        // Full replace: start from a clean slate so removed/absent
        // attributes in the new snapshot don't linger from the old one.
        EntityState fresh;
        fresh.entity_id = state.entity_id;
        state = fresh;
    }

    if (change.state.has_value()) state.state = *change.state;
    for (const auto& attr : change.attributes) {
        if (attr.op == AttributeOp::Set) {
            state.attributes[attr.name] = attr.value_json;
        } else {
            state.attributes.erase(attr.name);
        }
    }
    if (change.last_changed_epoch_s.has_value()) state.last_changed_epoch_s = change.last_changed_epoch_s;
    if (change.last_updated_epoch_s.has_value()) state.last_updated_epoch_s = change.last_updated_epoch_s;
    state.availability = availability_from_state_string(state.state);
    state.source_epoch = epoch;

    bool changed = !states_semantically_equal(before, state);
    state.version = changed ? before.version + 1 : before.version;
    return changed;
}

void EntityStore::apply_snapshot(const EntityKey& key, const EntityChange& change, ConnectionEpoch epoch) {
    EntityRecord& rec = records_[key]; // ensure() semantics inline (create if absent)
    if (rec.state.entity_id.empty()) rec.state.entity_id = key.entity_id;

    bool changed = apply_change_to_state(rec.state, change, /*is_snapshot=*/true, epoch);
    rec.has_initial_snapshot = true;

    // Apply any deltas that arrived before this snapshot, in the order
    // they were received (ADR-0005).
    for (const auto& buffered : rec.pending_pre_snapshot_deltas) {
        changed = apply_change_to_state(rec.state, buffered, /*is_snapshot=*/false, epoch) || changed;
    }
    rec.pending_pre_snapshot_deltas.clear();

    if (changed) notify(key);
}

namespace {
constexpr size_t kMaxPendingPreSnapshotDeltas = 16;
} // namespace

void EntityStore::apply_delta(const EntityKey& key, const EntityChange& change, ConnectionEpoch epoch) {
    auto it = records_.find(key);
    if (it == records_.end()) {
        // Nobody locally ensure()'d this entity — do not start tracking
        // it just because a delta arrived (would defeat "local demand
        // drives store membership").
        return;
    }
    EntityRecord& rec = it->second;

    if (!rec.has_initial_snapshot) {
        if (rec.pending_pre_snapshot_deltas.size() >= kMaxPendingPreSnapshotDeltas) {
            rec.pending_pre_snapshot_deltas.erase(rec.pending_pre_snapshot_deltas.begin());
        }
        rec.pending_pre_snapshot_deltas.push_back(change);
        return;
    }

    if (apply_change_to_state(rec.state, change, /*is_snapshot=*/false, epoch)) notify(key);
}

void EntityStore::mark_all_stale(const HaInstanceId& instance) {
    for (auto& [key, rec] : records_) {
        if (!(key.instance == instance)) continue;
        if (rec.state.availability == Availability::stale) continue;
        rec.state.availability = Availability::stale;
        ++rec.state.version;
        notify(key);
    }
}

const EntityRecord* EntityStore::find(const EntityKey& key) const {
    auto it = records_.find(key);
    return it == records_.end() ? nullptr : &it->second;
}

EntityStore::Diagnostics EntityStore::diagnostics() const {
    Diagnostics d;
    d.entity_count = records_.size();
    d.observer_count = slots_.size();
    for (const auto& [key, rec] : records_) {
        if (rec.state.availability == Availability::stale) ++d.stale_count;
        if (rec.state.availability == Availability::loading) ++d.loading_count;
    }
    return d;
}

} // namespace app::home_assistant
