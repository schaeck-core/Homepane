#ifndef APP_CPP_UI_TILES_CALENDARTILE_H
#define APP_CPP_UI_TILES_CALENDARTILE_H

#include "TileWidget.h"

#include <vector>

struct _lv_timer_t;
using lv_timer_t = _lv_timer_t;

namespace app::ui {

// A calendar entity as a tile: its next events of the coming week (when +
// what), fetched with calendar.get_events. Refreshed when the entity
// changes (an event starts or ends) and every 15 minutes.
class CalendarTile final : public TileWidget {
public:
    using TileWidget::TileWidget;
    ~CalendarTile() override;

    struct Event {
        double start = 0; // epoch seconds
        bool all_day = false;
        std::string summary;
    };
    // The "response" of calendar.get_events for entity_id, sorted by start (pure; host-tested).
    static std::vector<Event> parse_events(const std::string& result_json, const std::string& entity_id);
    // "Heute · 18:00", "Morgen · Ganztägig", "Sa. · 09:30" within a week (pure; now: epoch seconds).
    static std::string when_text(const Event& e, double now);

    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;
    void fetch();

protected:
    void build(lv_obj_t* tile) override;
    void update() override;

private:
    std::vector<Event> events_;
    bool fetched_ = false;
    app::core::CancellationSource pending_;
    lv_timer_t* refresh_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* list_ = nullptr;
};

} // namespace app::ui

#endif // APP_CPP_UI_TILES_CALENDARTILE_H
