// MaPlayerStore: Music Assistant's players and queues from real (anonymized)
// server replies (tests/fixtures/ma, MA 2.10, schema 65) and its events.

#include "../../app/cpp/music_assistant/MaPlayerStore.h"

#include <cjson/cJSON.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace app::music_assistant;

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

std::string read(const char* path)
{
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
} // namespace

int main()
{
    {
        // the user's row settings, as MA's frontend applies them
        const MaRowsPreference none = MaRowsPreference::parse("{}");
        expect(none.visible("library://folder/trending", true), "rows: enabled by default is shown");
        expect(!none.visible("library://folder/favorite_radio", false), "rows: not enabled by default is hidden");
        expect(none.rank("library://folder/recently_played") < none.rank("library://folder/trending"),
               "rows: recently played before the server's other rows");
        const MaRowsPreference user = MaRowsPreference::parse(
            R"({"discover.rows":{"hidden":["library://folder/trending"],"shown":["library://folder/favorite_radio"],)"
            R"("order":["top_picks","library://folder/favorite_radio","library://folder/recently_played"]}})");
        expect(!user.visible("library://folder/trending", true), "rows: hidden by the user");
        expect(user.visible("library://folder/favorite_radio", false), "rows: shown by the user");
        expect(user.rank("library://folder/favorite_radio") < user.rank("library://folder/recently_played"),
               "rows: the user's order");
        expect(user.rank("library://folder/x") == 3, "rows: unknown rows after the ordered ones");
    }

    MaPlayerStore store;
    store.load_players(read("tests/fixtures/ma/players.json"));
    store.load_queues(read("tests/fixtures/ma/queues.json"));

    expect(store.players().size() == 4, "four players");
    const MaPlayer* tv = store.player("player_tv");
    expect(tv && tv->name == "TV Wohnzimmer" && tv->available && tv->volume && *tv->volume == 20 && tv->playback_state == "idle",
           "a player: name, available, volume 20, idle");
    const MaPlayer* bar = store.player("player_soundbar");
    expect(bar && !bar->volume, "a player without volume control has none");
    expect(tv && tv->can_group_with.size() == 2, "players it can play together with");

    const MaQueue* web = store.queue_of("player_web");
    expect(web && web->items == 56 && web->current && web->current->title == "Legends" &&
               web->current->artist == "Sleeping With Sirens" && web->current->album == "Legends" &&
               web->current->duration == 222 && !web->current->image.empty() && web->next &&
               web->next->artist == "Theory of a Deadman",
           "the queue: 56 items, current title / artist / album / length / picture, next item");

    // events
    const double now = 1791555500;
    expect(store.apply_event("queue_time_updated", "player_web", "42.5", now) && store.queue("player_web")->elapsed == 42.5,
           "queue_time_updated sets the position");
    expect(!store.apply_event("queue_time_updated", "nope", "1", now), "an unknown queue is ignored");
    expect(store.apply_event("player_updated", "player_tv",
                             R"({"player_id":"player_tv","name":"TV Wohnzimmer","available":true,"playback_state":"playing","volume_level":35,"group_members":["player_tv","player_soundbar"]})",
                             now) &&
               store.player("player_tv")->volume == 35 && store.player("player_tv")->group_members.size() == 2,
           "player_updated replaces the player");
    expect(!store.apply_event("media_item_played", "x", "{}", now), "other events change nothing");

    // the position goes on while playing
    MaQueue q;
    q.state = "playing";
    q.elapsed = 10;
    q.elapsed_updated_at = 100;
    expect(MaPlayerStore::elapsed_at(q, 105) == 15, "playing: the position counts on");
    q.state = "paused";
    expect(MaPlayerStore::elapsed_at(q, 105) == 10, "paused: it stays");

    expect(ma_http_base("ws://192.0.2.5:8095/ws") == "http://192.0.2.5:8095", "image base from the websocket URL");
    MaImage img;
    img.proxy_id = "abc";
    expect(ma_image_url("http://h:8095", img, 64) == "http://h:8095/imageproxy/abc?size=80&fmt=jpeg" &&
               ma_image_url("http://h:8095", img, 208) == "http://h:8095/imageproxy/abc?size=256&fmt=jpeg",
           "the proxy's next size up, as JPEG");

    {
        // a playlist / album as music/search and music/item_by_uri give it: what its page needs
        auto parse = [](const char* json) {
            cJSON* o = cJSON_Parse(json);
            MaQueueItem it = MaPlayerStore::parse_queue_item(o);
            cJSON_Delete(o);
            return it;
        };
        const MaQueueItem pl = parse(R"({"item_id":"12","provider":"library","name":"Kochen","media_type":"playlist",
            "uri":"library://playlist/12","owner":"Julia","favorite":true})");
        expect(pl.item_id == "12" && pl.provider == "library" && pl.owner == "Julia" && pl.favorite,
               "playlist: library id, owner, favourite");
        const MaQueueItem al = parse(R"({"name":"A Night at the Opera","media_type":"album","year":1975,
            "uri":"spotify--yb8Wet3Z://album/1GAkFO17jNudCOK0v1EgiF","artists":[{"name":"Queen"}]})");
        expect(al.provider == "spotify--yb8Wet3Z" && al.item_id == "1GAkFO17jNudCOK0v1EgiF" && al.year == 1975 &&
                   al.artist == "Queen",
               "album without item_id/provider: both from its uri; year");
        const MaQueueItem fs = parse(R"({"name":"x","media_type":"track","uri":"filesystem--a1://track/Rock/x.mp3"})");
        expect(fs.item_id == "Rock/x.mp3", "uri fallback: an id with slashes stays whole");
    }

    std::printf("%s\n", g_failures ? "FAILED" : "ALL OK");
    return g_failures ? 1 : 0;
}
