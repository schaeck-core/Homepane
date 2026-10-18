#ifndef APP_CPP_CORE_ITASKRUNNER_H
#define APP_CPP_CORE_ITASKRUNNER_H

#include "CancellationToken.h"
#include "ConnectionEpoch.h"
#include "DashboardGeneration.h"

#include <cstdint>

namespace app::core {

// Extended in Phase 2 (CPP_MIGRATION_PLAN.md §10) with the metadata the
// real bounded UiDispatcher needs: epoch/generation to drop stale work,
// critical to exempt control/auth/disconnect events from coalescing, and
// an optional coalesce key so repeated updates to the same logical thing
// collapse to the latest instead of growing the queue unboundedly.
struct PostOptions {
    ConnectionEpoch epoch{};
    DashboardGeneration generation{};
    bool critical = false;
    bool coalesce = false;
    uint32_t coalesce_key = 0;
    CancellationToken cancellation{};
};

class ITaskRunner {
public:
    virtual ~ITaskRunner() = default;
    virtual void post(void (*fn)(void* ctx), void* ctx, const PostOptions& options = {}) = 0;

    // Runs queued work from the calling (UI) context. No-op by default:
    // simple synchronous implementations don't need it, UiDispatcher
    // (Phase 2) is the real implementation.
    virtual void drain() {}
};

} // namespace app::core

#endif // APP_CPP_CORE_ITASKRUNNER_H
