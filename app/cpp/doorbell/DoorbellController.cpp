#include "DoorbellController.h"

#include "../core/IsoTime.h"

#include <cmath>
#include <ctime>

namespace app::doorbell {

namespace {
constexpr double kMaxRingAgeS = 60;

std::string domain_of(const std::string& entity_id)
{
    const size_t dot = entity_id.find('.');
    return dot == std::string::npos ? std::string() : entity_id.substr(0, dot);
}
} // namespace

bool DoorbellController::is_ring(const std::string& domain, const std::optional<std::string>& previous,
                                 const std::string& next, double now_epoch)
{
    if (!previous || *previous == next) return false;
    if (next == "unavailable" || next == "unknown" || next.empty()) return false;
    if (domain == "event" || domain == "input_button" || domain == "button") {
        // the state is the time of the last press
        const auto when = app::core::parse_iso_time(next);
        return when && std::fabs(now_epoch - *when) <= kMaxRingAgeS;
    }
    // binary_sensor, input_boolean, switch, ...: off -> on
    return next == "on" && *previous == "off";
}

std::string DoorbellController::opener_service(const std::string& opener_entity)
{
    const std::string d = domain_of(opener_entity);
    if (d == "lock") return "lock.unlock";
    if (d == "button" || d == "input_button") return d + ".press";
    if (d == "script" || d == "switch" || d == "scene" || d == "input_boolean") return d + ".turn_on";
    if (d == "cover") return "cover.open_cover";
    return {};
}

void DoorbellController::attach(app::home_assistant::EntitySubscriptionCoordinator* coordinator,
                                app::home_assistant::HaCommandService* commands, Settings settings, RingFn on_ring)
{
    handle_.reset();
    last_state_.reset();
    settings_ = std::move(settings);
    on_ring_ = std::move(on_ring);
    commands_ = commands;
    if (!coordinator || settings_.trigger.empty()) return;
    handle_ = coordinator->observe(settings_.trigger,
                                   [this](const app::home_assistant::EntityState& s) { on_state(s); });
    coordinator->deliver_current(settings_.trigger, handle_);
}

void DoorbellController::on_state(const app::home_assistant::EntityState& s)
{
    using app::home_assistant::Availability;
    if (s.availability != Availability::available && s.availability != Availability::unknown) {
        // loading, stale, gone: the next state is a fresh start, not a press
        // ("unknown" is a real state: a button never pressed so far)
        last_state_.reset();
        return;
    }
    const bool ring =
        is_ring(domain_of(settings_.trigger), last_state_, s.state, static_cast<double>(std::time(nullptr)));
    last_state_ = s.state;
    if (ring && on_ring_) on_ring_();
}

void DoorbellController::open(std::function<void(bool ok)> done)
{
    const std::string service = opener_service(settings_.opener);
    if (!commands_ || service.empty()) {
        if (done) done(false);
        return;
    }
    const size_t dot = service.find('.');
    pending_.cancel();
    pending_ = commands_->call_service(service.substr(0, dot), service.substr(dot + 1), settings_.opener, {},
                                       [done](const app::home_assistant::ResultMessage& r) {
                                           if (done) done(r.success);
                                       });
}

} // namespace app::doorbell
