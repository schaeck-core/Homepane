// Contract tests for MaProtocolDecoder — decides ServerInfo/Result/Event
// purely from message shape (Music Assistant's envelope has no HA-style
// "type" discriminator). No LVGL, no real sockets.

#include "../../app/cpp/music_assistant/MaProtocolDecoder.h"

#include <cstdio>
#include <string>

using namespace app::music_assistant;

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

void test_malformed_json() {
    auto msg = MaProtocolDecoder::decode("{not json");
    expect(msg.kind == MaDecodedMessageKind::Malformed, "unparseable text decodes as Malformed");
}

void test_non_object_top_level() {
    auto msg = MaProtocolDecoder::decode("[1,2,3]");
    expect(msg.kind == MaDecodedMessageKind::Malformed, "a JSON array at the top level decodes as Malformed");
}

void test_server_info() {
    auto msg = MaProtocolDecoder::decode(
        R"({"server_id":"abc123","server_version":"2.6.0","schema_version":29,)"
        R"("min_supported_schema_version":25,"base_url":"http://ma.local:8095"})");
    expect(msg.kind == MaDecodedMessageKind::ServerInfo, "the unsolicited connect message decodes as ServerInfo");
    expect(msg.server_info.schema_version == 29, "schema_version is parsed correctly");
    expect(msg.server_info.min_supported_schema_version == 25, "min_supported_schema_version is parsed correctly");
    expect(msg.server_info.server_id == "abc123", "server_id is parsed correctly");
    expect(msg.server_info.server_version == "2.6.0", "server_version is parsed correctly");
}

void test_success_result() {
    auto msg = MaProtocolDecoder::decode(R"({"message_id":"1","result":{"ok":true},"partial":false})");
    expect(msg.kind == MaDecodedMessageKind::Result, "a message with message_id+result decodes as Result");
    expect(msg.result.message_id == "1", "message_id is kept as a string, not parsed to int");
    expect(msg.result.success, "presence of 'result' without 'error_code' means success");
    expect(msg.result.result_json == R"({"ok":true})", "the result payload is passed through as owned JSON text");
}

void test_success_result_with_no_result_field() {
    // SuccessResultMessage.result defaults to None/null — some commands
    // (e.g. fire-and-forget ones) legitimately have nothing to return.
    auto msg = MaProtocolDecoder::decode(R"({"message_id":"2","partial":false})");
    expect(msg.kind == MaDecodedMessageKind::Result, "a message_id-only message still decodes as Result");
    expect(msg.result.success, "absence of 'error_code' means success even with no 'result' key");
    expect(msg.result.result_json.empty(), "result_json is empty when the 'result' key is absent");
}

void test_error_result() {
    auto msg = MaProtocolDecoder::decode(
        R"({"message_id":"3","error_code":401,"details":"invalid or expired token"})");
    expect(msg.kind == MaDecodedMessageKind::Result, "an ErrorResultMessage still decodes as Result");
    expect(msg.result.message_id == "3", "error results carry the correlating message_id too");
    expect(!msg.result.success, "presence of 'error_code' means success is false");
    expect(msg.result.error_code == 401, "error_code is parsed correctly");
    expect(msg.result.error_message == "invalid or expired token", "'details' is surfaced as error_message");
}

void test_event() {
    auto msg = MaProtocolDecoder::decode(
        R"({"event":"queue_updated","object_id":"my_player_id","data":{"state":"playing"}})");
    expect(msg.kind == MaDecodedMessageKind::Event, "a message with a string 'event' field decodes as Event");
    expect(msg.event_type == "queue_updated", "event_type is parsed correctly");
    expect(msg.event_object_id == "my_player_id", "event_object_id is parsed correctly");
    expect(msg.event_data_json == R"({"state":"playing"})", "event data is passed through as owned JSON text");
}

void test_event_without_object_id_or_data() {
    auto msg = MaProtocolDecoder::decode(R"({"event":"providers_updated"})");
    expect(msg.kind == MaDecodedMessageKind::Event, "an event without object_id/data still decodes as Event");
    expect(msg.event_object_id.empty(), "event_object_id is empty when absent");
    expect(msg.event_data_json.empty(), "event_data_json is empty when absent");
}

void test_unrecognized_shape() {
    auto msg = MaProtocolDecoder::decode(R"({"foo":"bar"})");
    expect(msg.kind == MaDecodedMessageKind::Unknown, "a well-formed object matching none of the shapes is Unknown");
}

} // namespace

int main() {
    test_malformed_json();
    test_non_object_top_level();
    test_server_info();
    test_success_result();
    test_success_result_with_no_result_field();
    test_error_result();
    test_event();
    test_event_without_object_id_or_data();
    test_unrecognized_shape();

    if (g_failures == 0) {
        std::printf("All MaProtocolDecoder contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
