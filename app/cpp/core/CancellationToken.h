#ifndef APP_CPP_CORE_CANCELLATIONTOKEN_H
#define APP_CPP_CORE_CANCELLATIONTOKEN_H

#include <atomic>
#include <memory>

namespace app::core {

// Move-only RAII cancellation source. Cancelling is idempotent and safe to
// call from any thread. A CancellationSource produces lightweight, copyable
// CancellationToken "readers" that queued commands carry along; the
// dispatcher checks is_cancelled() at drain time and skips the command
// without invoking it (docs/ARCHITECTURE.md §5.2, Phase 2).
class CancellationToken {
public:
    // No attached source: is_cancelled() always returns false.
    CancellationToken() = default;

    bool is_cancelled() const {
        return flag_ && flag_->load(std::memory_order_acquire);
    }

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> flag) : flag_(std::move(flag)) {}
    std::shared_ptr<std::atomic<bool>> flag_;
};

class CancellationSource {
public:
    CancellationSource() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) = default;
    CancellationSource& operator=(CancellationSource&&) = default;

    void cancel() {
        if (flag_) flag_->store(true, std::memory_order_release);
    }

    bool is_cancelled() const {
        return flag_ && flag_->load(std::memory_order_acquire);
    }

    CancellationToken token() const { return CancellationToken(flag_); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

} // namespace app::core

#endif // APP_CPP_CORE_CANCELLATIONTOKEN_H
