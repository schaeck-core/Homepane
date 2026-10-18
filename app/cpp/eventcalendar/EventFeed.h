#ifndef APP_CPP_EVENTCALENDAR_EVENTFEED_H
#define APP_CPP_EVENTCALENDAR_EVENTFEED_H

#include "../core/CancellationToken.h"
#include "../home_assistant/EntitySubscriptionCoordinator.h"
#include "../home_assistant/HaClient.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace app::eventcalendar {

// The HA Eventcalendar add-on's display feed (api_version 1): a sensor
// (default sensor.ha_eventcalendar_kalenderanzeige) whose attributes carry
// the events to show (from 7 days before their start until their end) and
// the waste pickups due (the day before and on the day). Pictures are HA
// image entities matched by attributes.eventcalendar_image_key.
struct Event {
    std::string id, title, location;
    double start = 0, end = 0; // epoch seconds; all-day: local midnight, end exclusive
    bool all_day = false;
    std::optional<double> distance_km;
    std::string distance_type; // "Luftlinie" (air distance) or empty
    std::string image_key, image_kind; // image_kind: "event" (from the source) or "symbol"
};

struct Pickup {
    std::string id, title, day_label; // day_label: "Heute" / "Morgen"
};

struct Feed {
    bool available = false; // a valid api_version 1 state
    std::vector<Event> events;
    std::vector<Pickup> waste;
    int card_seconds = 10;  // how long each event shows
    int idle_seconds = 45;  // pause of the automatic change after a swipe
};

// Attributes (name -> JSON text, as in EntityStore) -> Feed (pure; host-tested).
Feed parse_feed(const std::string& state, const std::map<std::string, std::string>& attributes);

// Watches the feed sensor and the image entities of its events
// (image.ha_eventcalendar_<image_key>, taken only when its
// eventcalendar_image_key attribute matches — no get_states, which takes
// seconds on the panel). UI thread.
class EventFeed {
public:
    using Changed = std::function<void()>;

    void attach(app::home_assistant::EntitySubscriptionCoordinator* coordinator, app::home_assistant::HaClient* client,
                const std::string& sensor, Changed changed);

    const Feed& feed() const { return feed_; }
    // "image.ha_eventcalendar_..." for an image_key, "" while unknown.
    std::string image_entity(const std::string& image_key) const;

private:
    void on_state(const app::home_assistant::EntityState& state);
    void watch_images();
    void on_image_state(const std::string& key, const app::home_assistant::EntityState& state);

    Feed feed_;
    Changed changed_;
    app::home_assistant::EntitySubscriptionCoordinator* coordinator_ = nullptr;
    app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle handle_;
    std::map<std::string, std::string> images_; // image_key -> image entity (confirmed)
    std::map<std::string, app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle> image_subs_;
};

} // namespace app::eventcalendar

#endif // APP_CPP_EVENTCALENDAR_EVENTFEED_H
