#ifndef APP_CPP_MUSIC_ASSISTANT_MAPLAYERSTORE_H
#define APP_CPP_MUSIC_ASSISTANT_MAPLAYERSTORE_H

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace app::music_assistant {

// A picture of a media item. MA serves every picture through its image
// proxy (/imageproxy/<proxy_id>?size=80|160|256|512|1024&fmt=jpeg, no
// login needed), whether it lies on the server or at a streaming service.
struct MaImage {
    std::string proxy_id;
    bool empty() const { return proxy_id.empty(); }
};

// One entry of a queue (also the queue's current and next item).
struct MaQueueItem {
    std::string queue_item_id;
    std::string title;   // the track's name ("Automatic"); the item's name for radio etc.
    std::string artist;  // "DON BROCO" (artists joined with ", ")
    std::string album;
    std::string uri;     // the media item ("spotify://track/...")
    std::string media_type; // "track", "radio", ...
    std::string item_id;    // the media item at its provider ("library": the library's id)
    std::string provider;   // "library", "spotify--yb8Wet3Z", ...
    std::string owner;      // playlists: who made it
    int year = 0;           // albums: the release year; 0: unknown
    bool dynamic = false;   // playlists: made by MA as they play (no similar music from them)
    std::vector<std::string> mappings; // the provider instances that have it
    double duration = 0; // seconds; 0: none (radio)
    bool favorite = false;
    MaImage image;
};

struct MaPlayer {
    std::string id;
    std::string name;
    std::string type;  // "player", "group"
    bool available = false;
    std::string playback_state; // "idle", "paused", "playing"
    std::optional<int> volume;  // 0..100; none: no volume control
    bool muted = false;
    std::vector<std::string> group_members; // with this player as the leader (incl. itself)
    std::vector<std::string> can_group_with;
    std::string synced_to;     // the leader when this player follows another
    std::string active_source; // the queue it plays (normally its own id)
};

struct MaQueue {
    std::string id;
    std::string display_name;
    bool available = false;
    bool active = false;
    int items = 0;
    bool shuffle = false;
    std::string repeat = "off"; // "off", "one", "all"
    std::string state;          // "idle", "paused", "playing"
    int current_index = -1;
    double elapsed = 0;            // seconds into the current item ...
    double elapsed_updated_at = 0; // ... at this time (epoch seconds)
    std::optional<MaQueueItem> current;
    std::optional<MaQueueItem> next;
};

// Music Assistant's players and their queues: filled from players/all and
// player_queues/all, kept current by the server's events. Pure (cJSON; no
// LVGL, no network): host-tested.
class MaPlayerStore {
public:
    // result JSON of players/all / player_queues/all (arrays)
    void load_players(const std::string& json);
    void load_queues(const std::string& json);
    // An event; returns true when it changed something here.
    // now: epoch seconds (for queue_time_updated).
    bool apply_event(const std::string& type, const std::string& object_id, const std::string& data_json, double now);

    const std::vector<MaPlayer>& players() const { return players_; }
    const MaPlayer* player(const std::string& id) const;
    const MaQueue* queue(const std::string& id) const;
    // the queue a player shows: its leader's when it is grouped
    const MaQueue* queue_of(const std::string& player_id) const;
    // seconds into the current item at `now`, while playing still counting
    static double elapsed_at(const MaQueue& q, double now);

    // parsers (also for queue items, search results)
    static MaQueueItem parse_queue_item(const void* cjson_item);
    static MaImage parse_image(const void* cjson_image);

private:
    std::vector<MaPlayer> players_;
    std::map<std::string, MaQueue> queues_;
};

// "ws://host:8095/ws" -> "http://host:8095"
std::string ma_http_base(const std::string& ws_url);
// The image proxy URL of a picture in one of MA's sizes (80, 160, 256, 512).
std::string ma_image_url(const std::string& http_base, const MaImage& image, int size);

// The user's row settings of MA's home page (user preference "discover.rows",
// from auth/me), as MA's frontend applies them (helpers/rowsConfig.ts): a
// row is hidden when the user hid it, or MA hides it by default and the user
// did not show it; the user's order, else in progress and recently played
// first, then the server's order. Row ids are the folder uris ("top_picks"
// for the top picks).
struct MaRowsPreference {
    std::vector<std::string> order, hidden, shown;

    static MaRowsPreference parse(const std::string& preferences_json);
    bool visible(const std::string& id, bool enabled_by_default) const;
    size_t rank(const std::string& id) const; // smaller first
};

} // namespace app::music_assistant

#endif // APP_CPP_MUSIC_ASSISTANT_MAPLAYERSTORE_H
