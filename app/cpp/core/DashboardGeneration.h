#ifndef APP_CPP_CORE_DASHBOARDGENERATION_H
#define APP_CPP_CORE_DASHBOARDGENERATION_H

#include <cstdint>

namespace app::core {

// Strong type: increments once per dashboard (re)load. Commands/events tag
// the generation they belong to so a late response for an already-replaced
// dashboard cannot mutate the new one (CPP_MIGRATION_PLAN.md §8/§4).
class DashboardGeneration {
public:
    constexpr DashboardGeneration() = default;
    constexpr explicit DashboardGeneration(uint32_t value) : value_(value) {}

    constexpr uint32_t value() const { return value_; }

    constexpr DashboardGeneration next() const { return DashboardGeneration(value_ + 1); }

    constexpr bool operator==(const DashboardGeneration& other) const { return value_ == other.value_; }
    constexpr bool operator!=(const DashboardGeneration& other) const { return !(*this == other); }

private:
    uint32_t value_ = 0;
};

} // namespace app::core

#endif // APP_CPP_CORE_DASHBOARDGENERATION_H
