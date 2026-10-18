#ifndef APP_CPP_MUSIC_ASSISTANT_MACONTROLLER_H
#define APP_CPP_MUSIC_ASSISTANT_MACONTROLLER_H

#include "MaClient.h"
#include "MaPlayerStore.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace app::music_assistant {

// The panel's view of Music Assistant: the players and queues (MaPlayerStore,
// loaded when the connection is ready and kept current by the server's
// events), the room's player, and what the player UI does with it. UI thread
// (MaClient delivers there); no LVGL.
class MaController {
public:
    using Listener = std::function<void()>;
    using ItemsDone = std::function<void(bool ok, std::vector<MaQueueItem> items, int total)>;

    // now: epoch seconds (queue positions)
    MaController(MaClient& client, std::function<double()> now);

    // The room's player (config ma_player_id); "" or unknown: the playing
    // one, else the first available.
    void set_player(const std::string& player_id);
    std::string player_id() const;
    const MaPlayer* player() const;
    const MaQueue* queue() const;
    const MaPlayerStore& store() const { return store_; }
    bool ready() const { return loaded_; }
    double elapsed() const; // seconds into the current item, now

    // Called on any change (players, queues, connection). Returns an id for remove.
    int add_listener(Listener l);
    void remove_listener(int id);

    // the room's player / its queue
    void play_pause();
    void next();
    void previous();
    void set_shuffle(bool on);
    void cycle_repeat(); // off -> all -> one -> off
    void seek(double seconds);
    void set_volume(int volume);
    void favorite_current(); // add the current item to the favorites
    // play also / no longer in another room (MA groups the players)
    void set_member(const std::string& other_player_id, bool on);
    bool is_member(const std::string& other_player_id) const;

    // queue page
    void load_queue_items(int offset, int limit, ItemsDone done);
    void play_queue_item(const std::string& queue_item_id);
    void remove_queue_item(const std::string& queue_item_id);
    void play_next(const std::string& queue_item_id, int index_in_queue);
    void start_radio(const std::string& uri); // similar music: radio mode from the item

    // search and browse; items are media items (MaQueueItem fields, no queue id)
    // a row of MA's suggestions (music/recommendations): its items come
    // separately (recommendation_items)
    struct Folder {
        std::string provider, item_id, name, subtitle, uri;
        bool shown = true; // MA's home page shows it (for this user)
    };
    using FoldersDone = std::function<void(std::vector<Folder> folders)>;
    // media_type: "" (all) or "track", "artist", "album", "playlist", "radio"
    void search(const std::string& query, const std::string& media_type, int limit, ItemsDone done);
    // music/<kind>/library_items: "playlists", "radios", "albums", "artists", "tracks",
    // "audiobooks", "podcasts"
    void library(const std::string& kind, int limit, ItemsDone done, int offset = 0);
    // a provider keeps such items in the library (as MA's menu shows them);
    // true while the providers are not known yet
    bool has_library(const std::string& kind) const;
    void recently_played(int limit, ItemsDone done);
    // MA's suggestion rows as MA's home page shows them to the panel's
    // user: the user's "discover.rows" preference (order, hidden, shown)
    // over MA's defaults; and the items of one row
    void recommendations(FoldersDone done);
    bool top_picks_shown() const { return top_picks_shown_; }
    const std::string& user() const { return user_; } // logged in as (auth/me)
    void recommendation_items(const Folder& folder, ItemsDone done);
    // how an item goes into the queue (MA's QueueOption)
    enum class Enqueue { Play, Replace, Next, Add };
    void play_media(const MaQueueItem& item, Enqueue how);
    void play_shuffled(const MaQueueItem& item); // shuffle on, then it replaces the queue

    // a playlist's, album's or artist's page: the item itself (owner, year,
    // favorite), its tracks (an artist's: the top tracks) and an artist's albums.
    // limit/offset since MA 2.11; older servers send all of them at once.
    using ItemDone = std::function<void(bool ok, MaQueueItem item)>;
    void item(const MaQueueItem& item, ItemDone done);
    void tracks(const MaQueueItem& item, int offset, int limit, ItemsDone done);
    void artist_albums(const MaQueueItem& artist, int limit, ItemsDone done);
    void set_favorite(const MaQueueItem& item, bool on);
    // MA can start similar music from it: one of its providers finds
    // similar tracks (a library item: any provider), as MA's frontend decides
    bool radio_supported(const MaQueueItem& item) const;
    static std::vector<MaQueueItem> parse_items(const std::string& json_array);

    // players and queues anew from the server (e.g. the settings' player list)
    void refresh() { if (client_.state() == MaClientState::Ready) reload(); }

    // from MaClient
    void on_state(MaClientState state);
    void on_event(const std::string& type, const std::string& object_id, const std::string& data_json);

private:
    void reload();
    void notify();
    void send(const std::string& command, const std::string& args_json);
    std::string queue_id() const;

    MaClient& client_;
    std::function<double()> now_;
    MaPlayerStore store_;
    std::string configured_player_;
    bool loaded_ = false;
    std::vector<std::pair<int, Listener>> listeners_;
    int next_listener_ = 1;
    bool top_picks_shown_ = true;
    std::string user_;
    bool providers_known_ = false;
    std::set<std::string> similar_;   // provider instances with similar tracks
    std::set<std::string> libraries_; // "artists", "albums", ... some music provider keeps
};

} // namespace app::music_assistant

#endif // APP_CPP_MUSIC_ASSISTANT_MACONTROLLER_H
