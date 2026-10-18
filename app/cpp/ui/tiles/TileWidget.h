#ifndef APP_CPP_UI_TILES_TILEWIDGET_H
#define APP_CPP_UI_TILES_TILEWIDGET_H

#include "../../core/CancellationToken.h"
#include "../../home_assistant/HaCommandService.h"
#include "../Widget.h"

#include <optional>
#include <string>

namespace app::ui {

// Base of the dashboard tiles of the design guide (docs/design/
// DESIGN_GUIDE.md, Widgets artboard): one entity, a 221 x 308 tile built
// from ui_kit, tap / hold / double tap. Subclasses build their content in
// build() and refresh it in update(); state() is the entity's last state
// (or the optimistic one while a command is under way).
class TileWidget : public Widget {
public:
    TileWidget(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService* commands,
               std::string entity_id, std::string name_override = {});

    std::vector<DataRequirement> requirements() const override;
    void create(lv_obj_t* parent) override;
    void on_data(const DataUpdate& update) override;
    void shutdown_ui() override;
    // Names starting with this (the section's title, e.g. the device name)
    // are shown without it: "Serverschrank Temperatur" under "Serverschrank".
    void set_name_prefix(std::string prefix) { name_prefix_ = std::move(prefix); }
    static std::string strip_prefix(const std::string& name, const std::string& prefix);

    static void tap_trampoline(void* self);
    static void hold_trampoline(void* self);
    static void double_tap_trampoline(void* self);

protected:
    virtual void build(lv_obj_t* tile) = 0;
    virtual void update() = 0;
    virtual void on_tap() {}
    virtual void on_hold() {}
    // Only tiles with a double tap action make a single tap wait for it.
    virtual bool has_double_tap() const { return false; }
    virtual void on_double_tap() {}
    // A command from call() failed (no update will confirm it).
    virtual void on_command_failed() {}

    const std::string& entity_id() const { return entity_id_; }
    std::string domain() const;
    std::string name() const;
    // The entity's state, or the optimistic one set by expect_state().
    const std::string& state() const { return optimistic_ ? *optimistic_ : entity_.state; }
    const app::home_assistant::EntityState& entity() const { return entity_; }
    bool loaded() const { return entity_.availability != app::home_assistant::Availability::loading; }
    bool available() const;
    std::string attr(const std::string& name) const;
    std::optional<double> attr_number(const std::string& name) const;
    // Seconds since the state last changed (std::nullopt without a time).
    std::optional<double> seconds_in_state() const;

    // Calls domain.service for this entity (extra_json: more service data).
    void call_domain(const std::string& domain, const std::string& service, const std::string& extra_json = {});
    void call(const std::string& service, const std::string& extra_json = {}) { call_domain(domain(), service, extra_json); }
    // Shows `state` until HA confirms (or the command fails).
    void expect_state(const std::string& state);

    lv_obj_t* tile() const { return tile_; }
    // For queries beyond the entity state (calendar events, todo items).
    app::home_assistant::HaCommandService* commands() const { return commands_; }

private:
    app::home_assistant::HaInstanceId instance_;
    app::home_assistant::HaCommandService* commands_;
    std::string entity_id_;
    std::string name_override_;
    std::string name_prefix_;
    app::home_assistant::EntityState entity_;
    std::optional<std::string> optimistic_;
    app::core::CancellationSource pending_;
    lv_obj_t* tile_ = nullptr;
};

// "12 Min.", "3 Std.", "2 Tage" in the UI language.
std::string format_duration(double seconds);
// 14.2 -> "14,2" with the UI language's decimal separator.
std::string format_number(double value, int decimals);

} // namespace app::ui

#endif // APP_CPP_UI_TILES_TILEWIDGET_H
