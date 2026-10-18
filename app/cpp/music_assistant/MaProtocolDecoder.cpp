#include "MaProtocolDecoder.h"

#include <cjson/cJSON.h>

#include <memory>

namespace app::music_assistant {

namespace {

struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

std::string print_owned(cJSON* value) {
    if (!value) return {};
    char* txt = cJSON_PrintUnformatted(value);
    if (!txt) return {};
    std::string result = txt;
    cJSON_free(txt);
    return result;
}

} // namespace

// MA's envelope has no HA-style "type" discriminator field. Messages are
// distinguished structurally instead (music_assistant_models.api):
//   - has "message_id" -> a Result (Success or Error, both correlated the
//     same way; distinguished by presence of "error_code")
//   - has a string "event" field -> a pushed MassEvent
//   - has "schema_version" + "server_id" -> the unsolicited ServerInfo
//     sent immediately on connect, before any command
MaDecodedMessage MaProtocolDecoder::decode(const std::string& raw_json) {
    MaDecodedMessage msg;

    CJsonPtr root(cJSON_Parse(raw_json.c_str()));
    if (!root || !cJSON_IsObject(root.get())) {
        msg.kind = MaDecodedMessageKind::Malformed;
        msg.malformed_reason = root ? "top-level JSON is not an object" : "invalid JSON";
        return msg;
    }

    cJSON* message_id = cJSON_GetObjectItemCaseSensitive(root.get(), "message_id");
    if (message_id && cJSON_IsString(message_id)) {
        msg.kind = MaDecodedMessageKind::Result;
        msg.result.message_id = message_id->valuestring;

        cJSON* error_code = cJSON_GetObjectItemCaseSensitive(root.get(), "error_code");
        if (error_code && cJSON_IsNumber(error_code)) {
            msg.result.success = false;
            msg.result.error_code = error_code->valueint;
            cJSON* details = cJSON_GetObjectItemCaseSensitive(root.get(), "details");
            if (details && cJSON_IsString(details)) msg.result.error_message = details->valuestring;
        } else {
            msg.result.success = true;
            cJSON* result = cJSON_GetObjectItemCaseSensitive(root.get(), "result");
            if (result) msg.result.result_json = print_owned(result);
        }
        return msg;
    }

    cJSON* event = cJSON_GetObjectItemCaseSensitive(root.get(), "event");
    if (event && cJSON_IsString(event)) {
        msg.kind = MaDecodedMessageKind::Event;
        msg.event_type = event->valuestring;
        cJSON* object_id = cJSON_GetObjectItemCaseSensitive(root.get(), "object_id");
        if (object_id && cJSON_IsString(object_id)) msg.event_object_id = object_id->valuestring;
        cJSON* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
        if (data) msg.event_data_json = print_owned(data);
        return msg;
    }

    cJSON* schema_version = cJSON_GetObjectItemCaseSensitive(root.get(), "schema_version");
    cJSON* server_id = cJSON_GetObjectItemCaseSensitive(root.get(), "server_id");
    if (schema_version && cJSON_IsNumber(schema_version) && server_id && cJSON_IsString(server_id)) {
        msg.kind = MaDecodedMessageKind::ServerInfo;
        msg.server_info.schema_version = schema_version->valueint;
        msg.server_info.server_id = server_id->valuestring;

        cJSON* min_schema = cJSON_GetObjectItemCaseSensitive(root.get(), "min_supported_schema_version");
        if (min_schema && cJSON_IsNumber(min_schema)) msg.server_info.min_supported_schema_version = min_schema->valueint;
        cJSON* server_version = cJSON_GetObjectItemCaseSensitive(root.get(), "server_version");
        if (server_version && cJSON_IsString(server_version)) msg.server_info.server_version = server_version->valuestring;
        return msg;
    }

    msg.kind = MaDecodedMessageKind::Unknown;
    return msg;
}

} // namespace app::music_assistant
