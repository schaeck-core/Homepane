#ifndef APP_CPP_MUSIC_ASSISTANT_MATYPES_H
#define APP_CPP_MUSIC_ASSISTANT_MATYPES_H

#include <string>

namespace app::music_assistant {

enum class MaDecodedMessageKind {
    ServerInfo,  // unsolicited, sent immediately on connect, before any command
    Result,      // SuccessResultMessage or ErrorResultMessage, both correlated by message_id
    Event,       // server-pushed MassEvent; not yet consumed by anything (see MaClient)
    Unknown,     // recognized JSON, but not one of the above (ignored upstream)
    Malformed,   // not parseable as JSON, or missing required fields
};

// Only the fields MaClient actually needs (schema_version decides whether
// auth is required at all — servers older than schema 28 don't require it).
struct MaServerInfo {
    int schema_version = 0;
    int min_supported_schema_version = 0;
    std::string server_id;
    std::string server_version;
};

// message_id is a string in MA's protocol (music_assistant_models.api.
// CommandMessage.message_id: str) — unlike HA's numeric "id". Keying
// correlation by string end-to-end (rather than parsing back to an int)
// avoids assuming our own ids are the only ones ever seen.
struct MaResultMessage {
    std::string message_id;
    bool success = false;
    std::string result_json;  // owned raw JSON text of "result", empty if absent
    int error_code = 0;
    std::string error_message;  // from ErrorResultMessage.details, if present
};

struct MaDecodedMessage {
    MaDecodedMessageKind kind = MaDecodedMessageKind::Unknown;
    MaServerInfo server_info;  // only for ServerInfo
    MaResultMessage result;    // only for Result
    std::string event_type;        // only for Event ("event" field)
    std::string event_object_id;   // only for Event ("object_id" field, may be empty)
    std::string event_data_json;   // only for Event: owned raw JSON text of "data"
    std::string malformed_reason;  // only for Malformed
};

} // namespace app::music_assistant

#endif // APP_CPP_MUSIC_ASSISTANT_MATYPES_H
