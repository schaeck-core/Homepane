#include "QueryCoordinator.h"

#include <cjson/cJSON.h>

#include <ctime>
#include <memory>
#include <utility>

namespace app::home_assistant {

namespace {
struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

void format_iso8601_utc(double epoch_s, char* buffer, size_t buffer_size) {
    time_t t = static_cast<time_t>(epoch_s);
    struct tm tm_utc {};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    std::strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S+00:00", &tm_utc);
}

// Bounds one series' point count so a widget never has to render (or a
// dashboard switch never has to hold onto) an unbounded history —
// C++ migration phase 9 DoD: "UI erhält begrenzte Series-Daten".
constexpr size_t kMaxPointsPerSeries = 200;
} // namespace

std::string QueryCoordinator::forecast_key(const std::string& entity_id, const std::string& forecast_type) {
    return entity_id + "|" + forecast_type;
}

QueryCoordinator::QueryCoordinator(HaClient& client, app::core::IMonotonicClock& clock,
                                    app::core::IWallClock& wall_clock)
    : client_(client), clock_(clock), wall_clock_(wall_clock) {
    client_.add_event_listener([this](const DecodedMessage& msg) { on_ha_event(msg); });
}

// ---- Forecast -------------------------------------------------------------

void QueryCoordinator::ForecastHandle::reset() {
    if (coordinator_) {
        coordinator_->on_forecast_observer_removed(key_, slot_id_);
        coordinator_ = nullptr;
    }
}

void QueryCoordinator::StatisticsHandle::reset() {
    if (coordinator_) {
        coordinator_->on_statistics_observer_removed(query_, slot_id_);
        coordinator_ = nullptr;
    }
}

QueryCoordinator::ForecastHandle QueryCoordinator::observe_forecast(const std::string& entity_id,
                                                                      const std::string& forecast_type,
                                                                      ForecastObserverFn fn) {
    std::string key = forecast_key(entity_id, forecast_type);
    auto& record = forecasts_[key]; // get-or-create
    record.entity_id = entity_id;
    record.forecast_type = forecast_type;

    uint64_t slot_id = next_slot_id_++;
    record.observers[slot_id] = ForecastObserverSlot{std::move(fn), false};

    ensure_forecast_subscription(key);

    ForecastHandle handle;
    handle.coordinator_ = this;
    handle.key_ = key;
    handle.slot_id_ = slot_id;
    return handle;
}

void QueryCoordinator::deliver_current_forecast(const std::string& entity_id, const std::string& forecast_type,
                                                  const ForecastHandle& handle) {
    auto it = forecasts_.find(forecast_key(entity_id, forecast_type));
    if (it == forecasts_.end()) return;
    auto slot_it = it->second.observers.find(handle.slot_id_);
    if (slot_it == it->second.observers.end() || slot_it->second.removed) return;
    if (slot_it->second.fn) slot_it->second.fn(it->second.state);
}

void QueryCoordinator::on_forecast_observer_removed(const std::string& key, uint64_t slot_id) {
    auto it = forecasts_.find(key);
    if (it == forecasts_.end()) return;
    it->second.observers.erase(slot_id);
    // Deliberately no unsubscribe here, even if this was the last local
    // observer (see the class-level comment): the record and its remote
    // subscription simply keep running in the background at zero further
    // local cost, and a future observer for the same key re-attaches to
    // still-fresh data immediately instead of waiting for a fresh
    // subscribe+Result round trip.
}

void QueryCoordinator::ensure_forecast_subscription(const std::string& key) {
    auto& record = forecasts_[key];
    if (record.has_active_subscription || record.subscribe_in_flight) return;
    send_forecast_subscribe(key);
}

void QueryCoordinator::send_forecast_subscribe(const std::string& key) {
    auto& record = forecasts_[key];
    record.subscribe_in_flight = true;

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "type", "weather/subscribe_forecast");
    cJSON_AddStringToObject(root.get(), "forecast_type", record.forecast_type.c_str());
    cJSON_AddStringToObject(root.get(), "entity_id", record.entity_id.c_str());
    char* text = cJSON_PrintUnformatted(root.get());
    std::string cmd = text ? text : "";
    if (text) cJSON_free(text);

    client_.send_command(cmd, [this, key](const ResultMessage& result) {
        auto it = forecasts_.find(key);
        if (it == forecasts_.end()) return; // torn down (shouldn't happen — records aren't erased, defensive)
        it->second.subscribe_in_flight = false;
        if (result.success) {
            it->second.has_active_subscription = true;
            it->second.remote_subscription_id = result.id;
        }
        // On failure: left inactive. The next observe_forecast() or
        // on_ha_ready() retries via ensure_forecast_subscription() — same
        // no-automatic-retry-loop scope as EntitySubscriptionCoordinator.
    });
}

void QueryCoordinator::notify_forecast(const std::string& key) {
    auto it = forecasts_.find(key);
    if (it == forecasts_.end()) return;
    for (auto& [slot_id, slot] : it->second.observers) {
        if (!slot.removed && slot.fn) slot.fn(it->second.state);
    }
}

// ---- Statistics -------------------------------------------------------------

QueryCoordinator::StatisticsHandle QueryCoordinator::observe_statistics(const StatisticsQuery& query,
                                                                          uint32_t refresh_interval_ms,
                                                                          StatisticsObserverFn fn) {
    auto& record = statistics_[query]; // get-or-create
    record.refresh_interval_ms = refresh_interval_ms;

    uint64_t slot_id = next_slot_id_++;
    record.observers[slot_id] = StatisticsObserverSlot{std::move(fn), false};

    StatisticsHandle handle;
    handle.coordinator_ = this;
    handle.query_ = query;
    handle.slot_id_ = slot_id;
    return handle;
}

void QueryCoordinator::deliver_current_statistics(const StatisticsQuery& query, const StatisticsHandle& handle) {
    auto it = statistics_.find(query);
    if (it == statistics_.end()) return;
    auto slot_it = it->second.observers.find(handle.slot_id_);
    if (slot_it == it->second.observers.end() || slot_it->second.removed) return;
    if (slot_it->second.fn) slot_it->second.fn(it->second.result);
}

void QueryCoordinator::on_statistics_observer_removed(const StatisticsQuery& query, uint64_t slot_id) {
    auto it = statistics_.find(query);
    if (it == statistics_.end()) return;
    it->second.observers.erase(slot_id);
    if (it->second.observers.empty()) {
        // Unlike Forecast, Statistics is an *active* recurring poll this
        // client itself drives (real recorder/HA load per refresh) — stop
        // it as soon as nobody is looking, and cancel any in-flight
        // request so its callback becomes a no-op (ARCHITECTURE.md §7/§8 generation
        // safety) rather than updating a result nobody will ever read.
        it->second.pending_request.cancel();
        statistics_.erase(it);
    }
}

void QueryCoordinator::issue_statistics_request(const StatisticsQuery& query) {
    auto it = statistics_.find(query);
    if (it == statistics_.end()) return;
    auto& record = it->second;
    if (record.request_in_flight) return;
    record.request_in_flight = true;

    double now = wall_clock_.now_epoch_s();
    double start = now - (static_cast<double>(query.days_to_show) * 25.0 * 60.0 * 60.0); // matches legacy's 25h/day margin
    char start_buf[64];
    char end_buf[64];
    format_iso8601_utc(start, start_buf, sizeof(start_buf));
    format_iso8601_utc(now, end_buf, sizeof(end_buf));

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "type", "recorder/statistics_during_period");
    cJSON_AddStringToObject(root.get(), "start_time", start_buf);
    cJSON_AddStringToObject(root.get(), "end_time", end_buf);
    cJSON* statistic_ids = cJSON_CreateArray();
    for (const auto& id : query.entity_ids) cJSON_AddItemToArray(statistic_ids, cJSON_CreateString(id.c_str()));
    cJSON_AddItemToObject(root.get(), "statistic_ids", statistic_ids);
    cJSON_AddStringToObject(root.get(), "period", query.period.c_str());
    char* text = cJSON_PrintUnformatted(root.get());
    std::string cmd = text ? text : "";
    if (text) cJSON_free(text);

    record.pending_request = client_.send_command(cmd, [this, query](const ResultMessage& result) {
        auto rec_it = statistics_.find(query);
        if (rec_it == statistics_.end()) return; // last observer left mid-request
        rec_it->second.request_in_flight = false;
        rec_it->second.next_refresh_due_ms = clock_.now_ms() + rec_it->second.refresh_interval_ms;

        if (!result.success) return; // keep the last-known-good cached result, same as EntityStore-adjacent widgets

        // Response shape per HA's public recorder/statistics_during_period
        // docs (no captured real fixture exists yet, unlike the
        // entity-shaped fixtures in tests/fixtures/ha/ — see
        // docs/migration/phase0-hotspots.md convention of flagging
        // unverified assumptions explicitly):
        //   {"<entity_id>": [{"start": <epoch_s>, "mean": x, "min": y, "max": z, ...}, ...], ...}
        CJsonPtr root2(cJSON_Parse(result.result_json.c_str()));
        if (!root2 || !cJSON_IsObject(root2.get())) return;

        StatisticsResult parsed;
        parsed.has_data = true;
        for (const auto& entity_id : query.entity_ids) {
            cJSON* buckets = cJSON_GetObjectItemCaseSensitive(root2.get(), entity_id.c_str());
            if (!buckets || !cJSON_IsArray(buckets)) continue;

            for (const auto& stat_type : query.stat_types) {
                StatisticsSeries series;
                series.entity_id = entity_id;
                series.stat_type = stat_type;

                cJSON* bucket = nullptr;
                cJSON_ArrayForEach(bucket, buckets) {
                    if (series.points.size() >= kMaxPointsPerSeries) break;
                    cJSON* value = cJSON_GetObjectItemCaseSensitive(bucket, stat_type.c_str());
                    cJSON* start_field = cJSON_GetObjectItemCaseSensitive(bucket, "start");
                    if (!value || !cJSON_IsNumber(value) || !start_field || !cJSON_IsNumber(start_field)) continue;
                    series.points.push_back(StatisticsPoint{start_field->valuedouble, value->valuedouble});
                }
                parsed.series.push_back(std::move(series));
            }
        }

        rec_it->second.result = std::move(parsed);
        notify_statistics(query);
    });
}

void QueryCoordinator::notify_statistics(const StatisticsQuery& query) {
    auto it = statistics_.find(query);
    if (it == statistics_.end()) return;
    for (auto& [slot_id, slot] : it->second.observers) {
        if (!slot.removed && slot.fn) slot.fn(it->second.result);
    }
}

// ---- Lifecycle -------------------------------------------------------------

void QueryCoordinator::on_ha_ready() {
    for (auto& [key, record] : forecasts_) {
        record.has_active_subscription = false;
        record.subscribe_in_flight = false;
        if (!record.observers.empty()) ensure_forecast_subscription(key);
    }
    for (auto& [query, record] : statistics_) {
        record.next_refresh_due_ms = 0; // fetch again immediately: the connection (and any in-flight request) is new
        record.request_in_flight = false;
    }
}

void QueryCoordinator::on_ha_disconnected() {
    for (auto& [key, record] : forecasts_) {
        record.has_active_subscription = false;
        record.subscribe_in_flight = false;
    }
    for (auto& [query, record] : statistics_) {
        record.request_in_flight = false;
    }
}

void QueryCoordinator::poll(uint64_t now_ms) {
    for (auto& [query, record] : statistics_) {
        if (record.request_in_flight) continue;
        if (now_ms < record.next_refresh_due_ms) continue;
        issue_statistics_request(query);
    }
}

void QueryCoordinator::on_ha_event(const DecodedMessage& msg) {
    if (msg.kind != DecodedMessageKind::RawEvent) return;

    for (auto& [key, record] : forecasts_) {
        if (!record.has_active_subscription || record.remote_subscription_id != msg.event_subscription_id) continue;

        CJsonPtr root(cJSON_Parse(msg.raw_event_json.c_str()));
        if (!root) return;
        cJSON* forecast = cJSON_GetObjectItemCaseSensitive(root.get(), "forecast");
        if (!forecast || !cJSON_IsArray(forecast)) return;

        ForecastState state;
        state.has_data = true;
        cJSON* day = nullptr;
        cJSON_ArrayForEach(day, forecast) {
            ForecastDay parsed_day;
            cJSON* condition = cJSON_GetObjectItemCaseSensitive(day, "condition");
            if (condition && cJSON_IsString(condition)) parsed_day.condition = condition->valuestring;
            cJSON* temperature = cJSON_GetObjectItemCaseSensitive(day, "temperature");
            if (temperature && cJSON_IsNumber(temperature)) parsed_day.temperature = temperature->valuedouble;
            cJSON* templow = cJSON_GetObjectItemCaseSensitive(day, "templow");
            if (templow && cJSON_IsNumber(templow)) parsed_day.templow = templow->valuedouble;
            cJSON* precipitation = cJSON_GetObjectItemCaseSensitive(day, "precipitation");
            if (precipitation && cJSON_IsNumber(precipitation)) parsed_day.precipitation = precipitation->valuedouble;
            cJSON* probability = cJSON_GetObjectItemCaseSensitive(day, "precipitation_probability");
            if (probability && cJSON_IsNumber(probability)) parsed_day.precipitation_probability = probability->valuedouble;
            cJSON* datetime = cJSON_GetObjectItemCaseSensitive(day, "datetime");
            if (datetime && cJSON_IsString(datetime)) parsed_day.datetime = datetime->valuestring;
            state.days.push_back(std::move(parsed_day));
        }

        record.state = std::move(state);
        notify_forecast(key);
        return; // ids are unique per record; no other record can match
    }
}

QueryCoordinator::Diagnostics QueryCoordinator::diagnostics() const {
    Diagnostics d;
    d.forecast_records = forecasts_.size();
    for (const auto& [key, record] : forecasts_) {
        if (record.has_active_subscription) ++d.active_forecast_subscriptions;
    }
    d.statistics_records = statistics_.size();
    return d;
}

} // namespace app::home_assistant
