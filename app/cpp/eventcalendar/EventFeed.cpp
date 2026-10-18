#include "EventFeed.h"

#include "../core/IsoTime.h"

#include <cjson/cJSON.h>

namespace app::eventcalendar {

namespace {
std::string text(const cJSON* obj, const char* key)
{
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

cJSON* parse_attr(const std::map<std::string, std::string>& attributes, const char* name)
{
    auto it = attributes.find(name);
    return it == attributes.end() ? nullptr : cJSON_Parse(it->second.c_str());
}
} // namespace

Feed parse_feed(const std::string& state, const std::map<std::string, std::string>& attributes)
{
    Feed f;
    if (state == "unavailable" || state == "unknown" || state.empty()) return f;
    auto version = attributes.find("api_version");
    if (version == attributes.end() || version->second != "1") return f; // unknown format: show nothing wrong
    f.available = true;

    if (cJSON* events = parse_attr(attributes, "events")) {
        const cJSON* e = nullptr;
        cJSON_ArrayForEach(e, events)
        {
            Event ev;
            ev.id = text(e, "id");
            ev.title = text(e, "title");
            ev.location = text(e, "location");
            ev.all_day = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e, "all_day"));
            ev.start = app::core::parse_iso_time(text(e, "start")).value_or(0);
            ev.end = app::core::parse_iso_time(text(e, "end")).value_or(ev.start);
            const cJSON* km = cJSON_GetObjectItemCaseSensitive(e, "distance_km");
            if (cJSON_IsNumber(km)) ev.distance_km = km->valuedouble;
            ev.distance_type = text(e, "distance_type");
            ev.image_key = text(e, "image_key");
            ev.image_kind = text(e, "image_kind");
            if (!ev.id.empty() && !ev.title.empty()) f.events.push_back(std::move(ev));
        }
        cJSON_Delete(events);
    }
    if (cJSON* waste = parse_attr(attributes, "waste")) {
        const cJSON* w = nullptr;
        cJSON_ArrayForEach(w, waste)
        {
            Pickup p{text(w, "id"), text(w, "title"), text(w, "day_label")};
            if (!p.title.empty()) f.waste.push_back(std::move(p));
        }
        cJSON_Delete(waste);
    }
    if (cJSON* display = parse_attr(attributes, "display")) {
        const cJSON* card = cJSON_GetObjectItemCaseSensitive(display, "card_seconds");
        const cJSON* idle = cJSON_GetObjectItemCaseSensitive(display, "idle_seconds");
        if (cJSON_IsNumber(card) && card->valueint >= 3) f.card_seconds = card->valueint;
        if (cJSON_IsNumber(idle) && idle->valueint >= 0) f.idle_seconds = idle->valueint;
        cJSON_Delete(display);
    }
    return f;
}

void EventFeed::attach(app::home_assistant::EntitySubscriptionCoordinator* coordinator,
                       app::home_assistant::HaClient* client, const std::string& sensor, Changed changed)
{
    (void)client;
    handle_.reset();
    image_subs_.clear();
    images_.clear();
    feed_ = Feed{};
    changed_ = std::move(changed);
    coordinator_ = coordinator;
    if (!coordinator || sensor.empty()) return;
    handle_ = coordinator->observe(sensor, [this](const app::home_assistant::EntityState& s) { on_state(s); });
    coordinator->deliver_current(sensor, handle_);
}

std::string EventFeed::image_entity(const std::string& image_key) const
{
    auto it = images_.find(image_key);
    return it == images_.end() ? std::string() : it->second;
}

void EventFeed::on_state(const app::home_assistant::EntityState& s)
{
    using app::home_assistant::Availability;
    // the whole lists are replaced on every update (titles can change while
    // the count stays); stale / gone: not current
    feed_ = s.availability == Availability::available ? parse_feed(s.state, s.attributes) : Feed{};
    watch_images();
    if (changed_) changed_();
}

void EventFeed::watch_images()
{
    if (!coordinator_) return;
    std::map<std::string, app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle> keep;
    for (const Event& e : feed_.events) {
        if (e.image_key.empty() || keep.count(e.image_key)) continue;
        auto it = image_subs_.find(e.image_key);
        if (it != image_subs_.end()) {
            keep[e.image_key] = std::move(it->second);
            continue;
        }
        // the add-on's naming; confirmed by the attribute before it is used
        const std::string entity = "image.ha_eventcalendar_" + e.image_key;
        const std::string key = e.image_key;
        auto handle = coordinator_->observe(entity, [this, key](const app::home_assistant::EntityState& st) {
            on_image_state(key, st);
        });
        coordinator_->deliver_current(entity, handle);
        keep[key] = std::move(handle);
    }
    image_subs_ = std::move(keep); // events gone: their pictures are not watched any more
    for (auto it = images_.begin(); it != images_.end();) {
        it = image_subs_.count(it->first) ? std::next(it) : images_.erase(it);
    }
}

void EventFeed::on_image_state(const std::string& key, const app::home_assistant::EntityState& st)
{
    auto attr = st.attributes.find("eventcalendar_image_key");
    const bool match = attr != st.attributes.end() && attr->second == "\"" + key + "\""; // JSON text
    const bool had = images_.count(key) != 0;
    if (match && st.availability == app::home_assistant::Availability::available) {
        images_[key] = st.entity_id;
    } else {
        images_.erase(key);
    }
    if (had != (images_.count(key) != 0) && changed_) changed_();
}

} // namespace app::eventcalendar
