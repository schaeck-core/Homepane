#ifndef APP_CPP_HOME_ASSISTANT_HATYPES_H
#define APP_CPP_HOME_ASSISTANT_HATYPES_H

#include <optional>
#include <string>
#include <vector>

namespace app::home_assistant {

enum class DecodedMessageKind {
    AuthRequired,
    AuthOk,
    AuthInvalid,
    Result,
    EntitySnapshot,  // event.a — full state, replaces wholesale
    EntityDelta,     // event.c (+ optional "+" wrapper) — patch, merge onto existing state
    RawEvent,        // type=event, but not an entity snapshot/delta (e.g. a weather
                      // forecast event) — passed through as owned raw JSON text since
                      // the decoder stays domain-generic; QueryCoordinator (Phase 9)
                      // interprets the shape itself.
    Unknown,         // recognized JSON, but not one of the above (ignored upstream)
    Malformed,       // not parseable as JSON, or missing a required "type"/"id" field
};

enum class AttributeOp { Set, Removed };

struct AttributeChange {
    std::string name;
    AttributeOp op = AttributeOp::Set;
    // Owned JSON value text (e.g. "255", "\"color_temp\"", "[27.518,51.595]").
    // Only meaningful when op == Set. Kept as an owned JSON-value tree per
    // ADR-0001 (docs/migration/adr/0001-attribute-representation.md), not
    // a specialized Variant yet.
    std::string value_json;
};

// A real HA fixture (tests/fixtures/ha/cover_delta_sequence.ndjson) shows
// "lc" (last_changed) and "lu" (last_updated) as UNIX epoch seconds
// (double), e.g. 1788738598.1611631 — NOT ISO8601 strings. This refines
// CPP_MIGRATION_PLAN.md §5's EntityState sketch, which assumed strings;
// EntityStore (Phase 5) should use this numeric representation instead.
struct EntityChange {
    std::string entity_id;
    std::optional<std::string> state;           // unset = state unchanged in this delta
    std::optional<double> last_changed_epoch_s;  // present when state changed ("lc")
    std::optional<double> last_updated_epoch_s;  // present when only attributes changed ("lu")
    std::vector<AttributeChange> attributes;
    bool is_full_snapshot = false; // true for event.a: attributes replace wholesale, not merge
};

struct ResultMessage {
    int id = 0;
    bool success = false;
    std::string result_json; // owned raw JSON text of "result", empty if absent
    std::string error_code;
    std::string error_message;
};

struct DecodedMessage {
    DecodedMessageKind kind = DecodedMessageKind::Unknown;
    std::string auth_invalid_message; // only for AuthInvalid
    ResultMessage result;             // only for Result
    int event_subscription_id = 0;    // only for EntitySnapshot/EntityDelta/RawEvent ("id" of the event)
    std::vector<EntityChange> entity_changes; // only for EntitySnapshot/EntityDelta
    std::string raw_event_json;       // only for RawEvent: owned raw JSON text of "event"
    std::string malformed_reason;     // only for Malformed
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_HATYPES_H
