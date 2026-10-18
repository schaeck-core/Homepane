#ifndef APP_CPP_HOME_ASSISTANT_QUERYTYPES_H
#define APP_CPP_HOME_ASSISTANT_QUERYTYPES_H

#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace app::home_assistant {

// One day of a weather/subscribe_forecast event's "forecast" array.
// `datetime` is kept as HA's own ISO8601 text — display-only here, no
// downstream code needs to parse it into a calendar type.
struct ForecastDay {
    std::string condition;
    std::optional<double> temperature; // "high", or the single-value forecast temp
    std::optional<double> templow;
    std::optional<double> precipitation;             // mm
    std::optional<double> precipitation_probability; // %
    std::string datetime;
};

struct ForecastState {
    std::vector<ForecastDay> days;
    bool has_data = false; // false until the first event arrives (mirrors Availability::loading)
};

// A recorder/statistics_during_period request. Query IDENTITY, not the
// HA request id (which is ephemeral and gets reused every periodic
// refresh) — CPP_MIGRATION_PLAN.md Phase 9 DoD: "Key basiert auf
// kompletter Query, nicht Request-ID". entity_ids/stat_types are
// compared as given (already-sorted callers get proper dedup; an
// unsorted duplicate query with reordered entities is treated as a
// distinct query — acceptable since DashboardParser preserves the
// card's own JSON array order and callers naturally build these the
// same way every time for the same card).
struct StatisticsQuery {
    std::vector<std::string> entity_ids;
    std::string period; // "5minute" | "hour" | "day"
    std::vector<std::string> stat_types; // "mean" | "min" | "max" | ...
    int days_to_show = 1;

    bool operator<(const StatisticsQuery& other) const {
        return std::tie(entity_ids, period, stat_types, days_to_show) <
               std::tie(other.entity_ids, other.period, other.stat_types, other.days_to_show);
    }
    bool operator==(const StatisticsQuery& other) const { return !(*this < other) && !(other < *this); }
};

struct StatisticsPoint {
    double timestamp_epoch_s = 0.0;
    double value = 0.0;
};

struct StatisticsSeries {
    std::string entity_id;
    std::string stat_type;
    std::vector<StatisticsPoint> points; // bounded — see QueryCoordinator's kMaxPointsPerSeries
};

struct StatisticsResult {
    std::vector<StatisticsSeries> series;
    bool has_data = false;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_QUERYTYPES_H
