#ifndef APP_CPP_CORE_COMMAND_H
#define APP_CPP_CORE_COMMAND_H

#include "CancellationToken.h"
#include "ConnectionEpoch.h"
#include "DashboardGeneration.h"

#include <cstdint>

namespace app::core {

// A small, owned, value-type unit of work posted from any producer thread
// (network/worker) and executed only by UiDispatcher::drain() in the UI
// context. `ctx` ownership is the poster's responsibility: for Phase 2
// there is no payload lifetime management beyond the raw pointer — that
// lands with the real network/store events in later phases.
struct Command {
    void (*fn)(void* ctx) = nullptr;
    void* ctx = nullptr;

    ConnectionEpoch epoch{};
    DashboardGeneration generation{};

    // Critical commands are never silently dropped/coalesced away, even
    // under overflow (CPP_MIGRATION_PLAN.md §10 Queue-Regeln).
    bool critical = false;

    // If coalesce is set, a not-yet-drained queued command with the same
    // coalesce_key is replaced in place instead of a new slot being used.
    bool coalesce = false;
    uint32_t coalesce_key = 0;

    // Default-constructed token is never cancelled.
    CancellationToken cancellation{};
};

} // namespace app::core

#endif // APP_CPP_CORE_COMMAND_H
