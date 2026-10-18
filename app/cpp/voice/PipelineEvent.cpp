#include "PipelineEvent.h"

#include <cjson/cJSON.h>

#include <memory>

namespace app::voice {

namespace {

struct JsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};

const cJSON* path(const cJSON* node, std::initializer_list<const char*> keys)
{
    for (const char* k : keys) {
        if (!cJSON_IsObject(node)) return nullptr;
        node = cJSON_GetObjectItemCaseSensitive(node, k);
    }
    return node;
}

std::string str(const cJSON* node) { return cJSON_IsString(node) ? node->valuestring : ""; }

} // namespace

bool parse_pipeline_event(const std::string& event_json, PipelineEvent& out)
{
    out = PipelineEvent{};
    std::unique_ptr<cJSON, JsonDeleter> root(cJSON_Parse(event_json.c_str()));
    if (!root || !cJSON_IsObject(root.get())) return false;
    out.type = str(cJSON_GetObjectItemCaseSensitive(root.get(), "type"));
    if (out.type.empty()) return false;
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");

    if (out.type == "run-start") {
        const cJSON* id = path(data, {"runner_data", "stt_binary_handler_id"});
        if (cJSON_IsNumber(id)) out.stt_binary_handler_id = id->valueint;
        out.tts_url = str(path(data, {"tts_output", "url"}));
        out.tts_mime = str(path(data, {"tts_output", "mime_type"}));
        out.tts_stream_response = cJSON_IsTrue(path(data, {"tts_output", "stream_response"}));
        out.conversation_id = str(path(data, {"conversation_id"}));
    } else if (out.type == "stt-end") {
        out.text = str(path(data, {"stt_output", "text"}));
    } else if (out.type == "intent-progress") {
        out.tts_start_streaming = cJSON_IsTrue(path(data, {"tts_start_streaming"}));
    } else if (out.type == "intent-end") {
        out.text = str(path(data, {"intent_output", "response", "speech", "plain", "speech"}));
        out.conversation_id = str(path(data, {"intent_output", "conversation_id"}));
        out.continue_conversation = cJSON_IsTrue(path(data, {"intent_output", "continue_conversation"}));
    } else if (out.type == "tts-end") {
        out.tts_url = str(path(data, {"tts_output", "url"}));
    } else if (out.type == "error") {
        out.error_code = str(path(data, {"code"}));
        out.error_message = str(path(data, {"message"}));
    }
    return true;
}

bool parse_pipeline_list(const std::string& result_json, PipelineList& out)
{
    out = PipelineList{};
    std::unique_ptr<cJSON, JsonDeleter> root(cJSON_Parse(result_json.c_str()));
    if (!root || !cJSON_IsObject(root.get())) return false;
    const cJSON* list = cJSON_GetObjectItemCaseSensitive(root.get(), "pipelines");
    if (!cJSON_IsArray(list)) return false;
    const cJSON* p = nullptr;
    cJSON_ArrayForEach(p, list)
    {
        PipelineInfo info{str(cJSON_GetObjectItemCaseSensitive(p, "id")), str(cJSON_GetObjectItemCaseSensitive(p, "name"))};
        if (!info.id.empty()) out.pipelines.push_back(std::move(info));
    }
    out.preferred_id = str(cJSON_GetObjectItemCaseSensitive(root.get(), "preferred_pipeline"));
    return true;
}

} // namespace app::voice
