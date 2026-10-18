#ifndef APP_CPP_HOME_ASSISTANT_QUERYCOORDINATOR_H
#define APP_CPP_HOME_ASSISTANT_QUERYCOORDINATOR_H

#include "../core/CancellationToken.h"
#include "../core/IMonotonicClock.h"
#include "../core/IWallClock.h"
#include "HaClient.h"
#include "QueryTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace app::home_assistant {

// Sits beside EntitySubscriptionCoordinator (C++ migration phase
// 9 "spezielle Datenquellen"): handles the two HA data shapes that are
// not "one entity's state".
//
// Forecast: `weather/subscribe_forecast` is a normal HA subscribe
// command (one Result ack, then repeated `event` messages by id until
// the connection drops) — dedup and reconnect/resubscribe mirror
// EntitySubscriptionCoordinator almost exactly. Deliberately simpler in
// one respect: no debounced unsubscribe when the last observer leaves —
// a record just stays subscribed for the connection's lifetime once
// created. No live fleet depends on that optimization yet (the
// migration's own no-live-fleet recalibration).
//
// Statistics: `recorder/statistics_during_period` is a plain one-shot
// command (HaClient::send_command) with no HA-side subscription —
// QueryCoordinator's own poll()-driven scheduler re-issues it
// periodically. Keyed by the full StatisticsQuery, not by request id (a
// request id is ephemeral and gets reused every refresh) — N observers
// of an identical query share one periodic fetch and its cached
// last-known-good result.
class QueryCoordinator {
public:
    using ForecastObserverFn = std::function<void(const ForecastState&)>;
    using StatisticsObserverFn = std::function<void(const StatisticsResult&)>;

    // Move-only handle; same shape as EntitySubscriptionCoordinator's own
    // SubscriptionHandle (raw back-pointer, not EntityStore's deeper
    // weak_ptr self-ref scheme — QueryCoordinator is, like
    // EntitySubscriptionCoordinator, always owned at the composition
    // root and outlives every DashboardSession that hands out handles).
    class ForecastHandle {
    public:
        ForecastHandle() = default;
        ForecastHandle(ForecastHandle&&) noexcept = default;
        ForecastHandle& operator=(ForecastHandle&&) noexcept = default;
        ForecastHandle(const ForecastHandle&) = delete;
        ForecastHandle& operator=(const ForecastHandle&) = delete;
        ~ForecastHandle() { reset(); }
        void reset();

    private:
        friend class QueryCoordinator;
        QueryCoordinator* coordinator_ = nullptr;
        std::string key_;
        uint64_t slot_id_ = 0;
    };

    class StatisticsHandle {
    public:
        StatisticsHandle() = default;
        StatisticsHandle(StatisticsHandle&&) noexcept = default;
        StatisticsHandle& operator=(StatisticsHandle&&) noexcept = default;
        StatisticsHandle(const StatisticsHandle&) = delete;
        StatisticsHandle& operator=(const StatisticsHandle&) = delete;
        ~StatisticsHandle() { reset(); }
        void reset();

    private:
        friend class QueryCoordinator;
        QueryCoordinator* coordinator_ = nullptr;
        StatisticsQuery query_;
        uint64_t slot_id_ = 0;
    };

    QueryCoordinator(HaClient& client, app::core::IMonotonicClock& clock, app::core::IWallClock& wall_clock);

    ForecastHandle observe_forecast(const std::string& entity_id, const std::string& forecast_type,
                                     ForecastObserverFn fn);
    // Delivers the record's current state (possibly still `loading`, i.e.
    // has_data == false) to exactly this one observer, synchronously, on
    // demand — same "never inline from observe()" contract as
    // EntityStore/EntitySubscriptionCoordinator.
    void deliver_current_forecast(const std::string& entity_id, const std::string& forecast_type,
                                   const ForecastHandle& handle);

    // refresh_interval_ms: how often to re-issue the query while at least
    // one observer remains.
    StatisticsHandle observe_statistics(const StatisticsQuery& query, uint32_t refresh_interval_ms,
                                         StatisticsObserverFn fn);
    void deliver_current_statistics(const StatisticsQuery& query, const StatisticsHandle& handle);

    // Call on every transition into HaClientState::Ready, including the
    // first one: re-issues every still-observed forecast subscription and
    // immediately fetches every still-observed statistics query.
    void on_ha_ready();
    // Call on any HaClient transition out of Ready that is not a step
    // toward Ready: drops now-invalid remote subscription bookkeeping.
    // Cached results/observers are kept (ARCHITECTURE.md §4-style "stale, not gone").
    void on_ha_disconnected();
    // Drives the statistics periodic-refresh scheduler; call periodically
    // (e.g. from Application::poll()), same as every other poll()-driven
    // component in this stack.
    void poll(uint64_t now_ms);

    struct Diagnostics {
        size_t forecast_records = 0;
        size_t active_forecast_subscriptions = 0;
        size_t statistics_records = 0;
    };
    Diagnostics diagnostics() const;

private:
    struct ForecastObserverSlot {
        ForecastObserverFn fn;
        bool removed = false;
    };
    struct ForecastRecord {
        std::string entity_id;
        std::string forecast_type;
        ForecastState state;
        int remote_subscription_id = 0;
        bool subscribe_in_flight = false;
        bool has_active_subscription = false;
        std::map<uint64_t, ForecastObserverSlot> observers;
    };

    struct StatisticsObserverSlot {
        StatisticsObserverFn fn;
        bool removed = false;
    };
    struct StatisticsRecord {
        StatisticsResult result;
        uint32_t refresh_interval_ms = 60000;
        uint64_t next_refresh_due_ms = 0; // 0 = due immediately (never fetched yet)
        bool request_in_flight = false;
        app::core::CancellationSource pending_request;
        std::map<uint64_t, StatisticsObserverSlot> observers;
    };

    static std::string forecast_key(const std::string& entity_id, const std::string& forecast_type);

    void ensure_forecast_subscription(const std::string& key);
    void send_forecast_subscribe(const std::string& key);
    void notify_forecast(const std::string& key);
    void on_forecast_observer_removed(const std::string& key, uint64_t slot_id);

    void issue_statistics_request(const StatisticsQuery& query);
    void notify_statistics(const StatisticsQuery& query);
    void on_statistics_observer_removed(const StatisticsQuery& query, uint64_t slot_id);

    void on_ha_event(const DecodedMessage& msg);

    HaClient& client_;
    app::core::IMonotonicClock& clock_;
    app::core::IWallClock& wall_clock_;

    std::map<std::string, ForecastRecord> forecasts_;
    std::map<StatisticsQuery, StatisticsRecord> statistics_;
    uint64_t next_slot_id_ = 1;
};

} // namespace app::home_assistant

#endif // APP_CPP_HOME_ASSISTANT_QUERYCOORDINATOR_H
