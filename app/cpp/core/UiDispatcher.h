#ifndef APP_CPP_CORE_UIDISPATCHER_H
#define APP_CPP_CORE_UIDISPATCHER_H

#include "Command.h"
#include "ITaskRunner.h"
#include "ILogger.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace app::core {

struct DispatcherTelemetry {
    uint64_t total_posted = 0;
    uint64_t total_drained = 0;
    uint64_t total_coalesced = 0;
    uint64_t total_dropped_overflow = 0;
    uint64_t total_dropped_stale = 0;
    uint64_t total_dropped_cancelled = 0;
    uint64_t critical_overflow_count = 0;
    uint64_t thread_violation_count = 0;
    size_t high_water_mark = 0;
    size_t current_depth = 0;
};

// Bounded, thread-safe command queue (docs/ARCHITECTURE.md §9). Any
// thread may call post(); only the UI thread may call drain() (checked,
// not just assumed — see thread_violation_count).
//
// Conventions:
//  - epoch/generation value 0 (the default) means "not scoped to a
//    connection/dashboard instance"; such commands are never considered
//    stale. Real network code tags a nonzero epoch/generation once one
//    exists (from Phase 3/6 onward).
//  - Capacity is fixed at construction; post() never allocates a growing
//    container, only fills/evicts fixed slots.
class UiDispatcher final : public ITaskRunner {
public:
    explicit UiDispatcher(size_t capacity, ILogger* logger = nullptr);

    void post(void (*fn)(void* ctx), void* ctx, const PostOptions& options = {}) override;

    // Runs all currently-queued, non-stale, non-cancelled commands in
    // FIFO order (coalesced commands keep their original position).
    void drain() override;

    void set_current_epoch(ConnectionEpoch epoch);
    void set_current_generation(DashboardGeneration generation);

    DispatcherTelemetry telemetry() const;

private:
    struct Slot {
        bool occupied = false;
        uint64_t seq = 0;
        Command command;
    };

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    size_t count_ = 0;
    uint64_t next_seq_ = 0;

    ConnectionEpoch current_epoch_{};
    DashboardGeneration current_generation_{};

    DispatcherTelemetry telemetry_;

    // Identifies the calling thread/task without std::this_thread::get_id()
    // (see UiDispatcher.cpp for why — it crashes on ESP-IDF's own
    // main_task, a raw FreeRTOS task never created via pthread_create()).
    bool ui_thread_known_ = false;
    const void* ui_thread_id_ = nullptr;

    ILogger* logger_;
};

} // namespace app::core

#endif // APP_CPP_CORE_UIDISPATCHER_H
