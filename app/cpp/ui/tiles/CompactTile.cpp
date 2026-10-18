#include "CompactTile.h"

#include "EntityTile.h"
#include "ValueTile.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"
#include "../../../ui_kit.h"

#include <cmath>

namespace app::ui {

namespace {
constexpr int32_t kPad = 12;

const char* sensor_icon(const std::string& device_class)
{
    if (device_class == "temperature") return LI_THERMOMETER;
    if (device_class == "humidity" || device_class == "moisture") return LI_DROPLETS;
    if (device_class == "pressure" || device_class == "atmospheric_pressure") return LI_GAUGE;
    if (device_class == "power" || device_class == "energy" || device_class == "current" ||
        device_class == "voltage" || device_class == "apparent_power" || device_class == "power_factor") {
        return LI_ZAP;
    }
    if (device_class == "battery") return LI_BATTERY;
    if (device_class == "illuminance") return LI_SUN_DIM;
    return LI_ACTIVITY;
}

bool value_domain(const std::string& d)
{
    return d == "sensor" || d == "number" || d == "input_number" || d == "counter";
}
} // namespace

CompactTile::Look CompactTile::look_for(const Facts& f)
{
    Look l;
    const std::string& d = f.domain;
    const std::string& s = f.state;
    if (!f.available) {
        l.icon = value_domain(d) ? sensor_icon(f.device_class) : LI_CIRCLE_DOT;
        if (d == "light") l.icon = LI_LIGHTBULB_OFF;
        if (d == "cover") l.icon = LI_BLINDS;
        l.look = UI_LOOK_OFF;
        l.state_text = tr(TR_STATE_UNAVAILABLE);
        return l;
    }
    if (value_domain(d)) {
        l.icon = sensor_icon(f.device_class);
        l.look = UI_LOOK_INFO;
        l.state_text = ValueTile::format_value(s) + (f.unit.empty() ? "" : " " + f.unit);
    } else if (d == "light") {
        const bool on = s == "on";
        l.icon = on ? LI_LIGHTBULB : LI_LIGHTBULB_OFF;
        l.look = on ? UI_LOOK_ON : UI_LOOK_OFF;
        l.state_text = tr(on ? TR_STATE_ON : TR_STATE_OFF);
        if (on && f.brightness) l.state_text = format_number(std::round(*f.brightness / 2.55), 0) + " %";
    } else if (d == "cover") {
        const bool open = s == "open" || s == "opening";
        l.icon = LI_BLINDS;
        l.look = open ? UI_LOOK_ON : UI_LOOK_OFF;
        l.state_text = tr(s == "opening" ? TR_STATE_OPENING : s == "closing" ? TR_STATE_CLOSING
                          : open ? TR_STATE_OPEN : TR_STATE_CLOSED);
        if (open && f.position) l.state_text += " · " + format_number(*f.position, 0) + " %";
    } else if (d == "climate") {
        l.icon = s == "heat" ? LI_FLAME : s == "cool" ? LI_SNOWFLAKE : LI_THERMOMETER;
        l.look = s == "heat" ? UI_LOOK_HEAT : s == "cool" ? UI_LOOK_COOL : UI_LOOK_OFF;
        if (f.current_temperature) l.state_text = format_number(*f.current_temperature, 1) + "°";
        if (f.temperature && s != "off") {
            l.state_text += (l.state_text.empty() ? "" : " · ") + format_number(*f.temperature, 1) + "°";
        }
        if (l.state_text.empty()) l.state_text = tr(s == "off" ? TR_STATE_OFF : TR_STATE_ON);
    } else if (d == "media_player") {
        const bool playing = s == "playing";
        l.icon = LI_SPEAKER;
        l.look = playing ? UI_LOOK_ON : UI_LOOK_OFF;
        l.state_text = tr(playing ? TR_MEDIA_PLAYING : s == "paused" ? TR_MEDIA_PAUSED
                          : s == "off" || s == "standby" ? TR_MEDIA_STANDBY : TR_MEDIA_NOTHING);
    } else {
        // the simple domains: as the centred entity tile shows them
        EntityTile::Facts e;
        e.domain = d;
        e.state = s;
        e.device_class = f.device_class;
        const EntityTile::Look el = EntityTile::look_for(e);
        l.icon = el.icon;
        l.look = el.look;
        l.state_text = el.state_text;
    }
    return l;
}

std::string CompactTile::tap_service(const std::string& d, const std::string& s)
{
    if (d == "light" || d == "switch" || d == "input_boolean" || d == "fan" || d == "automation") {
        return d == "automation" ? "toggle" : s == "on" ? "turn_off" : "turn_on";
    }
    if (d == "cover") return s == "open" || s == "opening" ? "close_cover" : "open_cover";
    if (d == "lock") return s == "locked" ? "" : "lock"; // unlocking needs a hold
    if (d == "button" || d == "input_button") return "press";
    if (d == "scene" || d == "script") return "turn_on";
    if (d == "media_player") return "media_play_pause";
    return "";
}

bool CompactTile::tap_opens_detail(const std::string& d)
{
    return d == "cover" || d == "climate" || d == "alarm_control_panel";
}

void CompactTile::build(lv_obj_t* tile)
{
    lv_obj_set_style_radius(tile, UI_R_CONTROL, 0);
    lv_obj_set_style_pad_all(tile, kPad, 0);
    lv_obj_set_style_pad_row(tile, 2, 0);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    icon_ = ui_label(tile, UI_ICON_BUTTON, UI_TEXT3, LI_CIRCLE_DOT);
    name_label_ = ui_label_line(tile, UI_FONT_LABEL, UI_TEXT, "");
    lv_obj_set_width(name_label_, LV_PCT(100));
    state_label_ = ui_label_line(tile, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_width(state_label_, LV_PCT(100));
}

void CompactTile::update()
{
    if (!icon_) return;
    Facts f;
    f.domain = domain();
    f.state = state();
    f.device_class = attr("device_class");
    f.unit = attr("unit_of_measurement");
    f.brightness = attr_number("brightness");
    f.position = attr_number("current_position");
    f.current_temperature = attr_number("current_temperature");
    f.temperature = attr_number("temperature");
    f.available = available() || !loaded();
    const Look l = look_for(f);
    lv_label_set_text(icon_, l.icon ? l.icon : LI_CIRCLE_DOT);
    lv_obj_set_style_text_color(icon_, lv_color_hex(l.look == UI_LOOK_OFF ? UI_TEXT3 : ui_look_fg(static_cast<ui_look_t>(l.look))), 0);
    lv_label_set_text(name_label_, name().c_str());
    lv_label_set_text(state_label_, loaded() ? l.state_text.c_str() : "");
}

void CompactTile::on_tap()
{
    if (!loaded() || !available()) return;
    if (tap_opens_detail(domain())) open_detail();
    else quick_action();
}

void CompactTile::on_hold()
{
    if (!loaded() || !available()) return;
    const std::string d = domain();
    if (d == "lock" && state() == "locked") {
        expect_state("unlocking");
        call("unlock");
        ui_toast(trf(TR_LOCK_UNLOCKED_TOAST, {name()}).c_str());
    } else if (tap_opens_detail(d)) {
        quick_action();
    } else {
        open_detail();
    }
}

void CompactTile::quick_action()
{
    const std::string d = domain();
    const std::string service = tap_service(d, state());
    if (service.empty()) {
        if (d == "lock") ui_toast(tr(TR_TILE_HOLD_TO_UNLOCK));
        return;
    }
    if (service == "turn_on" && d != "scene" && d != "script") expect_state("on");
    if (service == "turn_off") expect_state("off");
    if (service == "lock") {
        expect_state("locking");
        ui_toast(trf(TR_LOCK_LOCKING_TOAST, {name()}).c_str());
    }
    call(service);
}

void CompactTile::open_detail()
{
    if (!EntityDetail::exists_for(domain())) return;
    if (!detail_) detail_ = std::make_unique<EntityDetail>([this](const std::string& service, const std::string& json) {
        call(service, json);
    });
    detail_->open(domain(), entity(), name(), state());
}

void CompactTile::on_data(const DataUpdate& u)
{
    TileWidget::on_data(u);
    if (detail_ && u.entity_state) detail_->update(entity(), name(), state());
}

void CompactTile::shutdown_ui()
{
    detail_.reset(); // the page lives on the screen, not under the tile
    TileWidget::shutdown_ui();
}

} // namespace app::ui
