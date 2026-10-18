#include "TileWidget.h"

#include "../AttributeText.h"
#include "lvgl/lvgl.h"

#include "../../../haptic/haptic.h"
#include "../../../i18n.h"
#include "../../../ui_kit.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace app::ui {

using app::home_assistant::Availability;
using app::home_assistant::ResultMessage;

TileWidget::TileWidget(app::home_assistant::HaInstanceId instance, app::home_assistant::HaCommandService* commands,
                       std::string entity_id, std::string name_override)
    : instance_(std::move(instance)), commands_(commands), entity_id_(std::move(entity_id)),
      name_override_(std::move(name_override))
{
    entity_.entity_id = entity_id_;
}

std::vector<DataRequirement> TileWidget::requirements() const
{
    if (entity_id_.empty()) return {};
    return {EntityRequirement{instance_, entity_id_}};
}

void TileWidget::create(lv_obj_t* parent)
{
    tile_ = ui_tile(parent);
    ui_gestures(tile_, tap_trampoline, hold_trampoline, has_double_tap() ? double_tap_trampoline : nullptr, this);
    haptic_add_click(tile_, HAPTIC_BUTTON_PRESS);
    build(tile_);
    track_root(tile_);
    update();
}

void TileWidget::on_data(const DataUpdate& u)
{
    if (!u.entity_state) return;
    entity_ = *u.entity_state;
    optimistic_.reset();
    if (tile_) {
        lv_obj_set_style_opa(tile_, available() ? LV_OPA_COVER : LV_OPA_60, 0);
        update();
    }
}

void TileWidget::shutdown_ui() { pending_.cancel(); }

void TileWidget::tap_trampoline(void* self) { static_cast<TileWidget*>(self)->on_tap(); }
void TileWidget::hold_trampoline(void* self) { static_cast<TileWidget*>(self)->on_hold(); }
void TileWidget::double_tap_trampoline(void* self) { static_cast<TileWidget*>(self)->on_double_tap(); }

std::string TileWidget::domain() const
{
    const size_t dot = entity_id_.find('.');
    return dot == std::string::npos ? "" : entity_id_.substr(0, dot);
}

std::string TileWidget::name() const
{
    if (!name_override_.empty()) return name_override_;
    std::string friendly = find_attr(entity_.attributes, "friendly_name");
    return friendly.empty() ? entity_id_ : strip_prefix(friendly, name_prefix_);
}

std::string TileWidget::strip_prefix(const std::string& name, const std::string& prefix)
{
    if (prefix.empty() || name.size() <= prefix.size() + 1) return name;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return name;
    }
    size_t start = prefix.size();
    while (start < name.size() && (name[start] == ' ' || name[start] == '-' || name[start] == ':')) ++start;
    if (start == prefix.size() || start >= name.size()) return name; // no separator: a different word
    std::string rest = name.substr(start);
    rest[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(rest[0])));
    return rest;
}

bool TileWidget::available() const
{
    return entity_.availability != Availability::unavailable && entity_.state != "unavailable";
}

std::string TileWidget::attr(const std::string& name) const { return find_attr(entity_.attributes, name); }

std::optional<double> TileWidget::attr_number(const std::string& name) const
{
    return find_attr_double(entity_.attributes, name);
}

std::optional<double> TileWidget::seconds_in_state() const
{
    if (!entity_.last_changed_epoch_s) return std::nullopt;
    const double now = static_cast<double>(std::time(nullptr));
    return now > *entity_.last_changed_epoch_s ? now - *entity_.last_changed_epoch_s : 0.0;
}

void TileWidget::call_domain(const std::string& domain, const std::string& service, const std::string& extra_json)
{
    if (!commands_) return;
    pending_.cancel();
    pending_ = commands_->call_service(domain, service, entity_id_, extra_json, [this](const ResultMessage& r) {
        if (r.success) return;
        optimistic_.reset(); // nothing will confirm the guess
        on_command_failed();
        update();
    });
}

void TileWidget::expect_state(const std::string& state)
{
    optimistic_ = state;
    update();
}

std::string format_duration(double s)
{
    if (s < 60) return tr(TR_TIME_JUST_NOW);
    if (s < 3600) return trf(TR_TIME_MINUTES, {std::to_string(static_cast<int>(s / 60))});
    if (s < 86400) return trf(TR_TIME_HOURS, {std::to_string(static_cast<int>(s / 3600))});
    const int d = static_cast<int>(s / 86400);
    return trf(d == 1 ? TR_TIME_DAY : TR_TIME_DAYS, {std::to_string(d)});
}

std::string format_number(double value, int decimals)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    std::string out = buf;
    const char sep = i18n_decimal();
    for (char& c : out) {
        if (c == '.') c = sep;
    }
    return out == "-0" ? "0" : out;
}

} // namespace app::ui
