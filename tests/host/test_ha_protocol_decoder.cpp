// Golden tests for HaProtocolDecoder against the real, captured fixtures
// from Phase 0 (tests/fixtures/ha/*.ndjson) plus a couple of synthetic
// cases for paths no live capture produced (auth_invalid). No LVGL, no
// network. C++ migration phase 4 DoD.

#include "../../app/cpp/home_assistant/HaProtocolDecoder.h"

#include <cjson/cJSON.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace app::home_assistant;

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

std::vector<std::string> load_ndjson_events(const std::string& path) {
    // Fixture lines are {"timestamp":...,"entity_id":...,"event":{...}}
    // (the debug-log wrapper from Phase 0); the decoder only cares about
    // the inner "event" object re-wrapped as a top-level HA message, so
    // for auth/result-shaped fixtures (no wrapper) the raw line is used
    // as-is, and for the wrapped ones we re-synthesize a top-level
    // {"type":"event","id":<n>,"event":...} message.
    std::vector<std::string> lines;
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "Could not open fixture: %s (run tests from the repo root)\n", path.c_str());
        return lines;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::string wrap_as_event_message(const std::string& debug_log_line) {
    // Extract the "event": {...} object out of the Phase 0 debug-log
    // wrapper and re-synthesize a proper top-level HA message, via cJSON
    // rather than string slicing so nesting/whitespace can't matter.
    cJSON* root = cJSON_Parse(debug_log_line.c_str());
    if (!root) return {};
    std::string result;
    cJSON* event = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (event) {
        char* event_txt = cJSON_PrintUnformatted(event);
        if (event_txt) {
            result = std::string("{\"type\":\"event\",\"id\":1,\"event\":") + event_txt + "}";
            cJSON_free(event_txt);
        }
    }
    cJSON_Delete(root);
    return result;
}

const EntityChange* find_attr_owner(const std::vector<EntityChange>& changes, const std::string& entity_id) {
    for (const auto& c : changes) {
        if (c.entity_id == entity_id) return &c;
    }
    return nullptr;
}

const AttributeChange* find_attr(const EntityChange& change, const std::string& name) {
    for (const auto& a : change.attributes) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

void test_auth_handshake_fixture() {
    auto lines = load_ndjson_events("tests/fixtures/ha/auth_handshake.ndjson");
    expect(lines.size() == 2, "auth_handshake.ndjson has the expected two lines");
    if (lines.size() != 2) return;

    auto required = HaProtocolDecoder::decode(lines[0]);
    expect(required.kind == DecodedMessageKind::AuthRequired, "real auth_required message decodes as AuthRequired");

    auto ok = HaProtocolDecoder::decode(lines[1]);
    expect(ok.kind == DecodedMessageKind::AuthOk, "real auth_ok message decodes as AuthOk");
}

void test_auth_invalid_synthetic() {
    auto msg = HaProtocolDecoder::decode(
        R"({"type":"auth_invalid","message":"Invalid access token or password"})");
    expect(msg.kind == DecodedMessageKind::AuthInvalid, "auth_invalid decodes as AuthInvalid");
    expect(msg.auth_invalid_message == "Invalid access token or password", "auth_invalid message text is captured");
}

void test_result_error_fixture() {
    auto lines = load_ndjson_events("tests/fixtures/ha/result_error.ndjson");
    expect(lines.size() == 1, "result_error.ndjson has one line");
    if (lines.empty()) return;

    auto msg = HaProtocolDecoder::decode(lines[0]);
    expect(msg.kind == DecodedMessageKind::Result, "real result-error message decodes as Result");
    expect(msg.result.id == 2, "result id is captured");
    expect(!msg.result.success, "success:false is captured");
    expect(msg.result.error_code == "config_not_found", "error code is captured");
    expect(msg.result.error_message == "No config found.", "error message is captured");
}

void test_result_success_synthetic() {
    auto msg = HaProtocolDecoder::decode(R"({"id":1,"type":"result","success":true,"result":{}})");
    expect(msg.kind == DecodedMessageKind::Result, "success result decodes as Result");
    expect(msg.result.id == 1, "success result id is captured");
    expect(msg.result.success, "success:true is captured");
    expect(msg.result.result_json == "{}", "empty result object is captured as owned JSON text");
}

void test_light_snapshot_and_delta_fixture() {
    auto lines = load_ndjson_events("tests/fixtures/ha/light_snapshot_and_delta.ndjson");
    expect(lines.size() == 3, "light_snapshot_and_delta.ndjson has three lines");
    if (lines.size() != 3) return;

    // Line 1: initial snapshot (event.a).
    auto snap = HaProtocolDecoder::decode(wrap_as_event_message(lines[0]));
    expect(snap.kind == DecodedMessageKind::EntitySnapshot, "real light snapshot decodes as EntitySnapshot");
    expect(snap.entity_changes.size() == 1, "snapshot carries exactly one entity");
    if (!snap.entity_changes.empty()) {
        const auto& c = snap.entity_changes[0];
        expect(c.entity_id == "light.bedroom_lamp", "snapshot entity_id matches the fixture");
        expect(c.is_full_snapshot, "snapshot is flagged as a full snapshot, not a patch");
        expect(c.state.has_value() && *c.state == "on", "snapshot state is 'on'");
        auto* brightness = find_attr(c, "brightness");
        expect(brightness && brightness->op == AttributeOp::Set && brightness->value_json == "255",
               "snapshot brightness attribute is Set to 255");
    }

    // Line 2: delta turning the light off — real fixture shows explicit
    // null attributes, must decode as Removed, not silently dropped.
    auto off = HaProtocolDecoder::decode(wrap_as_event_message(lines[1]));
    expect(off.kind == DecodedMessageKind::EntityDelta, "real light 'off' delta decodes as EntityDelta");
    if (!off.entity_changes.empty()) {
        const auto& c = off.entity_changes[0];
        expect(!c.is_full_snapshot, "delta is not flagged as a full snapshot");
        expect(c.state.has_value() && *c.state == "off", "delta state is 'off'");
        expect(c.last_changed_epoch_s.has_value(), "state-changing delta carries last_changed (lc), not just last_updated");
        expect(!c.last_updated_epoch_s.has_value(), "state-changing delta does not also carry lu");
        auto* brightness = find_attr(c, "brightness");
        expect(brightness && brightness->op == AttributeOp::Removed,
               "explicit JSON null in a delta decodes as AttributeOp::Removed, not Set(\"null\")");
        expect(c.attributes.size() == 7, "all seven null-valued attributes from the real delta are captured");
    }

    // Line 3: delta turning the light back on with real attribute values,
    // including an array-valued attribute (hs_color).
    auto on = HaProtocolDecoder::decode(wrap_as_event_message(lines[2]));
    expect(on.kind == DecodedMessageKind::EntityDelta, "real light 'on' delta decodes as EntityDelta");
    if (!on.entity_changes.empty()) {
        const auto& c = on.entity_changes[0];
        expect(c.state.has_value() && *c.state == "on", "second delta state is 'on'");
        auto* hs = find_attr(c, "hs_color");
        expect(hs && hs->op == AttributeOp::Set && hs->value_json == "[27.518,51.595]",
               "array-valued attribute (hs_color) is captured as owned JSON array text");
    }
}

void test_cover_delta_sequence_fixture() {
    auto lines = load_ndjson_events("tests/fixtures/ha/cover_delta_sequence.ndjson");
    expect(lines.size() == 5, "cover_delta_sequence.ndjson has five lines");
    if (lines.size() != 5) return;

    auto snap = HaProtocolDecoder::decode(wrap_as_event_message(lines[0]));
    expect(snap.kind == DecodedMessageKind::EntitySnapshot, "cover snapshot decodes as EntitySnapshot");

    // Line 2: state-only delta ("opening"), no "a" key at all in the
    // real payload — attributes must come back empty, not crash.
    auto opening = HaProtocolDecoder::decode(wrap_as_event_message(lines[1]));
    expect(opening.kind == DecodedMessageKind::EntityDelta, "'opening' delta decodes as EntityDelta");
    if (!opening.entity_changes.empty()) {
        const auto& c = opening.entity_changes[0];
        expect(c.state.has_value() && *c.state == "opening", "'opening' delta state is captured");
        expect(c.attributes.empty(), "delta with no 'a' key at all yields zero attribute changes, not a crash");
    }

    // Line 3: attribute-only delta (position tick), no "s" key — this is
    // the real-world proof that lu-only (no lc) deltas exist and must
    // leave `state` unset rather than defaulting to something wrong.
    auto tick = HaProtocolDecoder::decode(wrap_as_event_message(lines[2]));
    expect(tick.kind == DecodedMessageKind::EntityDelta, "position-tick delta decodes as EntityDelta");
    if (!tick.entity_changes.empty()) {
        const auto& c = tick.entity_changes[0];
        expect(!c.state.has_value(), "attribute-only delta leaves state unset (no 's' key was present)");
        expect(c.last_updated_epoch_s.has_value(), "attribute-only delta carries last_updated (lu)");
        expect(!c.last_changed_epoch_s.has_value(), "attribute-only delta does not carry lc");
        auto* pos = find_attr(c, "current_position");
        expect(pos && pos->op == AttributeOp::Set && pos->value_json == "80", "current_position ticks to 80");
    }

    // Line 5: state AND attribute change together.
    auto final_open = HaProtocolDecoder::decode(wrap_as_event_message(lines[4]));
    if (!final_open.entity_changes.empty()) {
        const auto& c = final_open.entity_changes[0];
        expect(c.state.has_value() && *c.state == "open", "final delta state is 'open'");
        expect(c.last_changed_epoch_s.has_value(), "final delta (state changed) carries lc");
        auto* pos = find_attr(c, "current_position");
        expect(pos && pos->value_json == "100", "final delta current_position is 100");
    }
}

void test_sensor_unavailable_fixture() {
    auto lines = load_ndjson_events("tests/fixtures/ha/sensor_unavailable_snapshot.ndjson");
    expect(lines.size() == 1, "sensor_unavailable_snapshot.ndjson has one line");
    if (lines.empty()) return;

    auto msg = HaProtocolDecoder::decode(wrap_as_event_message(lines[0]));
    expect(msg.kind == DecodedMessageKind::EntitySnapshot, "unavailable sensor snapshot decodes as EntitySnapshot");
    if (!msg.entity_changes.empty()) {
        const auto& c = msg.entity_changes[0];
        expect(c.state.has_value() && *c.state == "unavailable", "real 'unavailable' state string is preserved as-is");
    }
}

void test_weather_forecast_event_decodes_as_raw_event() {
    // Synthetic, shaped after HA's real weather/subscribe_forecast event
    // payload (no captured fixture exists for this yet, unlike the
    // entity-shaped fixtures above) — C++ migration phase 9.
    auto msg = HaProtocolDecoder::decode(
        R"({"id":7,"type":"event","event":{"forecast":[)"
        R"({"condition":"sunny","datetime":"2026-09-10T06:00:00+00:00","temperature":22.5,"templow":14.1})"
        R"(]}})");
    expect(msg.kind == DecodedMessageKind::RawEvent,
           "a type=event message that is not an entity snapshot/delta decodes as RawEvent, not Unknown");
    expect(msg.event_subscription_id == 7, "RawEvent preserves the message's 'id' as event_subscription_id");
    expect(msg.raw_event_json.find("\"forecast\"") != std::string::npos,
           "RawEvent carries the raw 'event' object as owned JSON text for a higher layer to interpret");
}

void test_malformed_inputs() {
    auto not_json = HaProtocolDecoder::decode("this is not json");
    expect(not_json.kind == DecodedMessageKind::Malformed, "non-JSON input decodes as Malformed, not a crash");

    auto no_type = HaProtocolDecoder::decode("{}");
    expect(no_type.kind == DecodedMessageKind::Malformed, "JSON object without a 'type' field decodes as Malformed");

    auto unknown_type = HaProtocolDecoder::decode(R"({"type":"pong"})");
    expect(unknown_type.kind == DecodedMessageKind::Unknown, "recognized-but-unhandled type decodes as Unknown, not Malformed");
}

} // namespace

int main() {
    test_auth_handshake_fixture();
    test_auth_invalid_synthetic();
    test_result_error_fixture();
    test_result_success_synthetic();
    test_light_snapshot_and_delta_fixture();
    test_cover_delta_sequence_fixture();
    test_sensor_unavailable_fixture();
    test_weather_forecast_event_decodes_as_raw_event();
    test_malformed_inputs();

    if (g_failures == 0) {
        std::printf("All HaProtocolDecoder golden tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
