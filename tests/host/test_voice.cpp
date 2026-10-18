// Unit tests for the voice assistant's portable parts
// (docs/voice/VOICE_ASSISTANT_PLAN.md): Assist pipeline event parsing and the
// microphone AGC. No audio hardware, no network.

#include "app/cpp/voice/PipelineEvent.h"
#include "app/cpp/voice/SpeechAgc.h"
#include "app/cpp/voice/SpeechEndpointer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using app::voice::parse_pipeline_event;
using app::voice::PipelineEvent;
using app::voice::SpeechAgc;

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

void test_pipeline_events()
{
    PipelineEvent ev;
    // Shapes as HA sends them (assist_pipeline/run.py, conversation/models.py).
    expect(parse_pipeline_event(
               R"({"type":"run-start","data":{"pipeline":"p","language":"de","conversation_id":"01JABC",)"
               R"("runner_data":{"stt_binary_handler_id":3,"timeout":300},)"
               R"("tts_output":{"token":"t.mp3","url":"/api/tts_proxy/t.mp3","mime_type":"audio/mpeg","stream_response":true}}})",
               ev) &&
               ev.type == "run-start" && ev.stt_binary_handler_id == 3 && ev.tts_url == "/api/tts_proxy/t.mp3" &&
               ev.tts_mime == "audio/mpeg" && ev.tts_stream_response && ev.conversation_id == "01JABC",
           "run-start: handler id, early TTS URL, streaming flag, conversation id");

    expect(parse_pipeline_event(R"({"type":"stt-end","data":{"stt_output":{"text":" Wie viel Uhr ist es?"}}})", ev) &&
               ev.text == " Wie viel Uhr ist es?",
           "stt-end: transcript");

    expect(parse_pipeline_event(R"({"type":"intent-progress","data":{"tts_start_streaming":true}})", ev) &&
               ev.tts_start_streaming,
           "intent-progress: response audio starts streaming");

    expect(parse_pipeline_event(
               R"({"type":"intent-end","data":{"intent_output":{"response":{"speech":{"plain":{"speech":"Welches Licht?"}}},)"
               R"("conversation_id":"01JXYZ","continue_conversation":true}}})",
               ev) &&
               ev.text == "Welches Licht?" && ev.conversation_id == "01JXYZ" && ev.continue_conversation,
           "intent-end: response, conversation id, follow-up question flag");

    expect(parse_pipeline_event(R"({"type":"intent-end","data":{"intent_output":{"response":{}}}})", ev) &&
               !ev.continue_conversation && ev.text.empty(),
           "intent-end without speech or flag parses with defaults");

    expect(parse_pipeline_event(R"({"type":"error","data":{"code":"stt-no-text-recognized","message":"No text recognized"}})", ev) &&
               ev.error_code == "stt-no-text-recognized" && ev.error_message == "No text recognized",
           "error: code and message");

    expect(!parse_pipeline_event("not json", ev) && !parse_pipeline_event(R"({"data":{}})", ev),
           "garbage and events without a type are rejected");
}

std::vector<int16_t> tone(size_t n, float amplitude, float hz = 300.0f)
{
    std::vector<int16_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<int16_t>(amplitude * std::sin(2.0f * 3.14159265f * hz * i / 16000.0f));
    return v;
}

int16_t peak(const std::vector<int16_t>& v, size_t from = 0)
{
    int p = 0;
    for (size_t i = from; i < v.size(); ++i) p = std::max(p, std::abs(static_cast<int>(v[i])));
    return static_cast<int16_t>(p);
}

void run(SpeechAgc& agc, std::vector<int16_t>& v)
{
    for (size_t off = 0; off < v.size(); off += 320) agc.process(&v[off], std::min<size_t>(320, v.size() - off));
}

void test_agc()
{
    SpeechAgc agc;
    agc.reset();
    // Quiet room, then quiet speech (~-40 dBFS peaks) for 3 s.
    auto noise = tone(16000, 20.0f, 3000.0f);
    run(agc, noise);
    expect(agc.gain() <= SpeechAgc::kStartGain + 0.01f, "silence does not raise the gain (no noise pumping)");
    auto speech = tone(48000, 900.0f);
    run(agc, speech);
    expect(peak(speech, 32000) > 8000, "quiet speech is brought up towards the target level");
    expect(peak(speech) < 32767, "no hard clipping");

    agc.reset();
    // A single 20 ms click at full scale, then quiet speech: the click must
    // not duck the speech (the problem seen on the device).
    std::vector<int16_t> click = tone(320, 30000.0f, 1000.0f);
    run(agc, click);
    auto after = tone(32000, 900.0f);
    run(agc, after);
    expect(peak(after, 16000) > 6000, "a single click does not duck the speech that follows");

    agc.reset();
    // Sustained loud speech: gain backs off, output stays below full scale.
    auto loud = tone(32000, 20000.0f);
    run(agc, loud);
    expect(agc.gain() < 2.0f && peak(loud, 16000) <= 32767, "sustained loud input lowers the gain");
}

void test_endpointer()
{
    using app::voice::SpeechEndpointer;
    auto feed = [](SpeechEndpointer& ep, float amplitude, int blocks, float hz = 300.0f) {
        bool ended = false;
        for (int b = 0; b < blocks; ++b) {
            auto v = tone(320, amplitude, hz);
            if (ep.feed(v.data(), v.size())) ended = true;
        }
        return ended;
    };
    SpeechEndpointer ep;
    ep.reset();
    feed(ep, 20, 10);                       // quiet room
    bool early = feed(ep, 8000, 75);         // 1.5 s of speech
    bool tv = feed(ep, 600, 30);            // 600 ms of a TV ~22 dB below
    bool end = feed(ep, 600, 20);           // ... 1 s total
    expect(!early && !tv, "no end while speaking or within 0.8 s after");
    expect(end, "end detected with a TV still playing well below the speaker");

    ep.reset();
    feed(ep, 20, 10);
    expect(!feed(ep, 8000, 5) && !feed(ep, 20, 60), "a short noise (<300 ms) is not a finished request");

    ep.reset();
    feed(ep, 20, 10);
    feed(ep, 8000, 50);
    expect(!feed(ep, 4000, 100), "speech continuing at a similar level does not end");
}

void test_cancel_phrases()
{
    using app::voice::is_cancel_phrase;
    expect(is_cancel_phrase(" Stopp!") && is_cancel_phrase("Abbrechen.") && is_cancel_phrase("Vergiss es.") &&
               is_cancel_phrase("stop") && is_cancel_phrase("Never mind"),
           "cancel phrases are recognized despite case, punctuation and spaces");
    expect(!is_cancel_phrase("Stoppe die Musik") && !is_cancel_phrase("Halt die Tür offen") && !is_cancel_phrase(""),
           "commands that merely contain a cancel word are not cancel phrases");
}

} // namespace

int main()
{
    test_pipeline_events();
    test_agc();
    test_endpointer();
    test_cancel_phrases();
    if (g_failures == 0) {
        std::printf("All voice tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
