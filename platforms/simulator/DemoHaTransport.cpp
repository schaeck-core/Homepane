#include "DemoHaTransport.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace platform_simulator {

namespace {
std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string str(const cJSON* node) { return cJSON_IsString(node) ? node->valuestring : ""; }

std::string print(cJSON* node)
{
    char* text = cJSON_PrintUnformatted(node);
    std::string out = text ? text : "";
    cJSON_free(text);
    return out;
}

std::string iso_now(double offset_s = 0)
{
    std::time_t t = std::time(nullptr) + static_cast<std::time_t>(offset_s);
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", std::gmtime(&t));
    return buf;
}
} // namespace

DemoHaTransport::DemoHaTransport(std::string fixture_dir) : fixture_dir_(std::move(fixture_dir))
{
    states_ = cJSON_Parse(read_file(fixture_dir_ + "/states.json").c_str());
    if (!states_) {
        std::fprintf(stderr, "DemoHaTransport: cannot read %s/states.json\n", fixture_dir_.c_str());
        states_ = cJSON_CreateObject();
    }
    // "demo:camera.jpg" pictures: files next to the fixtures
    cJSON* ent = nullptr;
    cJSON_ArrayForEach(ent, states_)
    {
        cJSON* attrs = cJSON_GetObjectItemCaseSensitive(ent, "a");
        cJSON* pic = cJSON_GetObjectItemCaseSensitive(attrs, "entity_picture");
        if (cJSON_IsString(pic) && std::string(pic->valuestring).rfind("demo:", 0) == 0) {
            const std::string url = "file://" + fixture_dir_ + "/" + (pic->valuestring + 5);
            cJSON_ReplaceItemInObjectCaseSensitive(attrs, "entity_picture", cJSON_CreateString(url.c_str()));
        }
    }
}

DemoHaTransport::~DemoHaTransport() { cJSON_Delete(states_); }

void DemoHaTransport::connect(const std::string&)
{
    open_pending_ = true;
    inbox_.push_back(R"({"type":"auth_required","ha_version":"2026.10.0"})");
}

void DemoHaTransport::close()
{
    closed_pending_ = true;
    subscriptions_.clear();
}

void DemoHaTransport::dispatch_pending()
{
    if (!sink_) return;
    if (open_pending_) {
        open_pending_ = false;
        sink_->on_transport_open();
        sink_->on_transport_writable();
    }
    // Deliver what is queued now; replies to messages sent while delivering
    // wait for the next poll (like a real network round trip).
    std::deque<std::string> batch;
    batch.swap(inbox_);
    for (const auto& m : batch) {
        sink_->on_transport_fragment(reinterpret_cast<const uint8_t*>(m.data()), m.size(), true);
    }
    if (queue_writable_) {
        queue_writable_ = false;
        sink_->on_transport_writable();
    }
    if (closed_pending_) {
        closed_pending_ = false;
        sink_->on_transport_closed("closed");
    }
}

bool DemoHaTransport::send_text(const uint8_t* data, size_t len)
{
    std::string text(reinterpret_cast<const char*>(data), len);
    cJSON* msg = cJSON_Parse(text.c_str());
    if (msg) handle(msg);
    cJSON_Delete(msg);
    return true;
}

void DemoHaTransport::reply(int id, cJSON* result_owned)
{
    cJSON* r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddStringToObject(r, "type", "result");
    cJSON_AddTrueToObject(r, "success");
    cJSON_AddItemToObject(r, "result", result_owned ? result_owned : cJSON_CreateNull());
    inbox_.push_back(print(r));
    cJSON_Delete(r);
}

void DemoHaTransport::reply_error(int id, const char* code, const char* message)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), R"({"id":%d,"type":"result","success":false,"error":{"code":"%s","message":"%s"}})",
                  id, code, message);
    inbox_.push_back(buf);
}

void DemoHaTransport::event(int id, cJSON* event_owned)
{
    cJSON* r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddStringToObject(r, "type", "event");
    cJSON_AddItemToObject(r, "event", event_owned);
    inbox_.push_back(print(r));
    cJSON_Delete(r);
}

cJSON* DemoHaTransport::entity(const std::string& entity_id)
{
    return cJSON_GetObjectItemCaseSensitive(states_, entity_id.c_str());
}

void DemoHaTransport::set_attr(cJSON* ent, const char* key, cJSON* value_owned)
{
    cJSON* attrs = cJSON_GetObjectItemCaseSensitive(ent, "a");
    if (!attrs) attrs = cJSON_AddObjectToObject(ent, "a");
    if (cJSON_GetObjectItemCaseSensitive(attrs, key)) cJSON_ReplaceItemInObjectCaseSensitive(attrs, key, value_owned);
    else cJSON_AddItemToObject(attrs, key, value_owned);
}

void DemoHaTransport::handle(cJSON* msg)
{
    const std::string type = str(cJSON_GetObjectItemCaseSensitive(msg, "type"));
    const cJSON* id_node = cJSON_GetObjectItemCaseSensitive(msg, "id");
    const int id = cJSON_IsNumber(id_node) ? id_node->valueint : 0;

    if (type == "auth") {
        inbox_.push_back(R"({"type":"auth_ok","ha_version":"2026.10.0"})");
    } else if (type == "get_panels") {
        // the setup's dashboard choice: the default dashboard plus two others
        reply(id, cJSON_Parse(R"({
            "lovelace": {"component_name": "lovelace", "title": null, "config": {"mode": "storage"}, "url_path": "lovelace"},
            "dashboard-wohnen": {"component_name": "lovelace", "title": "Home", "config": {"mode": "storage"}, "url_path": "dashboard-wohnen"},
            "dashboard-tablet": {"component_name": "lovelace", "title": "Hallway tablet", "config": {"mode": "storage"}, "url_path": "dashboard-tablet"},
            "config": {"component_name": "config", "title": "Settings", "config": {}, "url_path": "config"}
        })"));
    } else if (type == "lovelace/config") {
        reply(id, cJSON_Parse(read_file(fixture_dir_ + "/lovelace.json").c_str()));
    } else if (type == "subscribe_entities") {
        cJSON* snapshot = cJSON_CreateObject();
        std::vector<std::string> ids;
        const cJSON* e = nullptr;
        cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(msg, "entity_ids"))
        {
            const std::string eid = str(e);
            ids.push_back(eid);
            if (cJSON* ent = entity(eid)) {
                if (!cJSON_GetObjectItemCaseSensitive(ent, "lc")) // changed a while ago, varying per entity
                    cJSON_AddNumberToObject(ent, "lc", static_cast<double>(std::time(nullptr)) - 120.0 - (eid.size() % 7) * 700.0);
                cJSON_AddItemToObject(snapshot, eid.c_str(), cJSON_Duplicate(ent, true));
            }
        }
        subscriptions_[id] = ids;
        reply(id, nullptr);
        cJSON* ev = cJSON_CreateObject();
        cJSON_AddItemToObject(ev, "a", snapshot);
        event(id, ev);
    } else if (type == "unsubscribe_events") {
        const cJSON* sub = cJSON_GetObjectItemCaseSensitive(msg, "subscription");
        if (cJSON_IsNumber(sub)) subscriptions_.erase(sub->valueint);
        reply(id, nullptr);
    } else if (type == "weather/subscribe_forecast") {
        reply(id, nullptr);
        static const char* const conditions[] = {"sunny", "rainy", "rainy", "partlycloudy", "cloudy", "sunny"};
        static const double hi[] = {23, 16, 12, 15, 14, 18}, lo[] = {9, 9, 7, 12, 8, 10};
        cJSON* ev = cJSON_CreateObject();
        cJSON* days = cJSON_AddArrayToObject(ev, "forecast");
        if (str(cJSON_GetObjectItemCaseSensitive(msg, "forecast_type")) == "hourly") {
            static const char* const hourly[] = {"sunny", "sunny", "partlycloudy", "partlycloudy", "cloudy", "rainy"};
            for (int h = 0; h < 12; ++h) {
                cJSON* hour = cJSON_CreateObject();
                cJSON_AddStringToObject(hour, "datetime", iso_now(3600.0 * (h + 1)).c_str());
                cJSON_AddStringToObject(hour, "condition", hourly[h % 6]);
                cJSON_AddNumberToObject(hour, "temperature", 17 - h * 0.6);
                cJSON_AddItemToArray(days, hour);
            }
            event(id, ev);
            return;
        }
        for (int d = 0; d < 6; ++d) {
            cJSON* day = cJSON_CreateObject();
            cJSON_AddStringToObject(day, "datetime", iso_now(86400.0 * d).c_str()); // [0] = today
            cJSON_AddStringToObject(day, "condition", conditions[d]);
            cJSON_AddNumberToObject(day, "temperature", hi[d]);
            cJSON_AddNumberToObject(day, "templow", lo[d]);
            cJSON_AddNumberToObject(day, "precipitation", d == 1 || d == 2 ? 4.2 : 0.0);
            cJSON_AddNumberToObject(day, "precipitation_probability", d == 1 || d == 2 ? 80 : 10);
            cJSON_AddItemToArray(days, day);
        }
        event(id, ev);
    } else if (type == "recorder/statistics_during_period") {
        // 24 hourly buckets with a daily rhythm around the current value.
        cJSON* result = cJSON_CreateObject();
        const double now = static_cast<double>(std::time(nullptr));
        const cJSON* sid = nullptr;
        cJSON_ArrayForEach(sid, cJSON_GetObjectItemCaseSensitive(msg, "statistic_ids"))
        {
            cJSON* ent = entity(str(sid));
            const double base = ent ? std::atof(str(cJSON_GetObjectItemCaseSensitive(ent, "s")).c_str()) : 50.0;
            cJSON* buckets = cJSON_AddArrayToObject(result, str(sid).c_str());
            for (int h = 0; h < 24; ++h) {
                const double shape = 0.55 + 0.4 * std::sin(h * 0.45) + 0.25 * std::sin(h * 1.7);
                const double mean = base * shape;
                cJSON* b = cJSON_CreateObject();
                cJSON_AddNumberToObject(b, "start", (now - (24 - h) * 3600.0) * 1000.0);
                cJSON_AddNumberToObject(b, "mean", mean);
                cJSON_AddNumberToObject(b, "min", mean * 0.7);
                cJSON_AddNumberToObject(b, "max", mean * 1.3);
                cJSON_AddItemToArray(buckets, b);
            }
        }
        reply(id, result);
    } else if (type == "get_states") {
        cJSON* result = cJSON_CreateArray();
        cJSON* ent = nullptr;
        cJSON_ArrayForEach(ent, states_)
        {
            cJSON* st = cJSON_CreateObject();
            cJSON_AddStringToObject(st, "entity_id", ent->string);
            cJSON_AddStringToObject(st, "state", str(cJSON_GetObjectItemCaseSensitive(ent, "s")).c_str());
            cJSON* attrs = cJSON_GetObjectItemCaseSensitive(ent, "a");
            cJSON_AddItemToObject(st, "attributes", attrs ? cJSON_Duplicate(attrs, true) : cJSON_CreateObject());
            cJSON_AddItemToArray(result, st);
        }
        reply(id, result);
    } else if (type == "todo/item/list") {
        cJSON* result = cJSON_CreateObject();
        cJSON* items = cJSON_AddArrayToObject(result, "items");
        for (const TodoItem& t : todo_) {
            cJSON* it = cJSON_CreateObject();
            cJSON_AddStringToObject(it, "uid", t.uid.c_str());
            cJSON_AddStringToObject(it, "summary", t.summary.c_str());
            cJSON_AddStringToObject(it, "status", t.done ? "completed" : "needs_action");
            cJSON_AddItemToArray(items, it);
        }
        reply(id, result);
    } else if (type == "call_service") {
        const std::string domain = str(cJSON_GetObjectItemCaseSensitive(msg, "domain"));
        const std::string service = str(cJSON_GetObjectItemCaseSensitive(msg, "service"));
        cJSON* data = cJSON_GetObjectItemCaseSensitive(msg, "service_data");
        if (domain == "calendar" && service == "get_events") {
            const std::string eid = str(cJSON_GetObjectItemCaseSensitive(data, "entity_id"));
            cJSON* result = cJSON_CreateObject();
            cJSON* response = cJSON_AddObjectToObject(result, "response");
            cJSON* cal = cJSON_AddObjectToObject(response, eid.c_str());
            cJSON_AddItemToObject(cal, "events", calendar_events());
            reply(id, result);
            return;
        }
        if (domain == "todo" && service == "update_item") {
            const std::string uid = str(cJSON_GetObjectItemCaseSensitive(data, "item"));
            int open = 0;
            for (TodoItem& t : todo_) {
                if (t.uid == uid) t.done = str(cJSON_GetObjectItemCaseSensitive(data, "status")) == "completed";
                open += t.done ? 0 : 1;
            }
            if (cJSON* ent = entity("todo.einkaufsliste")) {
                cJSON_ReplaceItemInObjectCaseSensitive(ent, "s", cJSON_CreateString(std::to_string(open).c_str()));
                push_change("todo.einkaufsliste");
            }
            reply(id, cJSON_Parse(R"({"context":{"id":"demo"}})"));
            return;
        }
        call_service(domain, service, data);
        reply(id, cJSON_Parse(R"({"context":{"id":"demo"}})"));
    } else if (type == "ping") {
        inbox_.push_back("{\"id\":" + std::to_string(id) + ",\"type\":\"pong\"}");
    } else if (id) {
        reply_error(id, "not_supported", ("demo: " + type).c_str());
    }
}

// A few events from now on, as local times without an offset (like HA's
// all-day events and many local calendars).
cJSON* DemoHaTransport::calendar_events() const
{
    struct Ev {
        int day, hour, minute;
        const char* summary;
    };
    static const Ev kEvents[] = {{0, 18, 30, "Parents' evening"}, {1, 6, 0, "Organic waste collection"}, {2, -1, 0, "Farmers market"},
                                 {4, 9, 30, "Dentist"}};
    const time_t now = time(nullptr);
    cJSON* events = cJSON_CreateArray();
    for (const Ev& e : kEvents) {
        time_t t = now + e.day * 86400;
        struct tm l {};
        localtime_r(&t, &l);
        char buf[32];
        if (e.hour < 0) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
        } else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:00", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday,
                          e.hour, e.minute);
        }
        cJSON* ev = cJSON_CreateObject();
        cJSON_AddStringToObject(ev, "start", buf);
        cJSON_AddStringToObject(ev, "summary", e.summary);
        cJSON_AddItemToArray(events, ev);
    }
    return events;
}

void DemoHaTransport::call_service(const std::string& domain, const std::string& service, cJSON* data)
{
    const std::string eid = str(cJSON_GetObjectItemCaseSensitive(data, "entity_id"));
    cJSON* ent = entity(eid);
    if (!ent) return;
    auto set_state = [&](const std::string& s) {
        cJSON_ReplaceItemInObjectCaseSensitive(ent, "s", cJSON_CreateString(s.c_str()));
        cJSON_DeleteItemFromObjectCaseSensitive(ent, "lc");
        cJSON_AddNumberToObject(ent, "lc", static_cast<double>(std::time(nullptr)));
    };
    auto number = [&](const char* key, double fallback) {
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(data, key);
        return cJSON_IsNumber(v) ? v->valuedouble : fallback;
    };
    auto attr_number = [&](const char* key, double fallback) {
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(ent, "a"), key);
        return cJSON_IsNumber(v) ? v->valuedouble : fallback;
    };
    const std::string state = str(cJSON_GetObjectItemCaseSensitive(ent, "s"));
    const bool on = state != "off";

    if (service == "toggle") {
        set_state(on ? "off" : "on");
    } else if (service == "turn_off") {
        set_state("off");
    } else if (service == "turn_on") {
        if (domain == "scene") set_state(iso_now());
        else set_state("on");
        if (domain == "light") {
            if (cJSON_GetObjectItemCaseSensitive(data, "brightness"))
                set_attr(ent, "brightness", cJSON_CreateNumber(number("brightness", 255)));
            if (cJSON_GetObjectItemCaseSensitive(data, "color_temp_kelvin")) {
                set_attr(ent, "color_temp_kelvin", cJSON_CreateNumber(number("color_temp_kelvin", 2700)));
                set_attr(ent, "color_mode", cJSON_CreateString("color_temp"));
            }
            if (const cJSON* rgb = cJSON_GetObjectItemCaseSensitive(data, "rgb_color")) {
                set_attr(ent, "rgb_color", cJSON_Duplicate(rgb, true));
                set_attr(ent, "color_mode", cJSON_CreateString("hs"));
            }
            if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(ent, "a"), "brightness")))
                set_attr(ent, "brightness", cJSON_CreateNumber(255));
        }
    } else if (domain == "fan" && (service == "increase_speed" || service == "decrease_speed" || service == "set_percentage")) {
        const double step = attr_number("percentage_step", 33.33);
        double p = attr_number("percentage", 0);
        p = service == "set_percentage" ? number("percentage", p) : p + (service == "increase_speed" ? step : -step);
        p = p < 0 ? 0 : p > 100 ? 100 : p;
        set_attr(ent, "percentage", cJSON_CreateNumber(std::round(p)));
        set_state(p > 0 ? "on" : "off");
    } else if (domain == "cover") {
        double pos = attr_number("current_position", 0);
        if (service == "open_cover") pos = 100;
        else if (service == "close_cover") pos = 0;
        else if (service == "set_cover_position") pos = number("position", pos);
        if (service == "set_cover_tilt_position")
            set_attr(ent, "current_tilt_position", cJSON_CreateNumber(number("tilt_position", 50)));
        set_attr(ent, "current_position", cJSON_CreateNumber(pos));
        set_state(pos > 0 ? "open" : "closed");
    } else if (domain == "climate") {
        if (service == "set_hvac_mode") set_state(str(cJSON_GetObjectItemCaseSensitive(data, "hvac_mode")));
        for (const char* key : {"preset_mode", "fan_mode"}) {
            if (const cJSON* v = cJSON_GetObjectItemCaseSensitive(data, key)) set_attr(ent, key, cJSON_Duplicate(v, true));
        }
        for (const char* key : {"temperature", "target_temp_low", "target_temp_high"}) {
            if (cJSON_GetObjectItemCaseSensitive(data, key)) set_attr(ent, key, cJSON_CreateNumber(number(key, 20)));
        }
    } else if (domain == "media_player") {
        if (service == "media_play_pause") set_state(state == "playing" ? "paused" : "playing");
        else if (service == "media_play") set_state("playing");
        else if (service == "media_pause") set_state("paused");
        else if (service == "volume_set") set_attr(ent, "volume_level", cJSON_CreateNumber(number("volume_level", 0.3)));
        else if (service == "shuffle_set") set_attr(ent, "shuffle", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(data, "shuffle"), true));
        else if (service == "repeat_set") set_attr(ent, "repeat", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(data, "repeat"), true));
    } else if (domain == "lock") {
        set_state(service == "unlock" ? "unlocked" : "locked");
    } else if (service == "select_option") {
        set_state(str(cJSON_GetObjectItemCaseSensitive(data, "option")));
    } else if (service == "set_value") {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", number("value", 0));
        set_state(buf);
    } else if (service == "increment" || service == "decrement") {
        const double step = attr_number("step", 1);
        double v = std::atof(state.c_str()) + (service == "increment" ? step : -step);
        v = std::fmax(attr_number("min", v), std::fmin(attr_number("max", v), v));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        set_state(buf);
    } else if (domain == "alarm_control_panel") {
        if (service == "alarm_disarm") set_state("disarmed");
        else if (service.rfind("alarm_arm_", 0) == 0) set_state("armed_" + service.substr(10));
    } else if (domain == "button" && service == "press") {
        set_state(iso_now());
    } else if (domain == "update" && service == "install") {
        set_attr(ent, "installed_version",
                 cJSON_CreateString(str(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(ent, "a"),
                                                                         "latest_version"))
                                        .c_str()));
        set_state("off");
    } else {
        std::fprintf(stderr, "DemoHaTransport: %s.%s not simulated\n", domain.c_str(), service.c_str());
        return;
    }
    push_change(eid);
}

void DemoHaTransport::push_change(const std::string& entity_id)
{
    cJSON* ent = entity(entity_id);
    for (const auto& [sub_id, ids] : subscriptions_) {
        bool match = false;
        for (const auto& id : ids) match |= id == entity_id;
        if (!match) continue;
        // A full replacement in the "+" delta: state and every attribute.
        cJSON* ev = cJSON_CreateObject();
        cJSON* c = cJSON_AddObjectToObject(ev, "c");
        cJSON* e = cJSON_AddObjectToObject(c, entity_id.c_str());
        cJSON_AddItemToObject(e, "+", cJSON_Duplicate(ent, true));
        event(sub_id, ev);
    }
}

} // namespace platform_simulator
