#include "VoiceController.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <cjson/cJSON.h>

using app::core::LogLevel;
using app::home_assistant::DecodedMessage;
using app::home_assistant::DecodedMessageKind;
using app::home_assistant::HaClient;
using app::home_assistant::HaClientState;
using app::home_assistant::ResultMessage;

namespace app::voice {

VoiceController::VoiceController(IVoiceAudio& audio, app::core::IMonotonicClock& clock, ClientProvider client,
                                 std::function<std::string()> ha_base_url, std::function<std::string()> pipeline_id,
                                 app::core::ILogger* logger)
    : audio_(audio), clock_(clock), client_provider_(std::move(client)), ha_base_url_(std::move(ha_base_url)),
      pipeline_id_(std::move(pipeline_id)), logger_(logger)
{
}

namespace {
// HA ids (pipelines, conversations) are ULIDs / slugs; anything else is not
// put into the JSON command.
bool safe_id(const std::string& id)
{
    return !id.empty() && id.size() < 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    });
}
} // namespace

void VoiceController::bands(float out[3])
{
    if (status_.state == VoiceState::Listening) {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        band_levels_get(&mic_bands_, out);
    } else {
        audio_.playback_bands(out);
    }
}

void VoiceController::play_earcon(int style, earcon_kind_t kind)
{
    if (style < 0 || style >= EARCON_STYLE_COUNT) return;
    const auto st = static_cast<earcon_style_t>(style);
    std::vector<int16_t> pcm(earcon_length(st, kind, 16000));
    pcm.resize(earcon_render(st, kind, 16000, pcm.data(), pcm.size()));
    audio_.play_pcm(std::move(pcm));
}

void VoiceController::preview_earcon(int style) { play_earcon(style, EARCON_START); }

void VoiceController::set_enabled(bool enabled)
{
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled) cancel();
    log(enabled ? "enabled" : "disabled");
}

void VoiceController::fetch_pipelines(PipelinesCallback done)
{
    HaClient* client = client_provider_ ? client_provider_() : nullptr;
    if (!client || client->state() != HaClientState::Ready) {
        if (done) done(false, PipelineList{}, "not connected to Home Assistant");
        return;
    }
    client->send_command(R"({"type":"assist_pipeline/pipeline/list"})", [done](const ResultMessage& r) {
        if (!done) return;
        PipelineList list;
        if (!r.success) {
            done(false, list, r.error_message.empty() ? r.error_code : r.error_message);
        } else if (!parse_pipeline_list(r.result_json, list)) {
            done(false, list, "unexpected answer from Home Assistant");
        } else {
            done(true, list, "");
        }
    });
}

void VoiceController::log(const std::string& line)
{
    if (!logger_) return;
    // Times relative to the end of speech make the latency of each stage visible.
    char prefix[32];
    if (speech_end_ms_) {
        std::snprintf(prefix, sizeof(prefix), "Voice %+6.2fs: ",
                      (static_cast<double>(clock_.now_ms()) - static_cast<double>(speech_end_ms_)) / 1000.0);
    } else {
        std::snprintf(prefix, sizeof(prefix), "Voice: ");
    }
    logger_->log(LogLevel::Info, (prefix + line).c_str());
}

void VoiceController::set_state(VoiceState state)
{
    status_.state = state;
    state_since_ms_ = clock_.now_ms();
    if (listener_) listener_(status_);
}

void VoiceController::fail(const std::string& error)
{
    log("error: " + error);
    ++generation_;
    if (capturing_) {
        audio_.stop_capture();
        capturing_ = false;
    }
    if (playback_started_) audio_.stop_playback();
    status_.error = error;
    set_state(VoiceState::Error);
}

void VoiceController::button()
{
    switch (status_.state) {
    case VoiceState::Idle:
    case VoiceState::Done:
    case VoiceState::Error: start(); break;
    case VoiceState::Listening: stop_listening(); break;
    case VoiceState::Processing:
    case VoiceState::Speaking: cancel(); break;
    }
}

bool VoiceController::start_run(bool follow_up)
{
    if (!enabled_) return false;
    follow_ups_ = follow_up ? follow_ups_ + 1 : 0;
    HaClient* client = client_provider_ ? client_provider_() : nullptr;
    if (!client || client->state() != HaClientState::Ready) {
        status_ = VoiceStatus{};
        fail("Home Assistant is not connected");
        return false;
    }
    if (client != registered_client_) {
        // HaClient has no listener removal; it lives as long as the app.
        client->add_event_listener([this](const DecodedMessage& msg) { on_ha_message(msg); });
        registered_client_ = client;
    }

    const uint32_t gen = ++generation_;
    status_ = VoiceStatus{};
    run_id_ = 0;
    handler_id_ = -1;
    audio_ended_ = false;
    playback_started_ = false;
    run_ended_ = false;
    tts_url_.clear();
    tts_stream_response_ = false;
    continue_conversation_ = false;
    speech_end_ms_ = 0;
    playback_result_ = 0;
    stt_phase_ = true;
    endpoint_reached_ = false;
    {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        buffered_.clear();
        overflowed_ = false;
        endpointer_.reset();
        band_levels_reset(&mic_bands_);
    }

    // Capture first: the user starts talking right after pressing the
    // button, before HA has answered with the binary handler id.
    capturing_ = audio_.start_capture([this, gen](const int16_t* samples, size_t count) {
        if (generation_.load() != gen) return;
        double sq = 0;
        for (size_t i = 0; i < count; ++i) sq += static_cast<double>(samples[i]) * samples[i];
        const double rms = count ? std::sqrt(sq / count) : 0;
        // ~-60 dBFS -> 0, ~-20 dBFS -> 1 (log scale, what the eye expects).
        const double db = rms > 0 ? 20.0 * std::log10(rms / 32768.0) : -100.0;
        level_ = static_cast<float>(std::fmin(1.0, std::fmax(0.0, (db + 60.0) / 40.0)));
        std::lock_guard<std::mutex> lock(buf_mutex_);
        if (endpointer_.feed(samples, count)) endpoint_reached_ = true;
        band_levels_feed(&mic_bands_, samples, count);
        if (buffered_.size() + count > kMaxBufferedSamples) {
            overflowed_ = true;
            return;
        }
        buffered_.insert(buffered_.end(), samples, samples + count);
    });
    if (!capturing_) {
        fail("microphone unavailable");
        return false;
    }

    run_started_ms_ = clock_.now_ms();
    // Step 1: speech-to-text only; see on_event("stt-end").
    const std::string cmd = R"({"type":"assist_pipeline/run","start_stage":"stt","end_stage":"stt",)"
                            R"("input":{"sample_rate":16000},"timeout":60)" +
                            run_options() + "}";
    client->send_command(
        cmd,
        [this, gen](const ResultMessage& r) {
            if (generation_.load() != gen || r.success) return;
            fail("pipeline did not start: " + (r.error_message.empty() ? r.error_code : r.error_message));
        },
        10000, &run_id_);
    if (!run_id_) {
        fail("could not send the pipeline command");
        return false;
    }
    log(std::string(follow_up ? "listening for the answer" : "listening") + " (run " + std::to_string(run_id_) + ")");
    play_earcon(earcon_style_, EARCON_START); // the microphone ignores it (IVoiceAudio::play_pcm)
    set_state(VoiceState::Listening);
    return true;
}

std::string VoiceController::run_options() const
{
    std::string opts;
    // Continue HA's chat session if it is still alive, so "turn it back on"
    // knows what "it" is. HA's ids are ULIDs; anything else is not sent.
    if (safe_id(conversation_id_) && clock_.now_ms() - conversation_ms_ < kConversationKeepMs) {
        opts += R"(,"conversation_id":")" + conversation_id_ + "\"";
    }
    const std::string pipeline = pipeline_id_ ? pipeline_id_() : "";
    if (safe_id(pipeline)) opts += R"(,"pipeline":")" + pipeline + "\"";
    const std::string device = device_id_ ? device_id_() : "";
    if (safe_id(device)) opts += R"(,"device_id":")" + device + "\"";
    return opts;
}

void VoiceController::send_intent_run(const std::string& text)
{
    // Step 2: the transcript goes to the conversation agent, the answer to TTS.
    cJSON* input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "text", text.c_str());
    char* input_json = cJSON_PrintUnformatted(input);
    cJSON_Delete(input);
    const std::string cmd = std::string(R"({"type":"assist_pipeline/run","start_stage":"intent","end_stage":"tts",)") +
                            R"("timeout":60,"input":)" + (input_json ? input_json : "{}") + run_options() + "}";
    cJSON_free(input_json);
    const uint32_t gen = generation_.load();
    stt_phase_ = false;
    uint32_t id = 0;
    registered_client_->send_command(
        cmd,
        [this, gen](const ResultMessage& r) {
            if (generation_.load() != gen || r.success) return;
            fail("pipeline did not start: " + (r.error_message.empty() ? r.error_code : r.error_message));
        },
        10000, &id);
    if (!id) {
        fail("could not send the pipeline command");
        return;
    }
    run_id_ = id; // events of the finished STT run (its run-end) are ignored from now on
}

void VoiceController::stop_listening()
{
    if (status_.state != VoiceState::Listening) return;
    flush_audio();
    finish_audio();
    set_state(VoiceState::Processing);
}

void VoiceController::cancel()
{
    if (status_.state == VoiceState::Idle) return;
    ++generation_;
    if (capturing_) {
        audio_.stop_capture();
        capturing_ = false;
    }
    if (playback_started_) audio_.stop_playback();
    // Tell HA the audio is over so it does not wait for more.
    if (handler_id_ >= 0 && !audio_ended_) finish_audio(false);
    run_id_ = 0;
    status_ = VoiceStatus{};
    set_state(VoiceState::Idle);
}

void VoiceController::finish_audio(bool sound)
{
    if (capturing_) {
        audio_.stop_capture();
        capturing_ = false;
    }
    level_ = 0.0f;
    if (audio_ended_) return;
    audio_ended_ = true;
    speech_end_ms_ = clock_.now_ms();
    if (sound) play_earcon(earcon_style_, EARCON_END);
    if (handler_id_ >= 0 && registered_client_) {
        // A frame with only the handler id ends the audio stream.
        registered_client_->send_binary(std::string(1, static_cast<char>(handler_id_)));
    }
    log("end of audio sent");
}

void VoiceController::flush_audio()
{
    if (handler_id_ < 0 || !registered_client_ || audio_ended_) return;
    std::vector<int16_t> pending;
    {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        pending.swap(buffered_);
        if (overflowed_) {
            overflowed_ = false;
            log("audio buffer overflowed (HA too slow to start); dropped audio");
        }
    }
    for (size_t off = 0; off < pending.size(); off += kChunkSamples) {
        const size_t n = std::min(kChunkSamples, pending.size() - off);
        std::string frame(1, static_cast<char>(handler_id_));
        frame.append(reinterpret_cast<const char*>(pending.data() + off), n * sizeof(int16_t));
        if (!registered_client_->send_binary(std::move(frame))) {
            fail("audio could not be sent to Home Assistant");
            return;
        }
    }
}

void VoiceController::start_playback(const std::string& url)
{
    if (playback_started_ || url.empty()) return;
    playback_started_ = true;
    const std::string full = url.rfind("http", 0) == 0 ? url : ha_base_url_() + url;
    const uint32_t gen = generation_.load();
    playback_generation_ = gen;
    log("playing response from " + url);
    const bool ok = audio_.play_url(full, [this, gen](bool success, const std::string& error) {
        if (generation_.load() != gen) return;
        {
            std::lock_guard<std::mutex> lock(buf_mutex_);
            playback_error_ = error;
        }
        playback_result_ = success ? 1 : 2;
    });
    if (!ok) {
        fail("response audio could not be played");
        return;
    }
    if (status_.state != VoiceState::Error) set_state(VoiceState::Speaking);
}

void VoiceController::on_ha_message(const DecodedMessage& msg)
{
    if (msg.kind != DecodedMessageKind::RawEvent || run_id_ == 0 ||
        static_cast<uint32_t>(msg.event_subscription_id) != run_id_) {
        return;
    }
    PipelineEvent ev;
    if (parse_pipeline_event(msg.raw_event_json, ev)) on_event(ev);
}

void VoiceController::on_event(const PipelineEvent& ev)
{
    if (status_.state == VoiceState::Idle || status_.state == VoiceState::Error) return;
    if (ev.type == "run-start") {
        handler_id_ = ev.stt_binary_handler_id;
        if (!ev.conversation_id.empty()) {
            conversation_id_ = ev.conversation_id;
            conversation_ms_ = clock_.now_ms();
        }
        tts_url_ = ev.tts_url;
        tts_stream_response_ = ev.tts_stream_response;
        log("run-start (handler " + std::to_string(handler_id_) + ", tts " + ev.tts_mime +
            (ev.tts_stream_response ? ", streamed" : "") + ")");
        flush_audio(); // what was said before HA was ready
        if (audio_ended_ && handler_id_ >= 0 && registered_client_) {
            // The user released before run-start: send the end marker now.
            registered_client_->send_binary(std::string(1, static_cast<char>(handler_id_)));
        }
    } else if (ev.type == "stt-vad-end") {
        log("end of speech detected by HA");
        if (status_.state == VoiceState::Listening) {
            flush_audio();
            finish_audio();
            set_state(VoiceState::Processing);
        }
    } else if (ev.type == "stt-end") {
        status_.transcript = ev.text;
        log("transcript: " + ev.text);
        if (status_.state == VoiceState::Listening) {
            flush_audio();
            finish_audio();
            set_state(VoiceState::Processing);
        } else if (listener_) {
            listener_(status_);
        }
        if (stt_phase_) {
            if (ev.text.find_first_not_of(" .,!?") == std::string::npos) {
                fail("No text recognized");
                return;
            }
            if (is_cancel_phrase(ev.text.c_str())) {
                // "Stopp" after a false wake word: nothing goes to the agent.
                log("cancel phrase: stop listening");
                cancel();
                return;
            }
            send_intent_run(ev.text);
        }
    } else if (ev.type == "intent-progress") {
        if (ev.tts_start_streaming) {
            log("response audio starts streaming");
            start_playback(tts_url_);
        }
    } else if (ev.type == "intent-end") {
        status_.response = ev.text;
        if (!ev.conversation_id.empty()) conversation_id_ = ev.conversation_id;
        conversation_ms_ = clock_.now_ms();
        continue_conversation_ = ev.continue_conversation;
        log("response: " + ev.text + (ev.continue_conversation ? " [expects an answer]" : ""));
        if (listener_) listener_(status_);
    } else if (ev.type == "tts-end") {
        log("tts-end");
        start_playback(ev.tts_url.empty() ? tts_url_ : ev.tts_url);
    } else if (ev.type == "error") {
        if (follow_ups_ > 0 && ev.error_code == "stt-no-text-recognized") {
            // Nobody answered the agent's question: just close.
            log("no answer to the follow-up question");
            ++generation_;
            if (capturing_) {
                audio_.stop_capture();
                capturing_ = false;
            }
            set_state(VoiceState::Done);
            return;
        }
        fail(ev.error_message.empty() ? ev.error_code : ev.error_message);
    } else if (ev.type == "run-end") {
        if (stt_phase_) return; // the STT step ended without a transcript event; its error decides
        log("run-end");
        run_ended_ = true;
        if (!playback_started_ && status_.state != VoiceState::Error) finish_run();
    } else {
        log(ev.type);
    }
}

void VoiceController::finish_run()
{
    if (continue_conversation_ && follow_ups_ < kMaxFollowUps) {
        // The agent asked something: listen for the answer right away, in
        // the same chat session (as HA's own satellites do).
        start_run(true);
        return;
    }
    set_state(VoiceState::Done);
}

void VoiceController::poll(uint64_t now_ms)
{
    if (start_requested_.exchange(false) && enabled_) {
        const VoiceState s = status_.state;
        if (s == VoiceState::Idle || s == VoiceState::Done || s == VoiceState::Error) {
            log("wake word");
            start();
            // `now_ms` predates start(): the state timers below would
            // underflow. Next poll.
            return;
        }
    }
    // Guard against a `now_ms` older than the state change (same reason).
    const uint64_t in_state = now_ms > state_since_ms_ ? now_ms - state_since_ms_ : 0;
    const uint64_t in_run = now_ms > run_started_ms_ ? now_ms - run_started_ms_ : 0;
    switch (status_.state) {
    case VoiceState::Listening:
        flush_audio();
        if (endpoint_reached_.exchange(false)) {
            // The user is done although HA may still hear background voices
            // (TV): end the audio ourselves.
            log("end of speech detected on the device");
            stop_listening();
            break;
        }
        if (in_state > kMaxListenMs) {
            log("listening time limit reached");
            stop_listening();
        }
        break;
    case VoiceState::Processing:
        if (in_run > kRunTimeoutMs) fail("Home Assistant did not answer in time");
        break;
    case VoiceState::Speaking:
        if (int r = playback_result_.load()) {
            playback_result_ = 0;
            if (r == 2) {
                std::string error;
                {
                    std::lock_guard<std::mutex> lock(buf_mutex_);
                    error = playback_error_;
                }
                fail("playback failed: " + error);
            } else {
                log("playback finished");
                finish_run();
            }
        }
        break;
    case VoiceState::Done:
    case VoiceState::Error:
        if (in_state > kDoneHoldMs) {
            status_ = VoiceStatus{};
            set_state(VoiceState::Idle);
        }
        break;
    case VoiceState::Idle: break;
    }
}

} // namespace app::voice
