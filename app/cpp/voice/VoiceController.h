#ifndef APP_CPP_VOICE_VOICECONTROLLER_H
#define APP_CPP_VOICE_VOICECONTROLLER_H

#include "../core/ILogger.h"
#include "../core/IMonotonicClock.h"
#include "../home_assistant/HaClient.h"
#include "IVoiceAudio.h"
#include "PipelineEvent.h"
#include "SpeechEndpointer.h"
#include "BandLevels.h"
#include "earcons.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace app::voice {

enum class VoiceState {
    Idle,
    Listening,  // microphone streams to HA (run started, waiting for/inside speech)
    Processing, // speech ended; STT / intent running
    Speaking,   // response audio playing
    Done,       // finished; shown briefly, then Idle
    Error,
};

struct VoiceStatus {
    VoiceState state = VoiceState::Idle;
    std::string transcript; // what HA understood
    std::string response;   // what HA answers
    std::string error;
};

// Push-to-talk voice assistant over the existing HA WebSocket connection
// (assist_pipeline/run, stt -> tts; docs/voice/VOICE_ASSISTANT_PLAN.md,
// way A). Microphone audio goes to HA as binary frames; the response audio
// is streamed from HA's TTS URL as soon as HA starts producing it.
//
// UI thread only (start/stop/cancel/poll and the listener), except that the
// IVoiceAudio callbacks arrive on audio tasks and are handed over through a
// mutex-protected buffer / atomics.
class VoiceController {
public:
    using ClientProvider = std::function<app::home_assistant::HaClient*()>;
    using Listener = std::function<void(const VoiceStatus&)>;

    static constexpr uint64_t kMaxListenMs = 12000;     // then end the audio ourselves
    static constexpr uint64_t kRunTimeoutMs = 60000;    // whole run, without playback
    static constexpr uint64_t kDoneHoldMs = 4000;       // Done/Error shown, then Idle
    static constexpr size_t kChunkSamples = 1024;       // 64 ms per binary frame
    static constexpr size_t kMaxBufferedSamples = 16000 * 5; // before HA is ready
    // HA forgets a chat session after ~5 min without activity; follow-ups
    // within this window continue it ("turn it back on").
    static constexpr uint64_t kConversationKeepMs = 5 * 60 * 1000;
    // An agent can keep asking; stop listening again after this many
    // follow-ups in a row without a wake word / button.
    static constexpr int kMaxFollowUps = 3;

    using PipelinesCallback = std::function<void(bool ok, const PipelineList& list, const std::string& error)>;

    // `pipeline_id`: the HA assistant to use; empty = HA's default.
    VoiceController(IVoiceAudio& audio, app::core::IMonotonicClock& clock, ClientProvider client,
                    std::function<std::string()> ha_base_url, std::function<std::string()> pipeline_id,
                    app::core::ILogger* logger = nullptr);

    // Off: no button/wake word starts, a running request is cancelled.
    void set_enabled(bool enabled);
    // HA device id of the panel (its MQTT device): HA then takes the device's
    // area as the room for commands without one. Empty = not sent.
    void set_device_id_provider(std::function<std::string()> p) { device_id_ = std::move(p); }
    // Feedback sound style (earcon_style_t), -1 = no sounds.
    void set_earcon_style(int style) { earcon_style_ = style; }
    // Plays the start sound of `style` (settings preview).
    void preview_earcon(int style);
    bool enabled() const { return enabled_; }

    // Asks HA for its assistants; `done` runs on the UI thread.
    void fetch_pipelines(PipelinesCallback done);

    // Push-to-talk button: Idle/Done/Error -> start; Listening -> end of
    // speech; Processing/Speaking -> cancel.
    void button();
    bool start() { return start_run(false); }
    // Thread-safe (wake word detector on the audio task): starts on the next
    // poll() if the assistant is not already busy.
    void request_start() { start_requested_ = true; }
    void stop_listening();
    void cancel();

    void poll(uint64_t now_ms);

    const VoiceStatus& status() const { return status_; }
    // Microphone level 0..1 (for the listening animation).
    float level() const { return level_.load(); }
    // Response audio level 0..1 while speaking (animations).
    float output_level() const { return audio_.playback_level(); }
    // Low / mid / high levels 0..1: microphone while listening, else the
    // response audio (animations).
    void bands(float out[3]);
    void set_listener(Listener listener) { listener_ = std::move(listener); }

private:
    bool start_run(bool follow_up);
    void on_ha_message(const app::home_assistant::DecodedMessage& msg);
    void finish_run(); // playback over or run ended without audio
    void send_intent_run(const std::string& text);
    std::string run_options() const; // ,"conversation_id":...,"pipeline":...
    void on_event(const PipelineEvent& ev);
    void set_state(VoiceState state);
    void fail(const std::string& error);
    void finish_audio(bool sound = true);
    void start_playback(const std::string& url);
    void flush_audio();
    void log(const std::string& line);
    void play_earcon(int style, earcon_kind_t kind);

    IVoiceAudio& audio_;
    app::core::IMonotonicClock& clock_;
    ClientProvider client_provider_;
    std::function<std::string()> ha_base_url_;
    std::function<std::string()> pipeline_id_;
    std::function<std::string()> device_id_;
    bool enabled_ = true;
    int earcon_style_ = EARCON_STYLE_CHIME;
    app::core::ILogger* logger_;
    Listener listener_;

    app::home_assistant::HaClient* registered_client_ = nullptr;
    VoiceStatus status_;
    uint64_t state_since_ms_ = 0;
    uint64_t run_started_ms_ = 0;
    uint64_t speech_end_ms_ = 0;
    uint32_t run_id_ = 0;
    int handler_id_ = -1;
    bool audio_ended_ = false;
    bool capturing_ = false;
    bool playback_started_ = false;
    bool run_ended_ = false;
    std::string tts_url_;
    std::string conversation_id_;
    uint64_t conversation_ms_ = 0;
    bool continue_conversation_ = false;
    int follow_ups_ = 0;
    bool tts_stream_response_ = false;

    // A request runs in two steps: speech-to-text only, then (unless the
    // transcript is a cancel phrase like "Stopp") intent + TTS with the text.
    bool stt_phase_ = false;

    std::mutex buf_mutex_;
    SpeechEndpointer endpointer_;          // guarded by buf_mutex_
    band_levels_t mic_bands_{};            // guarded by buf_mutex_
    std::atomic<bool> endpoint_reached_{false};
    std::vector<int16_t> buffered_; // guarded by buf_mutex_
    bool overflowed_ = false;       // guarded by buf_mutex_
    std::atomic<float> level_{0.0f};
    std::atomic<int> playback_result_{0}; // 0 running/none, 1 ok, 2 failed
    std::string playback_error_;          // written before playback_result_
    std::atomic<bool> start_requested_{false};
    std::atomic<uint32_t> generation_{0}; // stale callbacks from a cancelled run are ignored
    uint32_t playback_generation_ = 0;
};

} // namespace app::voice

#endif // APP_CPP_VOICE_VOICECONTROLLER_H
