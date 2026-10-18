#include "DefaultWidgetRegistration.h"

#include "../ui/ClockWidget.h"
#include "../ui/LightWidget.h"
#include "../ui/UnsupportedWidget.h"
#include "../ui/WeatherWidget.h"
#include "../ui/tiles/CalendarTile.h"
#include "../ui/tiles/CameraTile.h"
#include "../ui/tiles/ChartTile.h"
#include "../ui/tiles/ClimateTile.h"
#include "../ui/tiles/CoverTile.h"
#include "../ui/tiles/CompactTile.h"
#include "../ui/tiles/EntityTile.h"
#include "../ui/tiles/MediaTile.h"
#include "../ui/tiles/OptionsTile.h"
#include "../ui/tiles/TodoTile.h"
#include "../ui/tiles/ValueTile.h"

#include <cjson/cJSON.h>

#include <memory>

namespace app::dashboard {

using app::home_assistant::HaCommandService;
using app::home_assistant::HaInstanceId;
using app::home_assistant::StatisticsQuery;
using app::ui::ClockWidget;
using app::ui::LightWidget;
using app::ui::UnsupportedWidget;
using app::ui::WeatherWidget;
using app::ui::Widget;

namespace {
std::string entity_domain(const std::string& entity_id) {
    auto dot = entity_id.find('.');
    return dot == std::string::npos ? std::string{} : entity_id.substr(0, dot);
}

// Extracts a statistics-graph card's own config directly from its raw
// JSON (CPP_MIGRATION_PLAN.md §8.2: DashboardCard deliberately keeps
// these fields out of the generic model — only the widget that needs
// them parses them, matching the legacy app/widgets/statistics_graph.c's
// own parse_entities/parse_stat_types).
struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

struct StatisticsCardConfig {
    StatisticsQuery query;
    std::string title;
};

StatisticsCardConfig parse_statistics_card_config(const std::string& raw_json) {
    StatisticsCardConfig config;
    config.query.period = "hour";
    // Always all three: StatisticsWidget's envelope-fill-behind-smoothed-
    // mean rendering (a direct port of the legacy widget's own
    // statistics_graph_update()) needs mean/min/max together. The legacy
    // widget parsed a card-level "stat_types" override too, but its own
    // request never actually sent stat_types to HA (recorder/
    // statistics_during_period returns every available field regardless)
    // — the override was already inert there, so it's not carried
    // forward here rather than risk a card that only requests "mean"
    // silently losing its envelope.
    config.query.stat_types = {"mean", "min", "max"};
    config.query.days_to_show = 1;

    CJsonPtr root(cJSON_Parse(raw_json.c_str()));
    if (!root || !cJSON_IsObject(root.get())) return config;

    cJSON* title = cJSON_GetObjectItemCaseSensitive(root.get(), "title");
    if (title && cJSON_IsString(title)) config.title = title->valuestring;

    cJSON* period = cJSON_GetObjectItemCaseSensitive(root.get(), "period");
    if (period && cJSON_IsString(period)) config.query.period = period->valuestring;

    cJSON* days = cJSON_GetObjectItemCaseSensitive(root.get(), "days_to_show");
    if (days && cJSON_IsNumber(days)) config.query.days_to_show = days->valueint;

    // history-graph: hours_to_show (default 24) instead of days_to_show
    cJSON* hours = cJSON_GetObjectItemCaseSensitive(root.get(), "hours_to_show");
    if (hours && cJSON_IsNumber(hours)) config.query.days_to_show = hours->valueint > 24 ? (hours->valueint + 23) / 24 : 1;

    // "sensor.x" or {"entity": "sensor.x", ...}
    cJSON* entities = cJSON_GetObjectItemCaseSensitive(root.get(), "entities");
    if (entities && cJSON_IsArray(entities)) {
        config.query.entity_ids.clear();
        cJSON* entity = nullptr;
        cJSON_ArrayForEach(entity, entities) {
            if (cJSON_IsString(entity)) config.query.entity_ids.emplace_back(entity->valuestring);
            cJSON* id = cJSON_IsObject(entity) ? cJSON_GetObjectItemCaseSensitive(entity, "entity") : nullptr;
            if (cJSON_IsString(id)) config.query.entity_ids.emplace_back(id->valuestring);
        }
    }

    return config;
}
// Tiles show entity names without the section title in front (see
// TileWidget::set_name_prefix).
template <typename T>
std::unique_ptr<Widget> with_prefix(std::unique_ptr<T> tile, const DashboardCard& card) {
    tile->set_name_prefix(card.section_title);
    return tile;
}
// The tile for one entity, chosen by its domain.
std::unique_ptr<Widget> tile_for_entity(const HaInstanceId& instance, HaCommandService& commands,
                                        const DashboardCard& card) {
    const std::string domain = entity_domain(card.entity_id);
    if (domain == "sensor" || domain == "number" || domain == "input_number" || domain == "update" ||
        domain == "weather" || domain == "input_text" || domain == "text" || domain == "air_quality" ||
        domain == "date" || domain == "time" || domain == "datetime" || domain == "input_datetime" ||
        domain == "event" || domain == "counter" || domain == "sun" || domain == "zone" || domain == "timer") {
        return with_prefix(std::make_unique<app::ui::ValueTile>(instance, &commands, card.entity_id), card);
    }
    if (domain == "cover") {
        return with_prefix(std::make_unique<app::ui::CoverTile>(instance, &commands, card.entity_id), card);
    }
    if (domain == "climate" || domain == "humidifier" || domain == "water_heater") {
        return with_prefix(std::make_unique<app::ui::ClimateTile>(instance, &commands, card.entity_id), card);
    }
    if (domain == "media_player") {
        return with_prefix(std::make_unique<app::ui::MediaTile>(instance, &commands, card.entity_id), card);
    }
    if (domain == "input_select" || domain == "select") {
        return std::make_unique<app::ui::OptionsTile>(instance, commands, card.entity_id);
    }
    if (domain == "light") {
        return std::make_unique<LightWidget>(instance, commands, card.entity_id);
    }
    if (domain == "camera") {
        return with_prefix(std::make_unique<app::ui::CameraTile>(instance, &commands, card.entity_id), card);
    }
    if (domain == "calendar") {
        return with_prefix(std::make_unique<app::ui::CalendarTile>(instance, &commands, card.entity_id), card);
    }
    if (domain == "todo") {
        return with_prefix(std::make_unique<app::ui::TodoTile>(instance, &commands, card.entity_id), card);
    }
    // Everything else: the centred entity tile (switch, fan, lock, scene, binary_sensor, person, remote, ...;
    // unknown domains show their raw state).
    if (!card.entity_id.empty()) {
        return with_prefix(std::make_unique<app::ui::EntityTile>(instance, &commands, card.entity_id), card);
    }
    return std::make_unique<UnsupportedWidget>("tile:" + (domain.empty() ? std::string("?") : domain));
}
} // namespace

void register_default_widgets(WidgetFactory& factory, HaInstanceId instance, HaCommandService& commands) {
    // "entities" card: the buttons tile (scenes, scripts, switches; up to three).
    factory.register_type("entities", [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        return app::ui::OptionsTile::from_entities_card(instance, commands, card.raw_json);
    });

    factory.register_type("clock", [](const DashboardCard&) -> std::unique_ptr<Widget> {
        return std::make_unique<ClockWidget>();
    });

    factory.register_type("light", [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        return std::make_unique<LightWidget>(instance, commands, card.entity_id);
    });

    factory.register_type("thermostat", [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        return with_prefix(std::make_unique<app::ui::ClimateTile>(instance, &commands, card.entity_id), card);
    });

    factory.register_type("weather-forecast", [instance](const DashboardCard& card) -> std::unique_ptr<Widget> {
        return std::make_unique<WeatherWidget>(instance, card.entity_id);
    });

    factory.register_type("statistics-graph", [instance](const DashboardCard& card) -> std::unique_ptr<Widget> {
        StatisticsCardConfig config = parse_statistics_card_config(card.raw_json);
        return std::make_unique<app::ui::ChartTile>(instance, std::move(config.query), std::move(config.title));
    });

    // "tile" is generic: the widget follows the entity's domain.
    factory.register_type("tile", [instance, &commands](const DashboardCard& card) {
        return tile_for_entity(instance, commands, card);
    });
    // Single-entity cards of other types show the same tile (their own
    // look is HA frontend styling the panel replaces with the guide's).
    for (const char* type : {"entity", "button", "sensor", "gauge", "media-control", "humidifier", "alarm-panel",
                             "picture-entity"}) {
        factory.register_type(type, [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
            if (card.entity_id.empty()) return std::make_unique<UnsupportedWidget>(card.type);
            return tile_for_entity(instance, commands, card);
        });
    }
    // "todo-list": one list
    factory.register_type("todo-list", [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        if (card.entity_id.empty()) return std::make_unique<UnsupportedWidget>(card.type);
        return tile_for_entity(instance, commands, card);
    });
    // "calendar": the first of its calendars ("entities")
    factory.register_type("calendar", [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        DashboardCard first = card;
        CJsonPtr root(cJSON_Parse(card.raw_json.c_str()));
        cJSON* entities = root ? cJSON_GetObjectItemCaseSensitive(root.get(), "entities") : nullptr;
        cJSON* e = cJSON_IsArray(entities) ? cJSON_GetArrayItem(entities, 0) : nullptr;
        if (cJSON_IsString(e)) first.entity_id = e->valuestring;
        if (first.entity_id.empty()) return std::make_unique<UnsupportedWidget>(card.type);
        return tile_for_entity(instance, commands, first);
    });
    // "glance": several entities with their state, like the entities card
    factory.register_type("glance", [instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        return app::ui::OptionsTile::from_entities_card(instance, commands, card.raw_json);
    });
    // Small HA cards (DashboardLayout's CellKind::Compact): the compact tile.
    factory.register_compact([instance, &commands](const DashboardCard& card) -> std::unique_ptr<Widget> {
        return with_prefix(std::make_unique<app::ui::CompactTile>(instance, &commands, card.entity_id), card);
    });
    // "history-graph": the chart tile over the recorder's statistics
    factory.register_type("history-graph", [instance](const DashboardCard& card) -> std::unique_ptr<Widget> {
        StatisticsCardConfig config = parse_statistics_card_config(card.raw_json);
        return std::make_unique<app::ui::ChartTile>(instance, std::move(config.query), std::move(config.title));
    });
}

} // namespace app::dashboard
