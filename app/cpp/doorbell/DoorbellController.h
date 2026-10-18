#ifndef APP_CPP_DOORBELL_DOORBELLCONTROLLER_H
#define APP_CPP_DOORBELL_DOORBELLCONTROLLER_H

#include "../core/CancellationToken.h"
#include "../home_assistant/EntitySubscriptionCoordinator.h"
#include "../home_assistant/HaCommandService.h"

#include <functional>
#include <optional>
#include <string>

namespace app::doorbell {

// The doorbell (CPP_MIGRATION_PLAN.md §16): watches the configured trigger
// entity through the dashboard's entity subscriptions and reports a ring;
// opens the door through the configured opener entity. UI thread.
//
// Settings (config.ini): doorbell_trigger (event, input_button, button —
// a new timestamp rings; binary_sensor, input_boolean, switch — off -> on
// rings), doorbell_camera (camera.*), doorbell_unlock (lock, button,
// input_button, script, switch, scene, cover).
class DoorbellController {
public:
    struct Settings {
        std::string trigger, camera, opener;
    };
    using RingFn = std::function<void()>;

    // Whether a state change of the trigger is a ring (pure; host-tested).
    // previous: the last state seen since (re)subscribing, nullopt for the
    // first delivery — that one never rings (it is the current state, not a
    // press). Timestamps older than 60 s do not ring either (a press while
    // the panel was offline).
    static bool is_ring(const std::string& domain, const std::optional<std::string>& previous, const std::string& next,
                        double now_epoch);
    // domain.service that opens the door for an opener entity ("" if none).
    static std::string opener_service(const std::string& opener_entity);

    // Re-attaches with new settings (also the first time). Null coordinator
    // or commands: detached (not connected yet).
    void attach(app::home_assistant::EntitySubscriptionCoordinator* coordinator,
                app::home_assistant::HaCommandService* commands, Settings settings, RingFn on_ring);

    // Calls the opener; done(ok) once HA answered.
    void open(std::function<void(bool ok)> done);

    const Settings& settings() const { return settings_; }

private:
    void on_state(const app::home_assistant::EntityState& state);

    Settings settings_;
    RingFn on_ring_;
    app::home_assistant::HaCommandService* commands_ = nullptr;
    app::home_assistant::EntitySubscriptionCoordinator::SubscriptionHandle handle_;
    std::optional<std::string> last_state_;
    app::core::CancellationSource pending_;
};

} // namespace app::doorbell

#endif // APP_CPP_DOORBELL_DOORBELLCONTROLLER_H
