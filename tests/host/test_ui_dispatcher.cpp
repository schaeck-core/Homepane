// Contract tests for UiDispatcher (Phase 2 of CPP_MIGRATION_PLAN.md §10).
// No LVGL, no network — pure host-side verification of ordering,
// coalescing, overflow policy, cancellation and stale-generation handling.

#include "../../app/cpp/core/UiDispatcher.h"

#include <cstdio>
#include <thread>
#include <vector>

using app::core::CancellationSource;
using app::core::ConnectionEpoch;
using app::core::DashboardGeneration;
using app::core::PostOptions;
using app::core::UiDispatcher;

namespace {

int g_failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description);
    }
}

// Helper: ctx points at a small struct owned by the caller (stack-local in
// each test), so trace_cb needs no heap allocation of its own.
struct TraceCtx {
    std::vector<int>* trace;
    int value;
};

void trace_cb(void* ctx) {
    auto* t = reinterpret_cast<TraceCtx*>(ctx);
    t->trace->push_back(t->value);
}

void test_order() {
    UiDispatcher dispatcher(8);
    std::vector<int> trace;
    TraceCtx a{&trace, 1}, b{&trace, 2}, c{&trace, 3};

    dispatcher.post(trace_cb, &a);
    dispatcher.post(trace_cb, &b);
    dispatcher.post(trace_cb, &c);
    dispatcher.drain();

    expect(trace.size() == 3 && trace[0] == 1 && trace[1] == 2 && trace[2] == 3,
           "post() order is preserved (FIFO) through drain()");
}

void test_coalescing() {
    UiDispatcher dispatcher(8);
    std::vector<int> trace;
    TraceCtx a{&trace, 1}, b{&trace, 2}, c{&trace, 30}; // c replaces a's content

    PostOptions coalesced;
    coalesced.coalesce = true;
    coalesced.coalesce_key = 42;

    dispatcher.post(trace_cb, &a, coalesced);  // slot 0, key 42
    dispatcher.post(trace_cb, &b);              // slot 1, no coalescing
    dispatcher.post(trace_cb, &c, coalesced);  // should replace slot 0's content, keep position

    dispatcher.drain();

    expect(trace.size() == 2, "coalesced post() does not grow the queue");
    expect(!trace.empty() && trace[0] == 30, "coalesced replacement keeps original queue position");
    expect(trace.size() > 1 && trace[1] == 2, "non-coalesced entry runs after the coalesced slot");
    expect(dispatcher.telemetry().total_coalesced == 1, "telemetry counts exactly one coalesce");
    expect(dispatcher.telemetry().total_posted == 2, "telemetry counts only genuinely new admissions as posted");
}

void test_overflow_noncritical_dropped() {
    UiDispatcher dispatcher(2); // tiny capacity to force overflow
    std::vector<int> trace;
    TraceCtx a{&trace, 1}, b{&trace, 2}, c{&trace, 3};

    dispatcher.post(trace_cb, &a);
    dispatcher.post(trace_cb, &b);
    dispatcher.post(trace_cb, &c); // queue full, c is non-critical -> dropped

    dispatcher.drain();

    expect(trace.size() == 2 && trace[0] == 1 && trace[1] == 2,
           "non-critical post() is dropped (not evicting older work) when the queue is full");
    expect(dispatcher.telemetry().total_dropped_overflow == 1, "overflow drop is counted");
}

void test_overflow_critical_evicts_noncritical() {
    UiDispatcher dispatcher(2);
    std::vector<int> trace;
    TraceCtx a{&trace, 1}, b{&trace, 2}, critical{&trace, 99};

    dispatcher.post(trace_cb, &a);
    dispatcher.post(trace_cb, &b);

    PostOptions opts;
    opts.critical = true;
    dispatcher.post(trace_cb, &critical, opts); // must evict oldest non-critical (a)

    dispatcher.drain();

    expect(trace.size() == 2, "critical post() still respects the fixed capacity");
    expect(!trace.empty() && trace[0] == 99, "critical command evicts the oldest non-critical slot's position");
    expect(trace.size() > 1 && trace[1] == 2, "the newer non-critical command survives eviction");
}

void test_cancellation() {
    UiDispatcher dispatcher(8);
    std::vector<int> trace;
    TraceCtx a{&trace, 1};

    CancellationSource source;
    PostOptions opts;
    opts.cancellation = source.token();

    dispatcher.post(trace_cb, &a, opts);
    source.cancel(); // cancel before drain

    dispatcher.drain();

    expect(trace.empty(), "cancelled command does not run");
    expect(dispatcher.telemetry().total_dropped_cancelled == 1, "cancellation is counted separately from overflow/staleness");
}

void test_stale_generation() {
    UiDispatcher dispatcher(8);
    std::vector<int> trace;
    TraceCtx stale{&trace, 1}, fresh{&trace, 2};

    dispatcher.set_current_generation(DashboardGeneration(2));

    PostOptions stale_opts;
    stale_opts.generation = DashboardGeneration(1); // superseded generation
    dispatcher.post(trace_cb, &stale, stale_opts);

    PostOptions fresh_opts;
    fresh_opts.generation = DashboardGeneration(2); // current generation
    dispatcher.post(trace_cb, &fresh, fresh_opts);

    dispatcher.drain();

    expect(trace.size() == 1 && trace[0] == 2, "command tagged with a superseded generation is skipped, current one runs");
    expect(dispatcher.telemetry().total_dropped_stale == 1, "stale-generation drop is counted");
}

void test_stale_epoch() {
    UiDispatcher dispatcher(8);
    std::vector<int> trace;
    TraceCtx stale{&trace, 1};

    dispatcher.set_current_epoch(ConnectionEpoch(5));

    PostOptions stale_opts;
    stale_opts.epoch = ConnectionEpoch(4); // old connection attempt
    dispatcher.post(trace_cb, &stale, stale_opts);
    dispatcher.drain();

    expect(trace.empty(), "command tagged with a superseded connection epoch is skipped");
}

void test_thread_violation_detection() {
    UiDispatcher dispatcher(8);
    dispatcher.drain(); // establishes "the UI thread" as this thread

    std::thread other([&dispatcher]() { dispatcher.drain(); });
    other.join();

    expect(dispatcher.telemetry().thread_violation_count == 1,
           "drain() from a second thread is detected and counted, not silently accepted");
}

} // namespace

int main() {
    test_order();
    test_coalescing();
    test_overflow_noncritical_dropped();
    test_overflow_critical_evicts_noncritical();
    test_cancellation();
    test_stale_generation();
    test_stale_epoch();
    test_thread_violation_detection();

    if (g_failures == 0) {
        std::printf("All UiDispatcher contract tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
