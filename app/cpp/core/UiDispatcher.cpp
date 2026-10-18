#include "UiDispatcher.h"

#include <algorithm>
#include <cstdio>

namespace app::core {

namespace {
// A stable, unique-per-thread/task identifier for drain()'s thread-
// affinity check, deliberately NOT std::this_thread::get_id(): on
// ESP-IDF, that calls pthread_self() internally, which unconditionally
// asserts ("Failed to find current thread ID!") when invoked from any raw
// FreeRTOS task that was never created via pthread_create() — which
// includes ESP-IDF's own main_task, i.e. exactly the task that calls
// app_cpp_poll() -> Application::poll() -> this drain(). A thread_local
// variable's address is unique per real thread of execution without
// requiring any such registration, and works identically on desktop
// (real OS threads) and ESP32 (FreeRTOS tasks).
thread_local char g_thread_marker = 0;
const void* current_thread_marker() { return &g_thread_marker; }
} // namespace

UiDispatcher::UiDispatcher(size_t capacity, ILogger* logger)
    : slots_(capacity), logger_(logger) {}

void UiDispatcher::post(void (*fn)(void* ctx), void* ctx, const PostOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (options.coalesce) {
        for (auto& slot : slots_) {
            if (slot.occupied && slot.command.coalesce &&
                slot.command.coalesce_key == options.coalesce_key) {
                // Replace in place: keep original seq (queue position),
                // so a rapidly-updating key can't starve older entries.
                slot.command.fn = fn;
                slot.command.ctx = ctx;
                slot.command.epoch = options.epoch;
                slot.command.generation = options.generation;
                slot.command.critical = options.critical;
                slot.command.cancellation = options.cancellation;
                ++telemetry_.total_coalesced;
                return;
            }
        }
    }

    // Find a free slot.
    for (auto& slot : slots_) {
        if (!slot.occupied) {
            slot.occupied = true;
            slot.seq = next_seq_++;
            slot.command = Command{fn, ctx, options.epoch, options.generation,
                                    options.critical, options.coalesce,
                                    options.coalesce_key, options.cancellation};
            ++count_;
            ++telemetry_.total_posted;
            telemetry_.current_depth = count_;
            telemetry_.high_water_mark = std::max(telemetry_.high_water_mark, count_);
            return;
        }
    }

    // Queue full. A non-critical command never evicts existing work — it
    // is simply dropped ("Low-Priority-Progress verwerfen", §10). Only a
    // critical command is allowed to force room for itself.
    if (!options.critical) {
        ++telemetry_.total_dropped_overflow;
        if (logger_) logger_->log(LogLevel::Warn, "UiDispatcher: dropped non-critical command, queue full");
        return;
    }

    Slot* victim = nullptr;
    for (auto& slot : slots_) {
        if (!slot.command.critical && (!victim || slot.seq < victim->seq)) {
            victim = &slot;
        }
    }
    if (!victim) {
        // Every slot holds a critical command; must still bound memory.
        // Evict the oldest critical one rather than growing unboundedly.
        for (auto& slot : slots_) {
            if (!victim || slot.seq < victim->seq) victim = &slot;
        }
        ++telemetry_.critical_overflow_count;
    }

    ++telemetry_.total_dropped_overflow;
    // Keep the victim's original seq: the evicting critical command takes
    // over its queue position (drains where the evicted one would have),
    // rather than joining at the back behind already-queued work.
    victim->command = Command{fn, ctx, options.epoch, options.generation,
                               options.critical, options.coalesce,
                               options.coalesce_key, options.cancellation};
    telemetry_.current_depth = count_;
    telemetry_.high_water_mark = std::max(telemetry_.high_water_mark, count_);
}

void UiDispatcher::drain() {
    std::vector<Command> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const void* this_thread = current_thread_marker();
        if (!ui_thread_known_) {
            ui_thread_id_ = this_thread;
            ui_thread_known_ = true;
        } else if (this_thread != ui_thread_id_) {
            ++telemetry_.thread_violation_count;
            if (logger_) logger_->log(LogLevel::Error, "UiDispatcher::drain() called from a different thread than before");
        }

        // Collect occupied slots ordered by seq (insertion order), then
        // clear the queue up front so drain() is reentrancy-safe: a
        // command's fn is allowed to call post() again without deadlocking
        // or corrupting slots still being iterated below.
        std::vector<Slot*> occupied;
        occupied.reserve(count_);
        for (auto& slot : slots_) {
            if (slot.occupied) occupied.push_back(&slot);
        }
        std::sort(occupied.begin(), occupied.end(),
                  [](const Slot* a, const Slot* b) { return a->seq < b->seq; });

        ready.reserve(occupied.size());
        for (Slot* slot : occupied) {
            ready.push_back(slot->command);
            slot->occupied = false;
        }
        count_ = 0;
        telemetry_.current_depth = 0;
    }

    for (const Command& cmd : ready) {
        if (cmd.cancellation.is_cancelled()) {
            ++telemetry_.total_dropped_cancelled;
            continue;
        }
        bool epoch_stale = cmd.epoch.value() != 0 && cmd.epoch != current_epoch_;
        bool generation_stale = cmd.generation.value() != 0 && cmd.generation != current_generation_;
        if (epoch_stale || generation_stale) {
            ++telemetry_.total_dropped_stale;
            continue;
        }
        if (cmd.fn) cmd.fn(cmd.ctx);
        ++telemetry_.total_drained;
    }
}

void UiDispatcher::set_current_epoch(ConnectionEpoch epoch) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_epoch_ = epoch;
}

void UiDispatcher::set_current_generation(DashboardGeneration generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_generation_ = generation;
}

DispatcherTelemetry UiDispatcher::telemetry() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_;
}

} // namespace app::core
