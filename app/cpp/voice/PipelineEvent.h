#ifndef APP_CPP_VOICE_PIPELINEEVENT_H
#define APP_CPP_VOICE_PIPELINEEVENT_H

#include <string>
#include <vector>

namespace app::voice {

// One event of a running assist_pipeline/run (HA sends them as
// {"type":"event","id":<run id>,"event":{"type":...,"data":{...}}}); only
// the fields the voice assistant uses.
struct PipelineEvent {
    std::string type;               // run-start, stt-vad-end, stt-end, intent-progress, ...
    int stt_binary_handler_id = -1; // run-start
    std::string tts_url;            // run-start (stream URL, known early) or tts-end
    std::string tts_mime;           // run-start
    bool tts_stream_response = false; // run-start: HA can stream the response
    bool tts_start_streaming = false; // intent-progress: response audio is being produced now
    std::string text;               // stt-end: transcript; intent-end: response speech
    std::string conversation_id;    // run-start, intent-end: HA's chat session
    bool continue_conversation = false; // intent-end: the agent asked a question
    std::string error_code, error_message; // error
};

// `event_json` is the "event" object. Returns false if it is not one.
bool parse_pipeline_event(const std::string& event_json, PipelineEvent& out);

// The assistants configured in HA (result of assist_pipeline/pipeline/list).
struct PipelineInfo {
    std::string id;
    std::string name;
};
struct PipelineList {
    std::vector<PipelineInfo> pipelines;
    std::string preferred_id; // HA's default assistant
};
bool parse_pipeline_list(const std::string& result_json, PipelineList& out);

} // namespace app::voice

#endif // APP_CPP_VOICE_PIPELINEEVENT_H
