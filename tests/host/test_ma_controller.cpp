// MaController: the wait for music asked to play now (play_media with play
// or replace, play_shuffled) until the queue plays it — the player's loader.
// FakeWebSocketTransport plays the server, the fixtures in tests/fixtures/ma
// are its players and queues.

#include "../../app/cpp/music_assistant/MaClient.h"
#include "../../app/cpp/music_assistant/MaController.h"
#include "fakes/FakeWebSocketTransport.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace app::music_assistant;
using app::core::IMonotonicClock;
using app::net::WebSocketConnection;
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

class FakeClock final : public IMonotonicClock {
public:
    uint64_t now_ms() const override { return 1000; }
};

std::string read(const char* path)
{
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// The id of the last command sent with this name ("" if none).
std::string last_id(const FakeWebSocketTransport& t, const std::string& command)
{
    std::string id;
    for (const auto& m : t.sent_messages) {
        if (m.find("\"command\":\"" + command + "\"") == std::string::npos) continue;
        const size_t at = m.find("\"message_id\":\"");
        if (at != std::string::npos) id = m.substr(at + 14, m.find('"', at + 14) - at - 14);
    }
    return id;
}

void reply(FakeWebSocketTransport& t, const std::string& command, const std::string& result_json)
{
    t.simulate_fragment(R"({"message_id":")" + last_id(t, command) + R"(","result":)" + result_json + "}", true);
}

// The web player's queue as an event: its state and the item it plays.
std::string queue_event(const char* state, const char* item_id)
{
    return std::string(R"({"queue_id":"player_web","display_name":"Web","available":true,"active":true,"state":")") +
           state + R"(","current_item":{"queue_item_id":")" + item_id +
           R"(","media_item":{"name":"Song","uri":"spotify://track/1"}}})";
}

struct Fixture {
    FakeClock clock;
    FakeWebSocketTransport* transport = nullptr;
    std::unique_ptr<MaClient> client;
    std::unique_ptr<MaController> ma;
    double now = 1000; // epoch seconds for the controller

    Fixture()
    {
        auto t = std::make_unique<FakeWebSocketTransport>();
        transport = t.get();
        client = std::make_unique<MaClient>(std::make_unique<WebSocketConnection>("music_assistant", std::move(t), clock),
                                            clock, MaCredentials{}, "");
        ma = std::make_unique<MaController>(*client, [this] { return now; });
        ma->set_player("player_web");
        client->start("ws://ma.local:8095/ws");
        transport->simulate_open();
        // a server without auth: ready at once
        transport->simulate_fragment(
            R"({"server_id":"abc","server_version":"1.9.0","schema_version":20,"min_supported_schema_version":15})", true);
        ma->on_state(MaClientState::Ready);
        reply(*transport, "players/all", read("tests/fixtures/ma/players.json"));
        reply(*transport, "player_queues/all", read("tests/fixtures/ma/queues.json"));
    }
};

MaQueueItem playlist()
{
    MaQueueItem it;
    it.title = "Road Trip";
    it.uri = "spotify://playlist/42";
    it.media_type = "playlist";
    return it;
}

void test_waits_until_another_item_plays()
{
    Fixture fx;
    expect(fx.ma->ready() && fx.ma->queue() && fx.ma->queue()->id == "player_web", "the web player's queue is loaded");
    expect(fx.ma->starting() == nullptr, "nothing is starting before anything was asked");

    fx.ma->play_media(playlist(), MaController::Enqueue::Replace);
    expect(fx.ma->starting() && fx.ma->starting()->title == "Road Trip", "replacing the queue: it is starting");

    fx.ma->on_event("queue_updated", "player_web", queue_event("playing", "1387aefcea1b4d57857fd7f223a59112"));
    expect(fx.ma->starting() != nullptr, "the old item still playing: still starting");
    fx.ma->on_event("queue_updated", "player_web", queue_event("idle", "new-item"));
    expect(fx.ma->starting() != nullptr, "the new item, not playing yet (buffering): still starting");
    fx.ma->on_event("queue_updated", "player_web", queue_event("playing", "new-item"));
    expect(fx.ma->starting() == nullptr, "the new item plays: started");
}

void test_queueing_is_no_start()
{
    Fixture fx;
    fx.ma->play_media(playlist(), MaController::Enqueue::Next);
    fx.ma->play_media(playlist(), MaController::Enqueue::Add);
    expect(fx.ma->starting() == nullptr, "playing next or adding to the queue starts nothing now");
}

void test_errors_and_timeout()
{
    Fixture fx;
    fx.ma->play_media(playlist(), MaController::Enqueue::Play);
    reply(*fx.transport, "player_queues/play_media", "null");
    expect(fx.ma->starting() != nullptr, "MA's answer alone does not end it (the music may still load)");
    fx.transport->simulate_fragment(R"({"message_id":")" + last_id(*fx.transport, "player_queues/play_media") +
                                        R"(","error_code":999,"details":"nope"})",
                                    true);
    expect(fx.ma->starting() != nullptr, "an answer to an already answered command changes nothing");

    Fixture refused;
    refused.ma->play_media(playlist(), MaController::Enqueue::Play);
    refused.transport->simulate_fragment(R"({"message_id":")" +
                                             last_id(*refused.transport, "player_queues/play_media") +
                                             R"(","error_code":999,"details":"not available"})",
                                         true);
    expect(refused.ma->starting() == nullptr, "MA refuses it: no longer starting");

    Fixture slow;
    slow.ma->play_shuffled(playlist());
    expect(slow.ma->starting() != nullptr, "shuffle and play: starting");
    slow.now += MaController::kStartTimeout + 1;
    expect(slow.ma->starting() == nullptr, "after the timeout it no longer shows as starting");
}

} // namespace

int main()
{
    test_waits_until_another_item_plays();
    test_queueing_is_no_start();
    test_errors_and_timeout();
    if (g_failures == 0) {
        std::printf("All MaController tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
