#include "EntityTile.h"

#include "lvgl/lvgl.h"
#include "../../../i18n.h"

#include "../../../ui_kit.h"

#include <cmath>
#include <cstdio>

namespace app::ui {

namespace {
constexpr uint32_t kUnlockHoldMs = 1000;

bool is_on(const std::string& s) { return s == "on" || s == "open" || s == "opening" || s == "home"; }

const char* alarm_mode(const std::string& s)
{
    if (s == "armed_away") return tr(TR_ALARM_ARMED_AWAY);
    if (s == "armed_home") return tr(TR_ALARM_ARMED_HOME);
    if (s == "armed_night") return tr(TR_ALARM_ARMED_NIGHT);
    if (s == "armed_vacation") return tr(TR_ALARM_ARMED_VACATION);
    if (s == "armed_custom_bypass") return tr(TR_ALARM_ARMED);
    if (s == "arming") return tr(TR_ALARM_ARMING);
    if (s == "pending") return tr(TR_ALARM_PENDING);
    if (s == "triggered") return tr(TR_ALARM_TRIGGERED);
    if (s == "disarming") return tr(TR_ALARM_DISARMING);
    return tr(TR_ALARM_DISARMED);
}

const char* vacuum_state(const std::string& s)
{
    if (s == "cleaning") return tr(TR_VACUUM_CLEANING);
    if (s == "docked") return tr(TR_VACUUM_DOCKED);
    if (s == "returning") return tr(TR_VACUUM_RETURNING);
    if (s == "paused") return tr(TR_STATE_PAUSED);
    if (s == "error") return tr(TR_STATE_ERROR);
    if (s == "mowing") return tr(TR_VACUUM_MOWING);
    return tr(TR_STATE_READY);
}

void raw_button_cb(lv_event_t* e)
{
    auto* self = static_cast<EntityTile*>(lv_event_get_user_data(e));
    lv_obj_t* b = lv_event_get_current_target_obj(e);
    self->button_pressed(static_cast<int>(lv_obj_get_index(b)));
}

void raw_hold_anim_ready(lv_anim_t* a) { static_cast<EntityTile*>(lv_anim_get_user_data(a))->hold_completed(); }

void raw_hold_anim_exec(void* obj, int32_t v)
{
    auto* fill = static_cast<lv_obj_t*>(obj);
    lv_obj_set_height(fill, v);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_LEFT, -UI_TILE_PAD, UI_TILE_PAD); // over the tile's padding too
}

void raw_release_cb(lv_event_t* e) { static_cast<EntityTile*>(lv_event_get_user_data(e))->hold_progress_stop(); }

void raw_press_cb(lv_event_t* e) { static_cast<EntityTile*>(lv_event_get_user_data(e))->hold_progress_start(); }
} // namespace

EntityTile::Look EntityTile::look_for(const Facts& f)
{
    Look l;
    const std::string& d = f.domain;
    const std::string& s = f.state;
    const bool on = is_on(s);
    l.look = on ? UI_LOOK_ON : UI_LOOK_OFF;
    l.state_text = tr(on ? TR_STATE_ON : TR_STATE_OFF);

    if (d == "switch") {
        l.icon = f.device_class == "outlet" ? LI_PLUG : LI_TOGGLE_RIGHT;
    } else if (d == "input_boolean") {
        l.icon = on ? LI_TOGGLE_RIGHT : LI_TOGGLE_LEFT;
    } else if (d == "fan") {
        l.icon = LI_FAN;
        if (on && f.percentage && f.fan_steps > 1) {
            const int step = static_cast<int>(std::lround(*f.percentage / (100.0 / f.fan_steps)));
            l.state_text = trf(TR_FAN_STEP, {std::to_string(step < 1 ? 1 : step), std::to_string(f.fan_steps)});
        } else if (on && f.percentage) {
            l.state_text = format_number(*f.percentage, 0) + " %";
        }
    } else if (d == "lock") {
        const bool locked = s == "locked";
        l.icon = locked ? LI_LOCK : LI_LOCK_OPEN;
        l.look = locked ? UI_LOOK_INFO : UI_LOOK_WARN;
        l.state_text = tr(locked ? TR_LOCK_LOCKED
                          : s == "unlocking" ? TR_LOCK_UNLOCKING
                          : s == "locking" ? TR_LOCK_LOCKING
                          : s == "jammed" ? TR_LOCK_JAMMED
                                          : TR_LOCK_OPEN_TAP);
        l.warn_text = !locked;
    } else if (d == "button" || d == "input_button") {
        l.icon = LI_CIRCLE_DOT;
        l.look = UI_LOOK_OFF;
        l.state_text = tr(TR_TILE_TAP_TO_TRIGGER);
    } else if (d == "script") {
        l.icon = LI_CODE;
        l.state_text = tr(on ? TR_STATE_RUNNING : TR_TILE_TAP_TO_START);
        if (on && f.seconds_in_state) l.state_text += " · " + format_duration(*f.seconds_in_state);
    } else if (d == "scene") {
        l.icon = LI_SPARKLES;
        l.look = UI_LOOK_OFF;
        l.state_text = tr(TR_TILE_TAP_TO_ACTIVATE);
    } else if (d == "binary_sensor") {
        const std::string& c = f.device_class;
        if (c == "door" || c == "garage_door" || c == "window" || c == "opening") {
            l.icon = on ? LI_DOOR_OPEN : LI_DOOR_CLOSED;
            l.look = on ? UI_LOOK_WARN : UI_LOOK_OFF;
            l.warn_text = on;
            l.state_text = tr(on ? TR_STATE_OPEN : TR_STATE_CLOSED);
            if (on && f.seconds_in_state) l.state_text += " · " + format_duration(*f.seconds_in_state);
        } else if (c == "motion" || c == "occupancy" || c == "presence") {
            l.icon = on ? LI_ACTIVITY : LI_USER;
            l.state_text = tr(on ? TR_SENSOR_DETECTED : TR_SENSOR_CLEAR);
        } else if (c == "moisture" || c == "smoke" || c == "gas" || c == "problem" || c == "safety") {
            l.icon = LI_TRIANGLE_ALERT;
            l.look = on ? UI_LOOK_WARN : UI_LOOK_OFF;
            l.warn_text = on;
            l.state_text = tr(on ? TR_SENSOR_ALARM : TR_SENSOR_OK);
        } else {
            l.icon = LI_CIRCLE_DOT;
        }
    } else if (d == "person" || d == "device_tracker") {
        l.icon = LI_USER_ROUND;
        l.look = UI_LOOK_INFO;
        l.state_text = s == "home" ? tr(TR_PERSON_HOME) : s == "not_home" ? tr(TR_PERSON_AWAY) : s;
    } else if (d == "alarm_control_panel") {
        const bool armed = s.rfind("armed", 0) == 0 || s == "triggered" || s == "pending" || s == "arming";
        l.icon = armed ? LI_SHIELD_ALERT : LI_SHIELD_CHECK;
        l.look = armed ? UI_LOOK_WARN : UI_LOOK_OFF;
        l.warn_text = armed;
        l.state_text = alarm_mode(s);
    } else if (d == "siren") {
        l.icon = LI_SIREN;
        l.look = on ? UI_LOOK_WARN : UI_LOOK_OFF;
        l.warn_text = on;
        l.state_text = tr(on ? TR_SIREN_SOUNDING : TR_STATE_OFF);
    } else if (d == "valve") {
        l.icon = LI_DROPLETS;
        l.state_text = tr(s == "open" ? TR_STATE_OPEN : s == "opening" ? TR_STATE_OPENING
                          : s == "closing" ? TR_STATE_CLOSING : TR_STATE_CLOSED);
    } else if (d == "vacuum" || d == "lawn_mower") {
        l.icon = d == "vacuum" ? LI_BOT : LI_LEAF;
        const bool busy = s == "cleaning" || s == "mowing" || s == "returning";
        l.look = busy ? UI_LOOK_ON : UI_LOOK_OFF;
        l.state_text = vacuum_state(s);
    } else if (d == "automation") {
        l.icon = LI_ZAP;
        l.state_text = tr(on ? TR_STATE_ACTIVE : TR_STATE_INACTIVE);
    } else if (d == "remote") {
        l.icon = LI_TV;
    } else if (d == "todo") {
        // state: number of open items
        l.icon = LI_LIST_CHECKS;
        l.look = s != "0" ? UI_LOOK_INFO : UI_LOOK_OFF;
        l.state_text = trf(TR_TODO_OPEN, {s});
    } else if (d == "calendar") {
        // on while an event runs; message: the current / next event
        l.icon = LI_CALENDAR;
        l.state_text = f.message.empty() ? std::string(tr(TR_CALENDAR_NOTHING)) : f.message;
    } else if (d == "camera") {
        l.icon = LI_CAMERA;
        l.look = UI_LOOK_INFO;
        l.state_text = tr(s == "recording" ? TR_CAMERA_RECORDING : s == "streaming" ? TR_CAMERA_STREAMING : TR_CAMERA_IDLE);
    } else {
        l.icon = LI_CIRCLE_HELP;
        l.look = UI_LOOK_INFO;
        l.state_text = s;
    }

    if (!f.available) {
        l.look = UI_LOOK_OFF;
        l.state_text = tr(TR_STATE_UNAVAILABLE);
        l.warn_text = false;
    }
    return l;
}

EntityTile::Facts EntityTile::facts() const
{
    Facts f;
    f.domain = domain();
    f.state = state();
    f.device_class = attr("device_class");
    f.message = attr("message");
    f.seconds_in_state = seconds_in_state();
    f.percentage = attr_number("percentage");
    if (auto step = attr_number("percentage_step"); step && *step > 0) {
        f.fan_steps = static_cast<int>(std::lround(100.0 / *step));
    }
    f.available = available() || !loaded();
    return f;
}

EntityTile::Buttons EntityTile::buttons_kind() const
{
    const std::string d = domain();
    if (d == "fan") return Buttons::MinusPlus;
    if (d == "script") return Buttons::Stop;
    if (d == "valve") return Buttons::OpenClose;
    if (d == "vacuum" || d == "lawn_mower") return Buttons::StartDock;
    return Buttons::None;
}

void EntityTile::build(lv_obj_t* tile)
{
    const Buttons kind = buttons_kind();
    const bool with_buttons = kind != Buttons::None;

    if (domain() == "lock") {
        // Holding unlocks: a sand fill rises from the bottom while held.
        progress_ = ui_box(tile);
        lv_obj_add_flag(progress_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(progress_, LV_PCT(100), 0);
        lv_obj_set_style_bg_color(progress_, lv_color_hex(UI_ACCENT_BG), 0);
        lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, 0);
        lv_obj_set_style_clip_corner(tile, true, 0); // the fill keeps the tile's round corners
        lv_obj_add_event_cb(tile, raw_press_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(tile, raw_release_cb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(tile, raw_release_cb, LV_EVENT_PRESS_LOST, this);
    }

    lv_obj_t* center = ui_box(tile);
    lv_obj_set_width(center, LV_PCT(100));
    lv_obj_set_flex_grow(center, 1);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center, with_buttons ? 14 : 20, 0);

    bubble_ = ui_bubble(center, with_buttons ? UI_BUBBLE_SMALL : UI_BUBBLE, with_buttons ? UI_ICON_VALUE : UI_ICON_TILE,
                        LI_CIRCLE_DOT, UI_LOOK_OFF);
    lv_obj_t* texts = ui_box(center);
    lv_obj_set_size(texts, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(texts, 2, 0);
    name_label_ = ui_label_line(texts, UI_FONT_NAME, UI_TEXT, "");
    lv_obj_set_style_text_align(name_label_, LV_TEXT_ALIGN_CENTER, 0);
    state_label_ = ui_label_line(texts, UI_FONT_LABEL, UI_TEXT2, "");
    lv_obj_set_style_text_align(state_label_, LV_TEXT_ALIGN_CENTER, 0);

    if (with_buttons) {
        lv_obj_t* row = ui_button_row(tile);
        const int count = kind == Buttons::Stop ? 1 : 2;
        for (int i = 0; i < count; ++i) {
            buttons_[i] = ui_tile_button(row, nullptr, nullptr, false);
            lv_obj_add_event_cb(buttons_[i], raw_button_cb, LV_EVENT_CLICKED, this);
        }
    }
}

void EntityTile::update()
{
    if (!bubble_) return;
    const Look l = look_for(facts());
    ui_bubble_set(bubble_, l.icon, static_cast<ui_look_t>(l.look), 0);
    lv_label_set_text(name_label_, name().c_str());
    lv_label_set_text(state_label_, loaded() ? l.state_text.c_str() : "");
    lv_obj_set_style_text_color(state_label_, lv_color_hex(l.warn_text ? UI_WARN : UI_TEXT2), 0);

    const std::string& s = state();
    switch (buttons_kind()) {
    case Buttons::MinusPlus:
        ui_tile_button_set(buttons_[0], LI_MINUS, nullptr, false);
        ui_tile_button_set(buttons_[1], LI_PLUS, nullptr, false);
        break;
    case Buttons::Stop:
        // a running script gets a stop button; otherwise the row is hidden
        ui_tile_button_set(buttons_[0], LI_SQUARE, tr(TR_BUTTON_STOP), false);
        if (s == "on") lv_obj_remove_flag(lv_obj_get_parent(buttons_[0]), LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(lv_obj_get_parent(buttons_[0]), LV_OBJ_FLAG_HIDDEN);
        break;
    case Buttons::OpenClose:
        ui_tile_button_set(buttons_[0], nullptr, tr(TR_BUTTON_OPEN), s == "open");
        ui_tile_button_set(buttons_[1], nullptr, tr(TR_BUTTON_CLOSE), s == "closed");
        break;
    case Buttons::StartDock:
        ui_tile_button_set(buttons_[0], nullptr, tr(s == "cleaning" || s == "mowing" ? TR_BUTTON_PAUSE : TR_BUTTON_START),
                           false);
        ui_tile_button_set(buttons_[1], nullptr, tr(TR_BUTTON_DOCK), false);
        break;
    case Buttons::None: break;
    }
}

AlarmDetail::State EntityTile::alarm_state() const { return alarm_detail_state(entity(), name(), state()); }

void EntityTile::on_data(const DataUpdate& u)
{
    TileWidget::on_data(u);
    if (alarm_ && alarm_->is_open() && u.entity_state) alarm_->update(alarm_state());
}

void EntityTile::shutdown_ui()
{
    alarm_.reset(); // the page lives on the screen, not under the tile
    TileWidget::shutdown_ui();
}

void EntityTile::on_tap()
{
    if (!loaded() || !available()) return;
    const std::string d = domain();
    const std::string& s = state();
    if (d == "alarm_control_panel") {
        if (!alarm_) alarm_ = std::make_unique<AlarmDetail>([this](const std::string& service, const std::string& json) {
            call(service, json);
        });
        alarm_->open(alarm_state());
        return;
    }
    if (d == "switch" || d == "input_boolean" || d == "fan" || d == "automation" || d == "remote") {
        expect_state(s == "on" ? "off" : "on");
        call(d == "automation" ? "toggle" : s == "on" ? "turn_off" : "turn_on");
    } else if (d == "lock") {
        if (s != "locked") {
            expect_state("locking");
            call("lock");
            ui_toast(trf(TR_LOCK_LOCKING_TOAST, {name()}).c_str());
        } else {
            ui_toast(tr(TR_TILE_HOLD_TO_UNLOCK));
        }
    } else if (d == "button" || d == "input_button") {
        call("press");
        ui_toast(trf(TR_TILE_TRIGGERED, {name()}).c_str());
    } else if (d == "script") {
        if (s != "on") {
            expect_state("on");
            call("turn_on");
        }
    } else if (d == "scene") {
        call("turn_on");
        ui_toast(trf(TR_TILE_SCENE, {name()}).c_str());
    } else if (d == "siren") {
        if (s == "on") {
            expect_state("off");
            call("turn_off");
        } else {
            ui_toast(tr(TR_TILE_HOLD_TO_SWITCH_ON));
        }
    } else if (d == "valve") {
        call(s == "open" ? "close_valve" : "open_valve");
    } else if (d == "vacuum" || d == "lawn_mower") {
        const bool busy = s == "cleaning" || s == "mowing";
        call(busy ? "pause" : d == "vacuum" ? "start" : "start_mowing");
    }
}

void EntityTile::on_hold()
{
    if (!loaded() || !available()) return;
    if (domain() == "siren" && state() != "on") {
        expect_state("on");
        call("turn_on");
    }
}

void EntityTile::button_pressed(int index)
{
    const std::string d = domain();
    switch (buttons_kind()) {
    case Buttons::MinusPlus: call(index == 0 ? "decrease_speed" : "increase_speed"); break;
    case Buttons::Stop: call("turn_off"); break;
    case Buttons::OpenClose: call(index == 0 ? "open_valve" : "close_valve"); break;
    case Buttons::StartDock:
        if (index == 1) call("return_to_base");
        else on_tap();
        break;
    case Buttons::None: break;
    }
}


void EntityTile::hold_progress_start()
{
    if (!progress_ || state() != "locked" || !available()) return;
    lv_obj_t* t = tile();
    lv_obj_set_width(progress_, lv_obj_get_width(t));
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, progress_);
    lv_anim_set_exec_cb(&a, raw_hold_anim_exec);
    lv_anim_set_values(&a, 0, lv_obj_get_height(t));
    lv_anim_set_duration(&a, kUnlockHoldMs);
    lv_anim_set_user_data(&a, this);
    lv_anim_set_completed_cb(&a, raw_hold_anim_ready);
    lv_anim_start(&a);
    lv_label_set_text(state_label_, tr(TR_TILE_KEEP_HOLDING));
}

void EntityTile::hold_progress_stop()
{
    if (!progress_ || !lv_anim_get(progress_, raw_hold_anim_exec)) return;
    lv_anim_delete(progress_, raw_hold_anim_exec);
    lv_obj_set_height(progress_, 0);
    update();
}

void EntityTile::hold_completed()
{
    lv_obj_set_height(progress_, 0);
    expect_state("unlocking");
    call("unlock");
    ui_toast(trf(TR_LOCK_UNLOCKED_TOAST, {name()}).c_str());
}

} // namespace app::ui
