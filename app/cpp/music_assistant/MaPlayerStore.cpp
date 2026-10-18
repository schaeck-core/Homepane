#include "MaPlayerStore.h"

#include <cjson/cJSON.h>

#include <algorithm>

namespace app::music_assistant {

namespace {
std::string str(const cJSON* o, const char* key)
{
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : std::string();
}

bool boolean(const cJSON* o, const char* key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, key));
}

double number(const cJSON* o, const char* key, double fallback = 0)
{
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

std::vector<std::string> strings(const cJSON* o, const char* key)
{
    std::vector<std::string> out;
    const cJSON* v = nullptr;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(o, key))
    {
        if (cJSON_IsString(v)) out.emplace_back(v->valuestring);
    }
    return out;
}

MaPlayer parse_player(const cJSON* p)
{
    MaPlayer pl;
    pl.id = str(p, "player_id");
    pl.name = str(p, "name");
    pl.type = str(p, "type");
    pl.available = boolean(p, "available");
    pl.playback_state = str(p, "playback_state");
    const cJSON* vol = cJSON_GetObjectItemCaseSensitive(p, "volume_level");
    if (cJSON_IsNumber(vol)) pl.volume = static_cast<int>(vol->valuedouble + 0.5);
    pl.muted = boolean(p, "volume_muted");
    pl.group_members = strings(p, "group_members");
    pl.can_group_with = strings(p, "can_group_with");
    pl.synced_to = str(p, "synced_to");
    pl.active_source = str(p, "active_source");
    return pl;
}

MaQueue parse_queue(const cJSON* q)
{
    MaQueue out;
    out.id = str(q, "queue_id");
    out.display_name = str(q, "display_name");
    out.available = boolean(q, "available");
    out.active = boolean(q, "active");
    out.items = static_cast<int>(number(q, "items"));
    out.shuffle = boolean(q, "shuffle_enabled");
    const std::string repeat = str(q, "repeat_mode");
    if (!repeat.empty()) out.repeat = repeat;
    out.state = str(q, "state");
    out.current_index = static_cast<int>(number(q, "current_index", -1));
    out.elapsed = number(q, "elapsed_time");
    out.elapsed_updated_at = number(q, "elapsed_time_last_updated");
    const cJSON* cur = cJSON_GetObjectItemCaseSensitive(q, "current_item");
    if (cJSON_IsObject(cur)) out.current = MaPlayerStore::parse_queue_item(cur);
    const cJSON* next = cJSON_GetObjectItemCaseSensitive(q, "next_item");
    if (cJSON_IsObject(next)) out.next = MaPlayerStore::parse_queue_item(next);
    return out;
}
} // namespace

MaImage MaPlayerStore::parse_image(const void* cjson_image)
{
    const auto* img = static_cast<const cJSON*>(cjson_image);
    MaImage out;
    if (cJSON_IsObject(img)) out.proxy_id = str(img, "proxy_id");
    return out;
}

MaQueueItem MaPlayerStore::parse_queue_item(const void* cjson_item)
{
    const auto* it = static_cast<const cJSON*>(cjson_item);
    MaQueueItem out;
    out.queue_item_id = str(it, "queue_item_id");
    out.duration = number(it, "duration");
    out.image = parse_image(cJSON_GetObjectItemCaseSensitive(it, "image"));
    const cJSON* media = cJSON_GetObjectItemCaseSensitive(it, "media_item");
    if (!cJSON_IsObject(media)) media = it; // a media item itself (search results, library lists)
    out.title = str(media, "name");
    out.uri = str(media, "uri");
    out.media_type = str(media, "media_type");
    out.item_id = str(media, "item_id");
    out.provider = str(media, "provider");
    if ((out.item_id.empty() || out.provider.empty()) && !out.uri.empty()) {
        // "spotify--yb8Wet3Z://album/1GAkFO17jNudCOK0v1EgiF": provider://type/id
        const size_t scheme = out.uri.find("://");
        const size_t slash = scheme == std::string::npos ? scheme : out.uri.find('/', scheme + 3);
        if (slash != std::string::npos) {
            out.provider = out.uri.substr(0, scheme);
            out.item_id = out.uri.substr(slash + 1);
        }
    }
    out.owner = str(media, "owner");
    out.year = static_cast<int>(number(media, "year"));
    out.dynamic = boolean(media, "is_dynamic");
    const cJSON* m = nullptr;
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(media, "provider_mappings"))
    {
        const std::string inst = str(m, "provider_instance");
        if (!inst.empty()) out.mappings.push_back(inst);
    }
    out.favorite = boolean(media, "favorite");
    if (out.duration <= 0) out.duration = number(media, "duration");
    const cJSON* a = nullptr;
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(media, "artists"))
    {
        const std::string name = str(a, "name");
        if (name.empty()) continue;
        if (!out.artist.empty()) out.artist += ", ";
        out.artist += name;
    }
    out.album = str(cJSON_GetObjectItemCaseSensitive(media, "album"), "name");
    if (out.image.empty()) {
        // a media item's own pictures: metadata.images[0] (the thumb), else its album's
        const cJSON* images = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(media, "metadata"), "images");
        if (cJSON_IsArray(images) && cJSON_GetArraySize(images) > 0) out.image = parse_image(cJSON_GetArrayItem(images, 0));
        if (out.image.empty()) out.image = parse_image(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(media, "album"), "image"));
    }
    if (out.title.empty()) out.title = str(it, "name");
    return out;
}

void MaPlayerStore::load_players(const std::string& json)
{
    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }
    players_.clear();
    const cJSON* p = nullptr;
    cJSON_ArrayForEach(p, root) players_.push_back(parse_player(p));
    cJSON_Delete(root);
}

void MaPlayerStore::load_queues(const std::string& json)
{
    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }
    queues_.clear();
    const cJSON* q = nullptr;
    cJSON_ArrayForEach(q, root)
    {
        MaQueue parsed = parse_queue(q);
        if (!parsed.id.empty()) queues_[parsed.id] = std::move(parsed);
    }
    cJSON_Delete(root);
}

bool MaPlayerStore::apply_event(const std::string& type, const std::string& object_id, const std::string& data_json,
                                double now)
{
    if (type == "queue_time_updated") {
        auto it = queues_.find(object_id);
        cJSON* v = cJSON_Parse(data_json.c_str());
        const bool ok = it != queues_.end() && cJSON_IsNumber(v);
        if (ok) {
            it->second.elapsed = v->valuedouble;
            it->second.elapsed_updated_at = now;
        }
        cJSON_Delete(v);
        return ok;
    }
    if (type == "player_removed") {
        const auto before = players_.size();
        players_.erase(std::remove_if(players_.begin(), players_.end(), [&](const MaPlayer& p) { return p.id == object_id; }),
                       players_.end());
        return players_.size() != before;
    }
    const bool player = type == "player_added" || type == "player_updated";
    const bool queue = type == "queue_added" || type == "queue_updated";
    if (!player && !queue) return false;
    cJSON* data = cJSON_Parse(data_json.c_str());
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(data);
        return false;
    }
    if (player) {
        MaPlayer p = parse_player(data);
        auto it = std::find_if(players_.begin(), players_.end(), [&](const MaPlayer& x) { return x.id == p.id; });
        if (it != players_.end()) *it = std::move(p);
        else if (!p.id.empty()) players_.push_back(std::move(p));
    } else {
        MaQueue q = parse_queue(data);
        if (!q.id.empty()) queues_[q.id] = std::move(q);
    }
    cJSON_Delete(data);
    return true;
}

const MaPlayer* MaPlayerStore::player(const std::string& id) const
{
    auto it = std::find_if(players_.begin(), players_.end(), [&](const MaPlayer& p) { return p.id == id; });
    return it != players_.end() ? &*it : nullptr;
}

const MaQueue* MaPlayerStore::queue(const std::string& id) const
{
    auto it = queues_.find(id);
    return it != queues_.end() ? &it->second : nullptr;
}

const MaQueue* MaPlayerStore::queue_of(const std::string& player_id) const
{
    const MaPlayer* p = player(player_id);
    if (p && !p->synced_to.empty()) {
        if (const MaQueue* q = queue(p->synced_to)) return q;
    }
    if (p && !p->active_source.empty() && p->active_source != p->id) {
        if (const MaQueue* q = queue(p->active_source)) return q;
    }
    return queue(player_id);
}

double MaPlayerStore::elapsed_at(const MaQueue& q, double now)
{
    double e = q.elapsed;
    if (q.state == "playing" && q.elapsed_updated_at > 0 && now > q.elapsed_updated_at) e += now - q.elapsed_updated_at;
    if (q.current && q.current->duration > 0) e = std::min(e, q.current->duration);
    return std::max(0.0, e);
}

std::string ma_http_base(const std::string& ws_url)
{
    std::string u = ws_url;
    if (u.rfind("wss://", 0) == 0) u = "https://" + u.substr(6);
    else if (u.rfind("ws://", 0) == 0) u = "http://" + u.substr(5);
    const size_t scheme = u.find("://");
    const size_t path = u.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    if (path != std::string::npos) u.erase(path);
    return u;
}

std::string ma_image_url(const std::string& http_base, const MaImage& image, int size)
{
    if (image.empty() || http_base.empty()) return {};
    const int sizes[] = {80, 160, 256, 512, 1024};
    int s = 1024;
    for (int c : sizes) {
        if (c >= size) {
            s = c;
            break;
        }
    }
    return http_base + "/imageproxy/" + image.proxy_id + "?size=" + std::to_string(s) + "&fmt=jpeg";
}

MaRowsPreference MaRowsPreference::parse(const std::string& preferences_json)
{
    MaRowsPreference p;
    cJSON* prefs = cJSON_Parse(preferences_json.c_str());
    const cJSON* rows = cJSON_GetObjectItemCaseSensitive(prefs, "discover.rows");
    auto list = [rows](const char* key, std::vector<std::string>& out) {
        const cJSON* v = nullptr;
        cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(rows, key))
        {
            if (cJSON_IsString(v)) out.emplace_back(v->valuestring);
        }
    };
    list("order", p.order);
    list("hidden", p.hidden);
    list("shown", p.shown);
    cJSON_Delete(prefs);
    if (p.order.empty()) p.order = {"top_picks", "library://folder/in_progress", "library://folder/recently_played"};
    return p;
}

bool MaRowsPreference::visible(const std::string& id, bool enabled_by_default) const
{
    auto has = [&id](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), id) != v.end(); };
    return !has(hidden) && (enabled_by_default || has(shown));
}

size_t MaRowsPreference::rank(const std::string& id) const
{
    const auto it = std::find(order.begin(), order.end(), id);
    return it == order.end() ? order.size() : static_cast<size_t>(it - order.begin());
}

} // namespace app::music_assistant
