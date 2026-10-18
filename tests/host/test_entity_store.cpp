// Unit tests for EntityStore (Phase 5 of CPP_MIGRATION_PLAN.md §5/§6).
// No LVGL, no network — pure in-process store logic.

#include "../../app/cpp/home_assistant/EntityStore.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace app::home_assistant;
using app::core::ConnectionEpoch;

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

EntityKey key(const std::string& entity_id) {
    return EntityKey{HaInstanceId{"homeassistant"}, entity_id};
}

EntityChange snapshot_change(const std::string& entity_id, const std::string& state,
                              std::vector<std::pair<std::string, std::string>> attrs = {}) {
    EntityChange c;
    c.entity_id = entity_id;
    c.state = state;
    c.is_full_snapshot = true;
    for (auto& [name, value] : attrs) {
        c.attributes.push_back(AttributeChange{name, AttributeOp::Set, value});
    }
    return c;
}

void test_ensure_creates_loading_record() {
    EntityStore store;
    const EntityState& s = store.ensure(key("light.x"));
    expect(s.availability == Availability::loading, "ensure() creates a record with loading availability");
    expect(s.entity_id == "light.x", "ensure() sets entity_id on the fresh record");
    expect(store.find(key("light.x")) != nullptr, "ensure() is visible via find()");
}

void test_two_observers_share_one_record() {
    EntityStore store;
    store.ensure(key("light.x"));

    std::vector<std::string> a_seen, b_seen;
    auto handle_a = store.observe(key("light.x"), [&](const EntityState& s) { a_seen.push_back(s.state); });
    auto handle_b = store.observe(key("light.x"), [&](const EntityState& s) { b_seen.push_back(s.state); });

    expect(a_seen.empty() && b_seen.empty(),
           "observe() never invokes the callback synchronously (must not run in a half-constructed widget)");

    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "on"), ConnectionEpoch(1));

    expect(a_seen.size() == 1 && a_seen[0] == "on", "first observer sees the snapshot");
    expect(b_seen.size() == 1 && b_seen[0] == "on", "second observer sees the same snapshot independently");
    expect(store.diagnostics().entity_count == 1, "two observers of the same entity still yield exactly one record");
    expect(store.diagnostics().observer_count == 2, "diagnostics count both observers");
}

void test_deliver_current_is_synchronous_and_explicit() {
    EntityStore store;
    store.apply_snapshot(key("sensor.x"), snapshot_change("sensor.x", "42"), ConnectionEpoch(1));

    std::vector<std::string> seen;
    auto handle = store.observe(key("sensor.x"), [&](const EntityState& s) { seen.push_back(s.state); });
    expect(seen.empty(), "a late observer does not retroactively receive past notifications");

    store.deliver_current(key("sensor.x"), handle);
    expect(seen.size() == 1 && seen[0] == "42", "deliver_current() hands the current state to exactly this observer");
}

void test_self_unsubscribe_during_notify_is_safe() {
    EntityStore store;
    store.ensure(key("light.x"));

    int call_count = 0;
    // A handle that will be captured and unsubscribed from within its own callback.
    auto handle = std::make_shared<EntityStore::SubscriptionHandle>();
    *handle = store.observe(key("light.x"), [&](const EntityState&) {
        ++call_count;
        handle->unsubscribe(); // self-unsubscribe mid-notification
    });

    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "on"), ConnectionEpoch(1));
    expect(call_count == 1, "self-unsubscribing observer still runs exactly once for the event that triggered it");

    // A second, unrelated update must not crash and must not re-invoke the removed observer.
    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "off"), ConnectionEpoch(1));
    expect(call_count == 1, "an observer that unsubscribed itself is not called again on subsequent updates");
}

void test_other_observer_survives_a_sibling_self_unsubscribe() {
    EntityStore store;
    store.ensure(key("light.x"));

    int other_calls = 0;
    auto self_removing = std::make_shared<EntityStore::SubscriptionHandle>();
    *self_removing = store.observe(key("light.x"), [&](const EntityState&) { self_removing->unsubscribe(); });
    auto other = store.observe(key("light.x"), [&](const EntityState&) { ++other_calls; });

    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "on"), ConnectionEpoch(1));
    expect(other_calls == 1, "a sibling observer still runs correctly while another self-unsubscribes in the same pass");
}

void test_pre_snapshot_delta_is_buffered_and_replayed() {
    EntityStore store;
    store.ensure(key("cover.x"));

    std::vector<std::string> seen_states;
    auto handle = store.observe(key("cover.x"), [&](const EntityState& s) { seen_states.push_back(s.state); });

    // Delta arrives before any snapshot — real race documented in
    // CPP_MIGRATION_PLAN.md §5 / ADR-0005.
    EntityChange delta;
    delta.entity_id = "cover.x";
    delta.state = "opening";
    store.apply_delta(key("cover.x"), delta, ConnectionEpoch(1));
    expect(seen_states.empty(), "a delta arriving before the first snapshot is buffered, not applied or notified yet");

    store.apply_snapshot(key("cover.x"), snapshot_change("cover.x", "open",
                                                           {{"current_position", "70"}}),
                          ConnectionEpoch(1));

    expect(seen_states.size() == 1, "snapshot arrival triggers exactly one notification, not one per buffered delta");
    expect(!seen_states.empty() && seen_states[0] == "opening",
           "the buffered pre-snapshot delta is replayed after the snapshot and wins (reflects the latest known state)");
}

void test_pre_snapshot_buffer_is_bounded() {
    EntityStore store;
    store.ensure(key("sensor.spam"));

    // Post more deltas than the internal bound (16) before any snapshot,
    // each touching a distinct attribute name so eviction is observable.
    for (int i = 0; i < 20; ++i) {
        EntityChange delta;
        delta.entity_id = "sensor.spam";
        delta.attributes.push_back(
            AttributeChange{"attr_" + std::to_string(i), AttributeOp::Set, std::to_string(i)});
        store.apply_delta(key("sensor.spam"), delta, ConnectionEpoch(1));
    }

    store.apply_snapshot(key("sensor.spam"), snapshot_change("sensor.spam", "ready"), ConnectionEpoch(1));

    const EntityRecord* rec = store.find(key("sensor.spam"));
    expect(rec != nullptr, "record exists after snapshot");
    if (rec) {
        expect(rec->state.attributes.count("attr_0") == 0,
               "the oldest pre-snapshot deltas are evicted once the bound is exceeded, not queued unboundedly");
        expect(rec->state.attributes.count("attr_19") == 1,
               "the most recent pre-snapshot deltas within the bound survive and get replayed");
    }
}

void test_null_attribute_is_removed_not_set_to_literal_null() {
    EntityStore store;
    store.apply_snapshot(key("light.x"),
                          snapshot_change("light.x", "on", {{"brightness", "255"}}),
                          ConnectionEpoch(1));

    EntityChange off_delta;
    off_delta.entity_id = "light.x";
    off_delta.state = "off";
    off_delta.attributes.push_back(AttributeChange{"brightness", AttributeOp::Removed, ""});
    store.apply_delta(key("light.x"), off_delta, ConnectionEpoch(1));

    const EntityRecord* rec = store.find(key("light.x"));
    expect(rec && rec->state.attributes.count("brightness") == 0,
           "AttributeOp::Removed erases the attribute from the store's state entirely");
}

void test_version_only_bumps_on_semantic_change() {
    EntityStore store;
    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "on"), ConnectionEpoch(1));
    uint64_t v1 = store.find(key("light.x"))->state.version;

    int notify_count = 0;
    auto handle = store.observe(key("light.x"), [&](const EntityState&) { ++notify_count; });

    // A delta that changes nothing observable.
    EntityChange no_op;
    no_op.entity_id = "light.x";
    no_op.state = "on"; // identical to current state
    store.apply_delta(key("light.x"), no_op, ConnectionEpoch(1));

    expect(store.find(key("light.x"))->state.version == v1, "a no-op delta does not bump the version");
    expect(notify_count == 0, "a no-op delta does not notify observers");

    EntityChange real_change;
    real_change.entity_id = "light.x";
    real_change.state = "off";
    store.apply_delta(key("light.x"), real_change, ConnectionEpoch(1));
    expect(store.find(key("light.x"))->state.version == v1 + 1, "a real change bumps the version exactly once");
    expect(notify_count == 1, "a real change notifies observers exactly once");
}

void test_disconnect_marks_stale_then_reconnect_snapshot_replaces_cleanly() {
    EntityStore store;
    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "on", {{"brightness", "255"}}),
                          ConnectionEpoch(1));

    std::vector<Availability> seen;
    auto handle = store.observe(key("light.x"), [&](const EntityState& s) { seen.push_back(s.availability); });

    store.mark_all_stale(HaInstanceId{"homeassistant"});
    expect(seen.size() == 1 && seen[0] == Availability::stale, "disconnect marks the entity stale exactly once");
    expect(store.find(key("light.x"))->state.state == "on",
           "the last known state is preserved while stale, not cleared");

    // A second mark_all_stale (e.g. a redundant disconnect signal) must
    // not notify again — already stale.
    store.mark_all_stale(HaInstanceId{"homeassistant"});
    expect(seen.size() == 1, "marking an already-stale entity stale again does not notify a second time");

    // Reconnect: a fresh snapshot arrives.
    store.apply_snapshot(key("light.x"), snapshot_change("light.x", "on", {{"brightness", "255"}}),
                          ConnectionEpoch(2));
    expect(seen.size() == 2 && seen[1] == Availability::available,
           "the reconnect snapshot clears stale and notifies exactly once, not once per stale+available pair");
}

void test_unavailable_and_unknown_state_strings_map_to_availability() {
    EntityStore store;
    store.apply_snapshot(key("sensor.x"), snapshot_change("sensor.x", "unavailable"), ConnectionEpoch(1));
    expect(store.find(key("sensor.x"))->state.availability == Availability::unavailable,
           "HA state string 'unavailable' is mirrored into Availability::unavailable");
    expect(store.find(key("sensor.x"))->state.state == "unavailable",
           "the original state string is preserved alongside the derived availability");

    store.apply_snapshot(key("sensor.y"), snapshot_change("sensor.y", "unknown"), ConnectionEpoch(1));
    expect(store.find(key("sensor.y"))->state.availability == Availability::unknown,
           "HA state string 'unknown' is mirrored into Availability::unknown");
}

void test_delta_for_never_ensured_entity_is_ignored() {
    EntityStore store;
    EntityChange delta;
    delta.entity_id = "light.nobody_asked_for_this";
    delta.state = "on";
    store.apply_delta(key("light.nobody_asked_for_this"), delta, ConnectionEpoch(1));
    expect(store.find(key("light.nobody_asked_for_this")) == nullptr,
           "a delta for an entity nobody ensure()'d does not create store membership");
}

} // namespace

int main() {
    test_ensure_creates_loading_record();
    test_two_observers_share_one_record();
    test_deliver_current_is_synchronous_and_explicit();
    test_self_unsubscribe_during_notify_is_safe();
    test_other_observer_survives_a_sibling_self_unsubscribe();
    test_pre_snapshot_delta_is_buffered_and_replayed();
    test_pre_snapshot_buffer_is_bounded();
    test_null_attribute_is_removed_not_set_to_literal_null();
    test_version_only_bumps_on_semantic_change();
    test_disconnect_marks_stale_then_reconnect_snapshot_replaces_cleanly();
    test_unavailable_and_unknown_state_strings_map_to_availability();
    test_delta_for_never_ensured_entity_is_ignored();

    if (g_failures == 0) {
        std::printf("All EntityStore unit tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
