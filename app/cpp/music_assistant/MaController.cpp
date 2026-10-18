#include "MaController.h"

#include <cjson/cJSON.h>

#include <algorithm>

namespace app::music_assistant {

namespace {
// {"key":"value",...} from pairs (all strings)
std::string args(std::initializer_list<std::pair<const char*, std::string>> kv)
{
    cJSON* o = cJSON_CreateObject();
    for (const auto& p : kv) cJSON_AddStringToObject(o, p.first, p.second.c_str());
    char* text = cJSON_PrintUnformatted(o);
    std::string out = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(o);
    return out;
}

std::string with(const std::string& json, const char* key, cJSON* value)
{
    cJSON* o = cJSON_Parse(json.c_str());
    if (!o) o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, key, value);
    char* text = cJSON_PrintUnformatted(o);
    std::string out = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(o);
    return out;
}
} // namespace

MaController::MaController(MaClient& client, std::function<double()> now) : client_(client), now_(std::move(now)) {}

void MaController::set_player(const std::string& player_id)
{
    configured_player_ = player_id;
    notify();
}

std::string MaController::player_id() const
{
    if (!configured_player_.empty() && store_.player(configured_player_)) return configured_player_;
    // none chosen (or gone): the one that plays, else paused, else one with
    // something in its queue, else the first available
    int best = -1;
    std::string id;
    for (const auto& p : store_.players()) {
        if (!p.available || !p.synced_to.empty() || p.type == "group") continue;
        const MaQueue* q = store_.queue(p.id);
        const int score = q && q->state == "playing" ? 3 : q && q->state == "paused" ? 2 : q && q->current ? 1 : 0;
        if (score > best) {
            best = score;
            id = p.id;
        }
    }
    if (!id.empty()) return id;
    return store_.players().empty() ? std::string() : store_.players().front().id;
}

const MaPlayer* MaController::player() const { return store_.player(player_id()); }

const MaQueue* MaController::queue() const { return store_.queue_of(player_id()); }

std::string MaController::queue_id() const
{
    const MaQueue* q = queue();
    return q ? q->id : player_id();
}

double MaController::elapsed() const
{
    const MaQueue* q = queue();
    return q ? MaPlayerStore::elapsed_at(*q, now_()) : 0;
}

int MaController::add_listener(Listener l)
{
    listeners_.emplace_back(next_listener_, std::move(l));
    return next_listener_++;
}

void MaController::remove_listener(int id)
{
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& p) { return p.first == id; }),
                     listeners_.end());
}

void MaController::notify()
{
    const auto copy = listeners_; // a listener may remove itself
    for (const auto& l : copy) l.second();
}

void MaController::on_state(MaClientState state)
{
    if (state == MaClientState::Ready) {
        reload();
    } else if (loaded_) {
        loaded_ = false;
        notify();
    }
}

void MaController::reload()
{
    // what the providers can (similar music, library kinds)
    client_.send_command("providers", "{}", [this](const MaResultMessage& r) {
        if (!r.success) return;
        similar_.clear();
        libraries_.clear();
        cJSON* root = cJSON_Parse(r.result_json.c_str());
        const cJSON* p = nullptr;
        cJSON_ArrayForEach(p, root)
        {
            const cJSON* id = cJSON_GetObjectItemCaseSensitive(p, "instance_id");
            const cJSON* type = cJSON_GetObjectItemCaseSensitive(p, "type");
            if (!cJSON_IsString(id) || cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(p, "available"))) continue;
            const bool music = cJSON_IsString(type) && std::string(type->valuestring) == "music";
            const cJSON* f = nullptr;
            cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(p, "supported_features"))
            {
                if (!cJSON_IsString(f)) continue;
                const std::string feature = f->valuestring;
                if (feature == "similar_tracks") similar_.insert(id->valuestring);
                // "library_albums" (not "library_albums_edit")
                if (music && feature.rfind("library_", 0) == 0 && feature.find('_', 8) == std::string::npos) {
                    libraries_.insert(feature.substr(8));
                }
            }
        }
        cJSON_Delete(root);
        providers_known_ = true;
    });
    // the MA user the panel is logged in as (shown in the settings)
    client_.send_command("auth/me", "{}", [this](const MaResultMessage& me) {
        if (!me.success) return;
        cJSON* root = cJSON_Parse(me.result_json.c_str());
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "username");
        user_ = cJSON_IsString(name) ? name->valuestring : "";
        cJSON_Delete(root);
        notify();
    });
    client_.send_command("players/all", "{}", [this](const MaResultMessage& r) {
        if (!r.success) return;
        store_.load_players(r.result_json);
        client_.send_command("player_queues/all", "{}", [this](const MaResultMessage& q) {
            if (q.success) store_.load_queues(q.result_json);
            loaded_ = true;
            notify();
        });
    });
}

void MaController::on_event(const std::string& type, const std::string& object_id, const std::string& data_json)
{
    if (store_.apply_event(type, object_id, data_json, now_())) notify();
}

void MaController::send(const std::string& command, const std::string& args_json)
{
    // fire and forget: the server's events bring the new state
    client_.send_command(command, args_json, nullptr);
}

void MaController::play_pause() { send("player_queues/play_pause", args({{"queue_id", queue_id()}})); }
void MaController::next() { send("player_queues/next", args({{"queue_id", queue_id()}})); }
void MaController::previous() { send("player_queues/previous", args({{"queue_id", queue_id()}})); }

void MaController::set_shuffle(bool on)
{
    send("player_queues/shuffle", with(args({{"queue_id", queue_id()}}), "shuffle_enabled", cJSON_CreateBool(on)));
}

void MaController::cycle_repeat()
{
    const MaQueue* q = queue();
    const std::string now = q ? q->repeat : "off";
    const std::string next = now == "off" ? "all" : now == "all" ? "one" : "off";
    send("player_queues/repeat", args({{"queue_id", queue_id()}, {"repeat_mode", next}}));
}

void MaController::seek(double seconds)
{
    send("player_queues/seek", with(args({{"queue_id", queue_id()}}), "position", cJSON_CreateNumber(static_cast<int>(seconds))));
}

void MaController::set_volume(int volume)
{
    volume = std::max(0, std::min(100, volume));
    const MaPlayer* p = player();
    // a group leader with members: the whole group
    const bool group = p && p->group_members.size() > 1;
    send(group ? "players/cmd/group_volume" : "players/cmd/volume_set",
         with(args({{"player_id", player_id()}}), "volume_level", cJSON_CreateNumber(volume)));
}

void MaController::favorite_current()
{
    send("players/add_currently_playing_to_favorites", args({{"player_id", player_id()}}));
}

bool MaController::is_member(const std::string& other) const
{
    const MaPlayer* p = player();
    if (!p) return false;
    return std::find(p->group_members.begin(), p->group_members.end(), other) != p->group_members.end();
}

void MaController::set_member(const std::string& other, bool on)
{
    cJSON* list = cJSON_CreateArray();
    cJSON_AddItemToArray(list, cJSON_CreateString(other.c_str()));
    send("players/cmd/set_members",
         with(args({{"target_player", player_id()}}), on ? "player_ids_to_add" : "player_ids_to_remove", list));
}

void MaController::load_queue_items(int offset, int limit, ItemsDone done)
{
    const MaQueue* q = queue();
    const int total = q ? q->items : 0;
    std::string a = args({{"queue_id", queue_id()}});
    a = with(a, "limit", cJSON_CreateNumber(limit));
    a = with(a, "offset", cJSON_CreateNumber(offset));
    client_.send_command("player_queues/items", a, [done, total](const MaResultMessage& r) {
        std::vector<MaQueueItem> items;
        if (r.success) {
            cJSON* root = cJSON_Parse(r.result_json.c_str());
            const cJSON* it = nullptr;
            cJSON_ArrayForEach(it, root) items.push_back(MaPlayerStore::parse_queue_item(it));
            cJSON_Delete(root);
        }
        if (done) done(r.success, std::move(items), total);
    });
}

void MaController::play_queue_item(const std::string& queue_item_id)
{
    send("player_queues/play_index", args({{"queue_id", queue_id()}, {"index", queue_item_id}}));
}

void MaController::remove_queue_item(const std::string& queue_item_id)
{
    send("player_queues/delete_item", args({{"queue_id", queue_id()}, {"item_id_or_index", queue_item_id}}));
}

void MaController::play_next(const std::string& queue_item_id, int index_in_queue)
{
    const MaQueue* q = queue();
    const int target = (q ? q->current_index : -1) + 1;
    const int shift = target - index_in_queue; // negative: up
    if (shift == 0) return;
    send("player_queues/move_item",
         with(args({{"queue_id", queue_id()}, {"queue_item_id", queue_item_id}}), "pos_shift", cJSON_CreateNumber(shift)));
}

void MaController::start_radio(const std::string& uri)
{
    std::string a = args({{"queue_id", queue_id()}, {"media", uri}, {"option", "replace"}});
    send("player_queues/play_media", with(a, "radio_mode", cJSON_CreateBool(true)));
}

std::vector<MaQueueItem> MaController::parse_items(const std::string& json_array)
{
    std::vector<MaQueueItem> items;
    cJSON* root = cJSON_Parse(json_array.c_str());
    const cJSON* it = nullptr;
    cJSON_ArrayForEach(it, root)
    {
        MaQueueItem item = MaPlayerStore::parse_queue_item(it);
        if (!item.uri.empty()) items.push_back(std::move(item));
    }
    cJSON_Delete(root);
    return items;
}

void MaController::search(const std::string& query, const std::string& media_type, int limit, ItemsDone done)
{
    cJSON* a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "search_query", query.c_str());
    cJSON_AddNumberToObject(a, "limit", limit);
    cJSON* types = cJSON_AddArrayToObject(a, "media_types");
    const char* all[] = {"track", "artist", "album", "playlist", "radio"};
    for (const char* t : all) {
        if (media_type.empty() || media_type == t) cJSON_AddItemToArray(types, cJSON_CreateString(t));
    }
    char* text = cJSON_PrintUnformatted(a);
    const std::string json = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(a);
    client_.send_command(
        "music/search", json,
        [done](const MaResultMessage& r) {
            std::vector<MaQueueItem> items;
            if (r.success) {
                cJSON* root = cJSON_Parse(r.result_json.c_str());
                // in this order: titles, artists, albums, playlists, radio
                for (const char* key : {"tracks", "artists", "albums", "playlists", "radio"}) {
                    const cJSON* it = nullptr;
                    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, key))
                    {
                        MaQueueItem item = MaPlayerStore::parse_queue_item(it);
                        if (!item.uri.empty()) items.push_back(std::move(item));
                    }
                }
                cJSON_Delete(root);
            }
            if (done) done(r.success, std::move(items), 0);
        },
        20000);
}

void MaController::library(const std::string& kind, int limit, ItemsDone done, int offset)
{
    client_.send_command("music/" + kind + "/library_items",
                         "{\"limit\":" + std::to_string(limit) + ",\"offset\":" + std::to_string(offset) + "}",
                         [done](const MaResultMessage& r) {
                             if (done) done(r.success, r.success ? parse_items(r.result_json) : std::vector<MaQueueItem>{}, 0);
                         });
}

bool MaController::has_library(const std::string& kind) const
{
    return !providers_known_ || libraries_.count(kind) > 0;
}

void MaController::recently_played(int limit, ItemsDone done)
{
    client_.send_command("music/recently_played_items", "{\"limit\":" + std::to_string(limit) + "}",
                         [done](const MaResultMessage& r) {
                             if (done) done(r.success, r.success ? parse_items(r.result_json) : std::vector<MaQueueItem>{}, 0);
                         });
}

void MaController::recommendations(FoldersDone done)
{
    // the user's row settings first (auth/me: preferences), then the rows
    client_.send_command("auth/me", "{}", [this, done](const MaResultMessage& me) {
        std::string prefs;
        if (me.success) {
            cJSON* root = cJSON_Parse(me.result_json.c_str());
            char* text = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(root, "preferences"));
            if (text) prefs = text;
            cJSON_free(text);
            cJSON_Delete(root);
        }
        client_.send_command("music/recommendations", "{}", [this, done, prefs](const MaResultMessage& r) {
            std::vector<Folder> folders;
            if (r.success) {
                cJSON* root = cJSON_Parse(r.result_json.c_str());
                const cJSON* f = nullptr;
                cJSON_ArrayForEach(f, root)
                {
                    auto str = [f](const char* k) {
                        const cJSON* v = cJSON_GetObjectItemCaseSensitive(f, k);
                        return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
                    };
                    Folder folder{str("provider"), str("item_id"), str("name"), str("subtitle"), str("uri"),
                                  !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(f, "enabled_by_default"))};
                    if (!folder.item_id.empty()) folders.push_back(std::move(folder));
                }
                cJSON_Delete(root);
            }
            const MaRowsPreference rows = MaRowsPreference::parse(prefs);
            for (auto& f : folders) f.shown = rows.visible(f.uri, f.shown);
            top_picks_shown_ = rows.visible("top_picks", true);
            std::stable_sort(folders.begin(), folders.end(), [&rows](const Folder& a, const Folder& b) {
                return rows.rank(a.uri) < rows.rank(b.uri);
            });
            if (done) done(std::move(folders));
        });
    });
}

void MaController::recommendation_items(const Folder& folder, ItemsDone done)
{
    client_.send_command("music/recommendations/items",
                         args({{"provider", folder.provider}, {"item_id", folder.item_id}}),
                         [done](const MaResultMessage& r) {
                             if (done) done(r.success, r.success ? parse_items(r.result_json) : std::vector<MaQueueItem>{}, 0);
                         },
                         30000);
}

void MaController::play_media(const MaQueueItem& item, Enqueue how)
{
    const char* option = how == Enqueue::Replace ? "replace" : how == Enqueue::Next ? "next" : how == Enqueue::Add ? "add" : "play";
    send("player_queues/play_media", args({{"queue_id", queue_id()}, {"media", item.uri}, {"option", option}}));
}

void MaController::play_shuffled(const MaQueueItem& item)
{
    // in this order: MA shuffles what comes into a shuffled queue
    const std::string queue = queue_id(), uri = item.uri;
    client_.send_command("player_queues/shuffle", with(args({{"queue_id", queue}}), "shuffle_enabled", cJSON_CreateBool(true)),
                         [this, queue, uri](const MaResultMessage&) {
                             send("player_queues/play_media", args({{"queue_id", queue}, {"media", uri}, {"option", "replace"}}));
                         });
}

void MaController::item(const MaQueueItem& item, ItemDone done)
{
    client_.send_command("music/item_by_uri", args({{"uri", item.uri}}), [done, item](const MaResultMessage& r) {
        MaQueueItem out = item;
        if (r.success) {
            cJSON* root = cJSON_Parse(r.result_json.c_str());
            if (cJSON_IsObject(root)) out = MaPlayerStore::parse_queue_item(root);
            cJSON_Delete(root);
            if (out.uri.empty()) out = item;
        }
        if (done) done(r.success, std::move(out));
    });
}

void MaController::tracks(const MaQueueItem& item, int offset, int limit, ItemsDone done)
{
    const char* command = item.media_type == "playlist" ? "music/playlists/playlist_tracks"
                          : item.media_type == "album"  ? "music/albums/album_tracks"
                                                        : "music/artists/top_tracks";
    std::string a = args({{"item_id", item.item_id}, {"provider_instance_id_or_domain", item.provider}});
    a = with(a, "limit", cJSON_CreateNumber(limit));
    a = with(a, "offset", cJSON_CreateNumber(offset));
    client_.send_command(command, a,
                         [done](const MaResultMessage& r) {
                             if (done) done(r.success, r.success ? parse_items(r.result_json) : std::vector<MaQueueItem>{}, 0);
                         },
                         30000); // a streaming service's playlist: MA loads it page by page first
}

void MaController::artist_albums(const MaQueueItem& artist, int limit, ItemsDone done)
{
    std::string a = args({{"item_id", artist.item_id}, {"provider_instance_id_or_domain", artist.provider}});
    client_.send_command("music/artists/artist_albums", with(a, "limit", cJSON_CreateNumber(limit)),
                         [done](const MaResultMessage& r) {
                             if (done) done(r.success, r.success ? parse_items(r.result_json) : std::vector<MaQueueItem>{}, 0);
                         },
                         30000);
}

void MaController::set_favorite(const MaQueueItem& item, bool on)
{
    if (on) send("music/favorites/add_item", args({{"item", item.uri}}));
    else if (item.provider == "library")
        send("music/favorites/remove_item", args({{"media_type", item.media_type}, {"library_item_id", item.item_id}}));
    else send("music/favorites/set_item", with(args({{"item", item.uri}}), "favorite", cJSON_CreateNull()));
}

bool MaController::radio_supported(const MaQueueItem& item) const
{
    const std::string& t = item.media_type;
    if (t != "track" && t != "artist" && t != "album" && t != "playlist") return false;
    if (t == "playlist" && item.dynamic) return false;
    if (item.provider == "library") return !similar_.empty();
    if (similar_.count(item.provider)) return true;
    for (const auto& m : item.mappings) {
        if (similar_.count(m)) return true;
    }
    return false;
}

} // namespace app::music_assistant
