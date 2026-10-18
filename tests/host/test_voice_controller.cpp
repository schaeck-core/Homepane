// VoiceController against a simulated HA connection (HaClient over
// FakeWebSocketTransport) and fake audio: the whole push-to-talk / wake word
// flow, including regressions seen on the device.

#include "../../app/cpp/home_assistant/DeviceLink.h"
#include "../../app/cpp/home_assistant/HaClient.h"
#include "../../app/cpp/voice/VoiceController.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using app::home_assistant::HaClient;
using app::net::WebSocketConnection;
using app::voice::VoiceController;
using app::voice::VoiceState;
using test_fakes::FakeWebSocketTransport;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

class FakeClock final : public app::core::IMonotonicClock {
public:
    uint64_t now_ms() const override { return now; }
    uint64_t now = 1000;
};

class FakeAudio final : public app::voice::IVoiceAudio {
public:
    bool start_capture(CaptureSink s) override
    {
        sink = std::move(s);
        ++starts;
        return true;
    }
    void stop_capture() override { sink = nullptr; }
    bool play_url(const std::string& url, PlaybackDone d) override
    {
        played_urls.push_back(url);
        done = std::move(d);
        return true;
    }
    void stop_playback() override { ++stops; }
    bool play_pcm(std::vector<int16_t> samples) override
    {
        earcons.push_back(samples.size());
        return true;
    }
    void speak(int16_t amplitude, size_t samples)
    {
        std::vector<int16_t> v(samples, amplitude);
        for (size_t i = 0; i < v.size(); i += 2) v[i] = static_cast<int16_t>(-amplitude); // AC signal
        if (sink) sink(v.data(), v.size());
    }

    CaptureSink sink;
    PlaybackDone done;
    std::vector<std::string> played_urls;
    std::vector<size_t> earcons;
    int starts = 0, stops = 0;
};

struct Fixture {
    FakeClock clock;
    FakeAudio audio;
    FakeWebSocketTransport* transport = nullptr;
    std::unique_ptr<HaClient> client;
    std::unique_ptr<VoiceController> voice;
    std::string pipeline;

    Fixture()
    {
        auto t = std::make_unique<FakeWebSocketTransport>();
        transport = t.get();
        client = std::make_unique<HaClient>(std::make_unique<WebSocketConnection>("ha", std::move(t), clock), clock,
                                            "token");
        client->start("ws://fake/api/websocket");
        transport->simulate_open();
        transport->simulate_fragment(R"({"type":"auth_ok","ha_version":"2026.9.1"})", true);
        voice = std::make_unique<VoiceController>(
            audio, clock, [this] { return client.get(); }, [] { return std::string("http://ha:8123"); },
            [this] { return pipeline; });
    }

    // id of the last command HaClient sent
    int last_id() const
    {
        const std::string& m = transport->sent_messages.back();
        const size_t p = m.find("\"id\":");
        return p == std::string::npos ? -1 : std::stoi(m.substr(p + 5));
    }
    const std::string& last_sent() const { return transport->sent_messages.back(); }
    void result_ok(int id) { transport->simulate_fragment(R"({"id":)" + std::to_string(id) + R"(,"type":"result","success":true,"result":null})", true); }
    void event(int id, const std::string& type, const std::string& data = "{}")
    {
        transport->simulate_fragment(R"({"id":)" + std::to_string(id) + R"(,"type":"event","event":{"type":")" + type +
                                         R"(","data":)" + data + "}}",
                                     true);
    }
    void poll(uint64_t advance = 33)
    {
        clock.now += advance;
        voice->poll(clock.now);
    }
};

void test_full_request()
{
    Fixture fx;
    fx.voice->button();
    expect(fx.voice->status().state == VoiceState::Listening, "button: listening");
    const int stt = fx.last_id();
    expect(fx.last_sent().find(R"("start_stage":"stt","end_stage":"stt")") != std::string::npos,
           "step 1 asks HA for speech-to-text only");
    expect(fx.audio.earcons.size() == 1, "a start sound plays");

    fx.audio.speak(4000, 3200); // spoken before HA is ready: buffered
    expect(fx.transport->sent_binary.empty(), "no audio before HA sent the binary handler id");
    fx.result_ok(stt);
    fx.event(stt, "run-start", R"({"conversation_id":"01CONV","runner_data":{"stt_binary_handler_id":3}})");
    fx.poll();
    expect(!fx.transport->sent_binary.empty() && fx.transport->sent_binary[0][0] == 3,
           "buffered audio goes out as binary frames with the handler id prefix");

    fx.event(stt, "stt-vad-end");
    expect(fx.voice->status().state == VoiceState::Processing, "HA's end of speech: processing");
    expect(fx.transport->sent_binary.back() == std::string(1, '\x03'), "end of audio = a frame with only the handler id");
    expect(fx.audio.earcons.size() == 2, "an end sound plays");

    fx.event(stt, "stt-end", R"({"stt_output":{"text":"Mach das \"Licht\" an"}})");
    const int intent = fx.last_id();
    expect(intent != stt && fx.last_sent().find(R"("start_stage":"intent","end_stage":"tts")") != std::string::npos,
           "step 2: the transcript goes to intent + tts");
    expect(fx.last_sent().find(R"(Mach das \"Licht\" an)") != std::string::npos, "the transcript is JSON-escaped");
    expect(fx.last_sent().find(R"("conversation_id":"01CONV")") != std::string::npos,
           "step 2 continues the conversation of step 1");

    fx.event(stt, "run-end"); // the finished STT run must not end the request
    expect(fx.voice->status().state == VoiceState::Processing, "run-end of the STT step is ignored");

    fx.result_ok(intent);
    fx.event(intent, "run-start", R"({"tts_output":{"url":"/api/tts_proxy/x.mp3","mime_type":"audio/mpeg"}})");
    fx.event(intent, "intent-progress", R"({"tts_start_streaming":true})");
    expect(fx.audio.played_urls.size() == 1 && fx.audio.played_urls[0] == "http://ha:8123/api/tts_proxy/x.mp3",
           "the response streams from HA's URL as soon as HA starts producing it");
    expect(fx.voice->status().state == VoiceState::Speaking, "speaking");
    fx.event(intent, "intent-end", R"({"intent_output":{"response":{"speech":{"plain":{"speech":"Erledigt"}}}}})");
    fx.event(intent, "tts-end", R"({"tts_output":{"url":"/api/tts_proxy/x.mp3"}})");
    expect(fx.audio.played_urls.size() == 1, "tts-end does not start the response a second time");
    fx.event(intent, "run-end");
    fx.audio.done(true, "");
    fx.poll();
    expect(fx.voice->status().state == VoiceState::Done && fx.voice->status().response == "Erledigt",
           "playback over: done, with the response text");
    fx.poll(VoiceController::kDoneHoldMs + 1);
    expect(fx.voice->status().state == VoiceState::Idle, "back to idle after the hold time");
}

void test_cancel_phrase()
{
    Fixture fx;
    fx.voice->button();
    const int stt = fx.last_id();
    fx.event(stt, "run-start", R"({"runner_data":{"stt_binary_handler_id":1}})");
    const size_t sent = fx.transport->sent_messages.size();
    fx.event(stt, "stt-end", R"({"stt_output":{"text":" Stopp!"}})");
    expect(fx.transport->sent_messages.size() == sent, "a cancel phrase is not sent to the agent");
    expect(fx.voice->status().state == VoiceState::Idle, "a cancel phrase closes the assistant");
}

void test_wake_word_start_inside_poll()
{
    Fixture fx;
    fx.clock.now = 50000;
    fx.voice->request_start(); // from the audio task
    fx.voice->poll(fx.clock.now - 10); // now_ms older than the start (the device's real order)
    expect(fx.voice->status().state == VoiceState::Listening, "wake word starts the assistant");
    fx.voice->poll(fx.clock.now - 5);
    expect(fx.voice->status().state == VoiceState::Listening,
           "a stale now_ms does not end the listening at once (unsigned underflow regression)");
}

void test_follow_up_question()
{
    Fixture fx;
    fx.voice->button();
    const int stt = fx.last_id();
    fx.event(stt, "run-start", R"({"conversation_id":"01ABC","runner_data":{"stt_binary_handler_id":1}})");
    fx.event(stt, "stt-end", R"({"stt_output":{"text":"Licht an"}})");
    const int intent = fx.last_id();
    fx.event(intent, "run-start", R"({"tts_output":{"url":"/t.mp3"}})");
    fx.event(intent, "intent-end",
             R"({"intent_output":{"response":{"speech":{"plain":{"speech":"Welches Licht?"}}},"conversation_id":"01ABC","continue_conversation":true}})");
    fx.event(intent, "tts-end", R"({"tts_output":{"url":"/t.mp3"}})");
    fx.event(intent, "run-end");
    const size_t sent = fx.transport->sent_messages.size();
    fx.audio.done(true, "");
    fx.poll();
    expect(fx.voice->status().state == VoiceState::Listening, "a question from the agent: listening again");
    expect(fx.transport->sent_messages.size() == sent + 1 &&
               fx.last_sent().find(R"("conversation_id":"01ABC")") != std::string::npos,
           "the answer goes into the same conversation");

    // Nobody answers: close quietly, not as an error.
    const int stt2 = fx.last_id();
    fx.event(stt2, "error", R"({"code":"stt-no-text-recognized","message":"No text recognized"})");
    expect(fx.voice->status().state == VoiceState::Done, "silence after a follow-up question just closes");
}

void test_pipeline_selection_and_errors()
{
    Fixture fx;
    fx.pipeline = "01pipeline";
    fx.voice->button();
    expect(fx.last_sent().find(R"("pipeline":"01pipeline")") != std::string::npos, "the selected HA assistant is used");
    fx.voice->cancel();
    fx.pipeline = "bad\"id";
    fx.voice->button();
    expect(fx.last_sent().find("bad") == std::string::npos, "ids that are not plain ULIDs/slugs are never put into JSON");

    Fixture off;
    off.voice->set_enabled(false);
    off.voice->button();
    expect(off.voice->status().state == VoiceState::Idle, "a disabled assistant does not start");

    Fixture down;
    down.transport->simulate_closed("gone");
    down.voice->button();
    expect(down.voice->status().state == VoiceState::Error, "without HA: an error, not a hang");
}

void test_device_link()
{
    using app::home_assistant::DeviceLink;
    const std::string devices = R"([{"id":"aaa","identifiers":[["zha","x"]],"area_id":null},)"
                                R"({"id":"b2fc","identifiers":[["mqtt","homepane_a1b2c3"]],"area_id":"wohnzimmer"}])";
    std::string area;
    expect(DeviceLink::find_device_id(devices, "homepane_a1b2c3", &area) == "b2fc" && area == "wohnzimmer",
           "the panel's HA device is found by its MQTT identifier");
    expect(DeviceLink::find_device_id(devices, "homepane_other").empty(), "another node id: not found");
    auto areas = DeviceLink::parse_areas(R"([{"area_id":"kueche","name":"Küche"},{"area_id":"bad","name":"Bad"}])");
    expect(areas.size() == 2 && areas[0].id == "kueche" && areas[0].name == "Küche", "areas parsed");
    const std::vector<app::home_assistant::Area> rooms = {{"wohn", "Wohn"}, {"wohnzimmer", "Wohnzimmer"}, {"living_room", "Living Room"}};
    auto* m = DeviceLink::match_dashboard(rooms, "dashboard-wohnzimmer");
    expect(m && m->id == "wohnzimmer", "default room: the longest area matching the dashboard path");
    m = DeviceLink::match_dashboard(rooms, "panel-living-room");
    expect(m && m->id == "living_room", "default room: '_' and spaces match '-'");
    expect(!DeviceLink::match_dashboard(rooms, "dashboard-test"), "default room: no match, no room");

    Fixture fx;
    std::string device = "0123456789abcdef0123456789abcdef";
    fx.voice->set_device_id_provider([&] { return device; });
    fx.voice->button();
    expect(fx.last_sent().find(R"("device_id":"0123456789abcdef0123456789abcdef")") != std::string::npos,
           "voice requests carry the panel's device id (HA then uses its area)");
}

} // namespace

int main()
{
    test_full_request();
    test_cancel_phrase();
    test_wake_word_start_inside_poll();
    test_follow_up_question();
    test_pipeline_selection_and_errors();
    test_device_link();
    if (g_failures == 0) {
        std::printf("All voice controller tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
