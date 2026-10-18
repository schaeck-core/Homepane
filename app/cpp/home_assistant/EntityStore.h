#ifndef APP_CPP_HOME_ASSISTANT_ENTITYSTORE_H
#define APP_CPP_HOME_ASSISTANT_ENTITYSTORE_H

#include "../core/ConnectionEpoch.h"
#include "HaTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app::home_assistant {

// Identifies which Home Assistant server an entity belongs to. A plain
// string today (matches the existing named-connection convention, e.g.
// "homeassistant"), typed so the EntityStore key can never accidentally
// collapse to bare entity_id even before a second HA server exists
// (ADR-0004, docs/migration/adr/0004-entity-key.md).
struct HaInstanceId {
    std::string value;
    bool operator==(const HaInstanceId& other) const { return value == other.value; }
    bool operator<(const HaInstanceId& other) const { return value < other.value; }
};

struct EntityKey {
    HaInstanceId instance;
    std::string entity_id;
    bool operator<(const EntityKey& other) const {
        if (instance.value != other.instance.value) return instance.value < other.instance.value;
        return entity_id < other.entity_id;
    }
    bool operator==(const EntityKey& other) const {
        return instance == other.instance && entity_id == other.entity_id;
    }
};

enum class Availability : uint8_t { loading, available, unavailable, unknown, stale };

// Owned JSON-value-text per attribute (ADR-0001): a pragmatic starting
// representation, not yet a specialized Variant — re-evaluate once real
// attribute size/type data is available (see the ADR).
using Attributes = std::map<std::string, std::string>;

struct EntityState {
    std::string entity_id;
    std::string state;
    Attributes attributes;
    // Real HA wire format sends these as UNIX epoch seconds, not ISO8601
    // strings (found from Phase 0 fixtures — see HaTypes.h EntityChange).
    std::optional<double> last_changed_epoch_s;
    std::optional<double> last_updated_epoch_s;
    Availability availability = Availability::loading;
    uint64_t version = 0;
    app::core::ConnectionEpoch source_epoch{};
};

struct EntityRecord {
    EntityState state;
    bool has_initial_snapshot = false;
    // Bounded buffer for deltas that arrive before the first snapshot
    // (ADR-0005, docs/migration/adr/0005-pre-snapshot-deltas.md).
    std::vector<EntityChange> pending_pre_snapshot_deltas;
};

// Owns entity_id -> EntityRecord for possibly multiple HA instances;
// merges snapshots/deltas; notifies local observers. Never touches LVGL,
// never holds a WidgetPtr — consumers reach it only through
// SubscriptionHandle (docs/ARCHITECTURE.md §2/§4/§5).
class EntityStore {
public:
    using ObserverFn = std::function<void(const EntityState&)>;

    // Move-only RAII handle (ARCHITECTURE.md §5.2): destruction (or explicit unsubscribe())
    // detaches the observer, idempotently, and safely even if the
    // EntityStore was already destroyed first (defensive — normally the
    // Store outlives its observers, per ARCHITECTURE.md §3).
    class SubscriptionHandle {
    public:
        SubscriptionHandle() = default;
        SubscriptionHandle(SubscriptionHandle&& other) noexcept { *this = std::move(other); }
        SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept;
        SubscriptionHandle(const SubscriptionHandle&) = delete;
        SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;
        ~SubscriptionHandle() { unsubscribe(); }

        void unsubscribe();
        bool active() const { return active_; }

    private:
        friend class EntityStore;
        SubscriptionHandle(std::weak_ptr<EntityStore*> store_ref, uint64_t slot_id)
            : store_ref_(std::move(store_ref)), slot_id_(slot_id), active_(true) {}

        std::weak_ptr<EntityStore*> store_ref_;
        uint64_t slot_id_ = 0;
        bool active_ = false;
    };

    EntityStore();
    ~EntityStore();

    // Creates a `loading` record on first local need if none exists yet —
    // this is what lets widgets exist before the initial HA snapshot
    // arrives (ARCHITECTURE.md §4).
    const EntityState& ensure(const EntityKey& key);

    // Registers fn for updates to `key`. Does NOT invoke fn synchronously
    // (ARCHITECTURE.md §5.2: never inline into a half-constructed widget) — call
    // deliver_current() explicitly once the caller is ready to receive it.
    SubscriptionHandle observe(const EntityKey& key, ObserverFn fn);

    // Delivers the record's current state to exactly this one observer,
    // synchronously, on demand (typically right after a widget finishes
    // create()).
    void deliver_current(const EntityKey& key, const SubscriptionHandle& handle);

    // event.a — full replace. Applies any deltas buffered because they
    // arrived before this snapshot (ADR-0005).
    void apply_snapshot(const EntityKey& key, const EntityChange& change, app::core::ConnectionEpoch epoch);

    // event.c (+ optional "+") — patch. Buffered (bounded) if no snapshot
    // has arrived yet for this key.
    void apply_delta(const EntityKey& key, const EntityChange& change, app::core::ConnectionEpoch epoch);

    // Disconnect: mark every record for this HA instance stale without
    // discarding the last known value (ARCHITECTURE.md §4/§6).
    void mark_all_stale(const HaInstanceId& instance);

    const EntityRecord* find(const EntityKey& key) const;

    struct Diagnostics {
        size_t entity_count = 0;
        size_t observer_count = 0;
        size_t stale_count = 0;
        size_t loading_count = 0;
    };
    Diagnostics diagnostics() const;

private:
    struct ObserverSlot {
        EntityKey key;
        ObserverFn fn;
        bool removed = false;
    };

    void notify(const EntityKey& key);
    void remove_observer(uint64_t slot_id);
    bool apply_change_to_state(EntityState& state, const EntityChange& change, bool is_snapshot,
                                app::core::ConnectionEpoch epoch);

    std::map<EntityKey, EntityRecord> records_;
    std::map<uint64_t, ObserverSlot> slots_;
    std::map<EntityKey, std::vector<uint64_t>> slots_by_key_;
    uint64_t next_slot_id_ = 1;

    // Self-pointer behind a shared_ptr so SubscriptionHandle can hold a
    // weak_ptr to detect "the EntityStore itself is already gone" without
    // requiring EntityStore to be managed via shared_ptr everywhere else.
    std::shared_ptr<EntityStore*> self_ref_;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_ENTITYSTORE_H
