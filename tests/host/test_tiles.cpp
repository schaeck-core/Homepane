// What the dashboard tiles of the design guide show for an entity: the
// pure parts of the tile classes (texts, colours, icons, positions), no
// LVGL objects.

#include "app/i18n.h"
#include "lvgl/lvgl.h"

#include "app/cpp/ui/tiles/ChartTile.h"
#include "app/cpp/ui/tiles/ClimateTile.h"
#include "app/cpp/ui/tiles/CoverTile.h"
#include "app/cpp/ui/tiles/CompactTile.h"
#include "app/cpp/ui/tiles/EntityTile.h"
#include "app/cpp/ui/tiles/MediaTile.h"
#include "app/cpp/ui/tiles/OptionsTile.h"
#include "app/cpp/ui/tiles/TileWidget.h"
#include "app/cpp/ui/tiles/ValueTile.h"
#include "app/cpp/ui/details/AlarmDetail.h"
#include "app/cpp/ui/tiles/CalendarTile.h"
#include "app/cpp/ui/tiles/TodoTile.h"
#include "app/cpp/media/ImageLoader.h"
#include "app/cpp/doorbell/DoorbellController.h"
#include "app/cpp/eventcalendar/EventFeed.h"
#include "app/cpp/ui/Screensaver.h"
#include "app/cpp/core/DateText.h"

#include <cjson/cJSON.h>
#include <fstream>
#include <sstream>
#include "app/cpp/media/ImageDecoder.h"

#include <ctime>
#include "app/ui_kit.h"

#include <cstdio>
#include <string>

using namespace app::ui;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description.c_str());
        ++g_failures;
    } else {
        std::fprintf(stdout, "OK:   %s\n", description.c_str());
    }
}

EntityTile::Look look(const std::string& domain, const std::string& state, const std::string& device_class = "")
{
    EntityTile::Facts f;
    f.domain = domain;
    f.state = state;
    f.device_class = device_class;
    return EntityTile::look_for(f);
}

void test_entity_tile_looks()
{
    auto sw = look("switch", "on", "outlet");
    expect(sw.icon == std::string(LI_PLUG) && sw.look == UI_LOOK_ON && sw.state_text == "An", "an outlet that is on");
    expect(look("switch", "off").look == UI_LOOK_OFF && look("switch", "off").state_text == "Aus", "a switch that is off");

    EntityTile::Facts fan;
    fan.domain = "fan";
    fan.state = "on";
    fan.percentage = 67;
    fan.fan_steps = 3;
    expect(EntityTile::look_for(fan).state_text == "Stufe 2 von 3", "fan speed as a step of the fan's steps");

    auto locked = look("lock", "locked");
    auto open = look("lock", "unlocked");
    expect(locked.look == UI_LOOK_INFO && locked.state_text == "Verriegelt", "a locked door: neutral");
    expect(open.look == UI_LOOK_WARN && open.warn_text, "an unlocked door: warning colours");

    EntityTile::Facts door;
    door.domain = "binary_sensor";
    door.state = "on";
    door.device_class = "door";
    door.seconds_in_state = 12 * 60 + 5;
    auto d = EntityTile::look_for(door);
    expect(d.state_text == "Offen · 12 Min." && d.look == UI_LOOK_WARN, "an open door says how long it is open");

    expect(look("person", "not_home").state_text == "Unterwegs" && look("person", "Arbeit").state_text == "Arbeit",
           "a person: home / away / the zone's name");
    expect(look("alarm_control_panel", "armed_away").state_text == "Scharf · Abwesend" &&
               look("alarm_control_panel", "armed_away").look == UI_LOOK_WARN,
           "an armed alarm panel");
    expect(look("alarm_control_panel", "disarmed").look == UI_LOOK_OFF, "a disarmed alarm panel is calm");

    EntityTile::Facts gone;
    gone.domain = "switch";
    gone.state = "unavailable";
    gone.available = false;
    expect(EntityTile::look_for(gone).state_text == "Nicht verfügbar", "unavailable entities say so");
}

ValueTile::Text value(const std::string& domain, const std::string& state, const std::string& unit = "",
                      const std::string& device_class = "")
{
    ValueTile::Facts f;
    f.domain = domain;
    f.state = state;
    f.unit = unit;
    f.device_class = device_class;
    return ValueTile::text_for(f);
}

void test_value_tile_texts()
{
    expect(ValueTile::format_value("21.456") == "21,5" && ValueTile::format_value("3.456") == "3,46" &&
               ValueTile::format_value("14.2") == "14,2",
           "German decimals, at most two below 10, else one");
    expect(ValueTile::format_value("1240") == "1240" && ValueTile::format_value("45.0") == "45",
           "whole numbers without a trailing ,0");
    expect(ValueTile::format_value("Normal") == "Normal", "text states stay as they are");
    auto co2 = value("sensor", "640", "ppm", "carbon_dioxide");
    expect(co2.value == "640" && co2.unit == "ppm" && co2.status == "Gut" && co2.status_color == UI_OK,
           "CO2 is rated as a word, good in the OK colour");
    expect(value("sensor", "1300", "ppm", "carbon_dioxide").status == "Lüften", "high CO2: time to air");
    ValueTile::Facts upd;
    upd.domain = "update";
    upd.state = "on";
    upd.latest_version = "1.4.0";
    upd.installed_version = "1.3.2";
    auto u = ValueTile::text_for(upd);
    expect(u.value == "1.4.0" && u.status == "Update verfügbar" && u.status_color == UI_ACCENT,
           "a pending update shows the new version in sand");
    ValueTile::Facts gone;
    gone.domain = "sensor";
    gone.available = false;
    expect(ValueTile::text_for(gone).value == "–", "unavailable: a dash, no link blue");
    expect(std::string(weather_text_de("partlycloudy")) == "Teils bewölkt" &&
               std::string(weather_icon("rainy")) == LI_CLOUD_RAIN,
           "weather conditions in German with a line icon");
}

void test_more_value_domains()
{
    const auto t0 = ValueTile::parse_time("2026-10-08T05:30:00+00:00");
    const auto t1 = ValueTile::parse_time("2026-10-08T07:30:00+02:00");
    const auto tz = ValueTile::parse_time("2026-10-08T05:30:00.123456Z");
    expect(t0 && t1 && tz && *t0 == *t1 && *t0 == *tz && static_cast<long>(*t0) == 1791437400L,
           "ISO times with offset, Z and fractions give the same epoch");

    ValueTile::Facts timer;
    timer.domain = "timer";
    timer.state = "active";
    timer.finishes_at = "2026-10-08T05:34:12+00:00";
    timer.now = *t0;
    auto tt = ValueTile::text_for(timer);
    expect(tt.value == "4:12" && tt.status == "Läuft" && tt.status_color == UI_ACCENT,
           "a running timer counts down to finishes_at");
    timer.state = "paused";
    timer.remaining = "0:02:05";
    expect(ValueTile::text_for(timer).value == "2:05", "a paused timer shows what remains");
    timer.state = "idle";
    timer.duration = "1:00:00";
    expect(ValueTile::text_for(timer).value == "1:00:00" && ValueTile::text_for(timer).status == "Bereit",
           "an idle timer shows its duration");

    expect(value("date", "2026-10-08").value == "8. Okt.", "a date helper reads as day and month");
    expect(value("time", "07:30:00").value == "07:30", "a time helper without seconds");
    ValueTile::Facts dt;
    dt.domain = "input_datetime";
    dt.state = "2026-12-24 18:00:00";
    dt.has_date = "true";
    dt.has_time = "true";
    auto d = ValueTile::text_for(dt);
    expect(d.value == "18:00" && d.status == "24. Dez.", "input_datetime: time big, date below");

    ValueTile::Facts ev;
    ev.domain = "event";
    ev.state = "2026-10-08T05:20:00+00:00";
    ev.event_type = "pressed";
    ev.now = *t0;
    auto e = ValueTile::text_for(ev);
    expect(e.value == "pressed" && e.status == "vor 10 Min.", "an event shows its type and how long ago");

    ValueTile::Facts sun;
    sun.domain = "sun";
    sun.state = "above_horizon";
    sun.next_setting = "2026-10-08T16:45:00+00:00";
    auto su = ValueTile::text_for(sun);
    expect(su.status == "Sonnenuntergang" && std::string(su.icon) == LI_SUN && su.value.size() == 5,
           "sun up: the next sunset time");

    EntityTile::Facts todo;
    todo.domain = "todo";
    todo.state = "3";
    expect(EntityTile::look_for(todo).state_text == "3 offen", "a todo list shows its open items");
    EntityTile::Facts cal;
    cal.domain = "calendar";
    cal.state = "off";
    cal.message = "Müllabfuhr";
    expect(EntityTile::look_for(cal).state_text == "Müllabfuhr", "a calendar shows its next event");
}

void test_alarm_keypad_logic()
{
    AlarmDetail::State s;
    s.state = "disarmed";
    s.supported_features = 1 | 2 | 32;
    auto modes = AlarmDetail::modes_for(s);
    expect(modes.size() == 3 && modes[0].service == "alarm_arm_home" && modes[2].service == "alarm_arm_vacation",
           "disarmed: the arm modes HA supports");
    s.state = "armed_night";
    modes = AlarmDetail::modes_for(s);
    expect(modes.size() == 1 && modes[0].service == "alarm_disarm", "armed: only disarm");
    s.code_needed = true;
    expect(AlarmDetail::service_json(s, "alarm_disarm", "1234") == "{\"code\":\"1234\"}",
           "a code goes along when HA asks for one");
    s.code_arm_required = false;
    expect(AlarmDetail::service_json(s, "alarm_arm_away", "1234").empty(), "arming without a code when not required");
    s.code_needed = false;
    expect(AlarmDetail::service_json(s, "alarm_disarm", "1234").empty(), "no code_format: never send a code");
}

void test_calendar_and_todo()
{
    const char* result = R"({"context":{"id":"x"},"response":{"calendar.familie":{"events":[
        {"start":"2026-10-12T09:30:00+02:00","end":"2026-10-12T10:00:00+02:00","summary":"Zahnarzt"},
        {"start":"2026-10-09","end":"2026-10-10","summary":"Mittelaltermarkt"}]}}})";
    auto events = CalendarTile::parse_events(result, "calendar.familie");
    expect(events.size() == 2 && events[0].summary == "Mittelaltermarkt" && events[0].all_day &&
               !events[1].all_day,
           "events sorted by start; a date-only start is all-day");
    expect(CalendarTile::parse_events(result, "calendar.other").empty(), "only the asked calendar's events");

    // relative day names against "now" in local time
    struct tm l {};
    l.tm_year = 2026 - 1900;
    l.tm_mon = 9;
    l.tm_mday = 8;
    l.tm_hour = 10;
    l.tm_isdst = -1;
    const double now = static_cast<double>(mktime(&l));
    CalendarTile::Event today{now + 8 * 3600, false, "x"};
    CalendarTile::Event tomorrow{now + 86400, true, "y"};
    expect(CalendarTile::when_text(today, now) == "Heute · 18:00", "today with its time");
    expect(CalendarTile::when_text(tomorrow, now) == "Morgen · Ganztägig", "tomorrow, all day");
    CalendarTile::Event saturday{now + 2 * 86400, false, "z"}; // Thu 8 Oct 2026 + 2
    expect(CalendarTile::when_text(saturday, now) == "Sa. · 10:00", "later this week: the weekday");

    auto items = TodoTile::parse_open_items(R"({"items":[
        {"uid":"1","summary":"Milch","status":"needs_action"},
        {"uid":"2","summary":"Brot","status":"completed"},
        {"uid":"3","summary":"Äpfel","status":"needs_action"}]})");
    expect(items.size() == 2 && items[0].summary == "Milch" && items[1].uid == "3",
           "a todo tile lists the open items in order");
}

// 32 x 16 baseline JPEG: left half red, right half blue (made with Pillow)
const uint8_t kTestJpeg[] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06,
    0x07, 0x09, 0x08, 0x06, 0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
    0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x05, 0x03, 0x03, 0x05, 0x0A, 0x07, 0x06, 0x07, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0xFF, 0xC0,
    0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x20, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00,
    0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03,
    0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
    0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15,
    0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29,
    0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4,
    0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6,
    0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00, 0x03,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07,
    0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51,
    0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15,
    0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84,
    0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
    0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11,
    0x00, 0x3F, 0x00, 0xF9, 0x6E, 0x8A, 0x28, 0xAF, 0xE7, 0xF3, 0xFD, 0x7C, 0x3E, 0x73, 0xA2, 0x8A, 0x2B, 0xFD, 0xFC, 0x3F,
    0xC4, 0x73, 0xFF, 0xD9,
};

// the same picture as a progressive JPEG and as a PNG (TJpgDec reads neither)
const uint8_t kTestProgressive[] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06,
    0x07, 0x09, 0x08, 0x06, 0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
    0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x05, 0x03, 0x03, 0x05, 0x0A, 0x07, 0x06, 0x07, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0xFF, 0xC2,
    0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x20, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00,
    0x15, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
    0xFF, 0xC4, 0x00, 0x15, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x07, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10, 0x00, 0x00, 0x01, 0x96, 0x89, 0xFA,
    0xFA, 0x72, 0x1F, 0xC2, 0x3F, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x05, 0x02, 0x0F, 0xFF, 0xC4,
    0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0xFF, 0xDA, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01, 0x3F, 0x01, 0x3F, 0xFF, 0xC4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01,
    0x01, 0x3F, 0x01, 0x3F, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x06, 0x3F, 0x02, 0x0F, 0xFF, 0xC4, 0x00,
    0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xFF,
    0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x3F, 0x21, 0x0F, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x0F, 0xFF, 0xC4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01, 0x3F, 0x10, 0x3F, 0xFF,
    0xC4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x02, 0x01, 0x01, 0x3F, 0x10, 0x3F, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01,
    0x00, 0x01, 0x3F, 0x10, 0x0F, 0xFF, 0xD9,
};
const uint8_t kTestPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x10, 0x08, 0x02, 0x00, 0x00, 0x00, 0xF8, 0x62, 0xEA, 0x0E, 0x00, 0x00, 0x00, 0x2D, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9C, 0x63, 0xBC, 0x23, 0x27, 0xC7, 0x40, 0x0A, 0x70, 0x62, 0xD8, 0x47, 0x92, 0x7A, 0x26, 0x06, 0x1A, 0x03,
    0xA6, 0x51, 0x0B, 0x08, 0x81, 0xD1, 0x20, 0x22, 0x08, 0x46, 0x83, 0x88, 0x20, 0x18, 0x0D, 0x22, 0x82, 0x60, 0xE8, 0x07,
    0x11, 0x00, 0xE5, 0x56, 0x02, 0x38, 0x78, 0x89, 0x2C, 0x39, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42,
    0x60, 0x82,
};

bool red_left_blue_right(const app::media::DecodedImage& img)
{
    int lr, lg, lb, rr, rg, rb;
    img.rgb_at(0, lr, lg, lb);
    img.rgb_at(static_cast<size_t>(img.w) - 1, rr, rg, rb);
    return lr > 160 && lb < 80 && rb > 160 && rr < 80;
}

void test_pictures()
{
    using app::media::ImageLoader;
    ImageLoader::Server server{"http://192.0.2.5:8123", "secret"};
    bool token = false;
    expect(ImageLoader::resolve("/api/camera_proxy/camera.x?token=1", server, &token) ==
                   "http://192.0.2.5:8123/api/camera_proxy/camera.x?token=1" && token,
           "a path goes to Home Assistant, with the token");
    expect(ImageLoader::resolve("https://example.com/a.jpg", server, &token) == "https://example.com/a.jpg" && !token,
           "another server never gets the Home Assistant token");
    ImageLoader::resolve("http://192.0.2.5:8123/local/a.jpg", server, &token);
    expect(token, "a full URL on the Home Assistant server keeps the token");
    ImageLoader::resolve("http://192.0.2.5:81234/x.jpg", server, &token);
    expect(!token, "a look-alike host with a longer port is another server");
    const std::string up = ImageLoader::upload_body("B", "media-source://media_source/local/Homepane", "a.jpg", "JPG");
    expect(up == "--B\r\nContent-Disposition: form-data; name=\"media_content_id\"\r\n\r\n"
                 "media-source://media_source/local/Homepane\r\n--B\r\n"
                 "Content-Disposition: form-data; name=\"file\"; filename=\"a.jpg\"\r\n"
                 "Content-Type: image/jpeg\r\n\r\nJPG\r\n--B--\r\n",
           "the media upload: target folder and picture as multipart form data");

    app::media::DecodedImage img;
    std::string error;
    const bool ok = app::media::decode_jpeg(kTestJpeg, sizeof(kTestJpeg), 16, 16, false, img, error);
    expect(ok && img.w == 16 && img.h == 8, "fit: 32 x 16 into 16 x 16 gives 16 x 8 (" + error + ")");
    if (ok) expect(red_left_blue_right(img), "the sampled pixels keep red left and blue right");
    app::media::DecodedImage img16;
    img16.rgb888 = false;
    expect(app::media::decode_jpeg(kTestJpeg, sizeof(kTestJpeg), 16, 16, false, img16, error) &&
               img16.pixels.size() == 16 * 8 * 2 && red_left_blue_right(img16),
           "RGB565 (16-bit displays): 2 bytes per pixel, colours kept");
    expect(app::media::decode_jpeg(kTestJpeg, sizeof(kTestJpeg), 8, 8, true, img, error) && img.w == 8 && img.h == 8,
           "cover: 8 x 8 cut from the middle");
    expect(!app::media::decode_jpeg(kTestJpeg, 3, 8, 8, true, img, error), "a truncated file is refused");

    expect(!app::media::decode_jpeg(kTestProgressive, sizeof(kTestProgressive), 16, 16, false, img, error),
           "TJpgDec refuses a progressive JPEG");
    expect(app::media::decode_image(kTestProgressive, sizeof(kTestProgressive), 16, 16, false, img, error) &&
               img.w == 16 && img.h == 8 && red_left_blue_right(img),
           "a progressive JPEG decodes through stb_image");
    // an MJPEG stream arrives in pieces: pictures are cut at FF D8 ... FF D9
    std::string buf = "--frame\r\nContent-Type: image/jpeg\r\n\r\n\xFF\xD8" "ab";
    std::string jpeg;
    expect(!ImageLoader::take_jpeg(buf, jpeg) && buf == "\xFF\xD8" "ab", "half a picture waits for the rest");
    buf += "c\xFF\xD9\r\n--frame\r\n\xFF";
    expect(ImageLoader::take_jpeg(buf, jpeg) && jpeg == "\xFF\xD8" "abc\xFF\xD9", "a whole picture is taken out");
    expect(!ImageLoader::take_jpeg(buf, jpeg) && buf == "\xFF", "a trailing FF is kept for the next marker");

    expect(app::media::decode_image(kTestPng, sizeof(kTestPng), 16, 16, false, img, error) && img.w == 16 &&
               red_left_blue_right(img),
           "a PNG decodes through stb_image");
}

void test_doorbell_rules()
{
    using app::doorbell::DoorbellController;
    const double now = 1791437400; // 2026-10-08T05:30:00Z
    const std::string fresh = "2026-10-08T05:29:58.120+00:00";
    expect(DoorbellController::is_ring("input_button", std::string("unknown"), fresh, now),
           "the first press of a never pressed button rings");
    expect(DoorbellController::is_ring("event", std::string("2026-10-08T05:00:00+00:00"), fresh, now),
           "a new event timestamp rings");
    expect(!DoorbellController::is_ring("event", std::nullopt, fresh, now),
           "the state delivered on (re)subscribing never rings");
    expect(!DoorbellController::is_ring("event", std::string("2026-10-08T05:00:00+00:00"),
                                        "2026-10-08T05:10:00+00:00", now),
           "a press older than a minute (panel was offline) does not ring");
    expect(!DoorbellController::is_ring("event", std::string(fresh), fresh, now), "the same state again: no ring");
    expect(!DoorbellController::is_ring("event", std::string(fresh), "unavailable", now), "unavailable: no ring");
    expect(DoorbellController::is_ring("binary_sensor", std::string("off"), "on", now) &&
               !DoorbellController::is_ring("binary_sensor", std::string("on"), "off", now) &&
               !DoorbellController::is_ring("binary_sensor", std::string("unavailable"), "on", now),
           "a sensor rings on off -> on only");
    expect(DoorbellController::opener_service("lock.haustuer") == "lock.unlock" &&
               DoorbellController::opener_service("input_button.demo_turoffner") == "input_button.press" &&
               DoorbellController::opener_service("script.tuer_auf") == "script.turn_on" &&
               DoorbellController::opener_service("sensor.x").empty(),
           "the door opener's service follows its domain");
}

// A state object as EntityStore holds it: attribute name -> JSON text.
std::map<std::string, std::string> attributes_of(const std::string& state_json, std::string* state)
{
    std::map<std::string, std::string> out;
    cJSON* root = cJSON_Parse(state_json.c_str());
    const cJSON* st = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (state) *state = cJSON_IsString(st) ? st->valuestring : "";
    const cJSON* a = nullptr;
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(root, "attributes"))
    {
        char* txt = cJSON_PrintUnformatted(a);
        out[a->string] = txt;
        cJSON_free(txt);
    }
    cJSON_Delete(root);
    return out;
}

void test_eventcalendar_feed()
{
    using app::eventcalendar::parse_feed;
    std::ifstream f("tests/fixtures/eventcalendar/eventcalendar-demo-state.json");
    std::stringstream ss;
    ss << f.rdbuf();
    std::string state;
    auto attrs = attributes_of(ss.str(), &state);
    auto feed = parse_feed(state, attrs);
    expect(feed.available && feed.events.size() == 2 && feed.waste.size() == 1,
           "the add-on's demo state: 2 events, 1 pickup (as its expected output says)");
    if (feed.events.size() == 2) {
        const auto& e = feed.events[0];
        expect(e.title == "Demo: Herbstmarkt" && e.location == "Marktplatz, Beispielstadt" && e.distance_km &&
                   *e.distance_km == 8.4 && e.distance_type == "Luftlinie" && e.image_kind == "symbol" &&
                   e.image_key == "11111111111111111111111111111111",
               "event fields: title, place, air distance, symbol picture, image key");
        expect(static_cast<long>(e.start) == 1791622800L && static_cast<long>(e.end - e.start) == 7 * 3600,
               "times with offset: 10.10.2026 11:00-18:00 Berlin");
    }
    expect(feed.waste.size() == 1 && feed.waste[0].title == "Demo: Gelbe Tonne" && feed.waste[0].day_label == "Morgen",
           "the pickup with its day label");
    expect(feed.card_seconds == 10 && feed.idle_seconds == 45, "display: 10 s per card, 45 s pause after a swipe");

    expect(!parse_feed("unavailable", attrs).available, "unavailable is not current");
    auto v2 = attrs;
    v2["api_version"] = "2";
    expect(!parse_feed(state, v2).available, "an unknown api_version is not shown");
    auto empty = attrs;
    empty["events"] = "[]";
    empty["waste"] = "[]";
    auto e2 = parse_feed("0", empty);
    expect(e2.available && e2.events.empty() && e2.waste.empty(), "empty lists are a normal, current state");
    // texts of the screensaver's event card (local times)
    struct tm l {};
    l.tm_year = 2026 - 1900;
    l.tm_mon = 9;
    l.tm_mday = 10;
    l.tm_hour = 11;
    l.tm_isdst = -1;
    const double sat11 = static_cast<double>(mktime(&l));
    app::eventcalendar::Event ev;
    ev.start = sat11;
    ev.end = sat11 + 7 * 3600;
    ev.distance_km = 8.4;
    using app::ui::Screensaver;
    expect(Screensaver::chip_text(ev) == "Sa. 10. Okt." && Screensaver::time_text(ev) == "11:00–18:00 Uhr" &&
               Screensaver::distance_text(ev) == "8,4 km entfernt",
           "event card: date chip, time range, distance (as in the design)");
    ev.end = sat11 + 2 * 86400;
    expect(Screensaver::time_text(ev) == "11:00 Uhr – Mo. 12. Okt.", "several days: start time until the last day");
    ev.all_day = true;
    ev.start = sat11 - 11 * 3600; // Sat 0:00
    ev.end = ev.start + 86400;    // exclusive end
    expect(Screensaver::time_text(ev) == "Ganztägig" && Screensaver::chip_text(ev) == "Sa. 10. Okt.",
           "an all-day event");
    // the date and time format of the settings
    {
        using namespace app::core;
        const struct tm t = local_tm(sat11 + 3 * 3600 + 5 * 60); // Sat 10 Oct 2026, 14:05
        set_datetime_format(datetime_format_from("12h", "us"));
        expect(time_text(t) == "2:05 PM" && clock_digits(t) == "2:05" && std::string(clock_meridiem(t)) == "PM" &&
                   hour_text(0) == "12 AM" && date_text(t, DateStyle::weekday_day_month) == "Sa., Okt. 10" &&
                   date_text(t, DateStyle::numeric) == "10/10/2026",
               "American: 12-hour clock, month before day");
        ev.all_day = false;
        ev.start = sat11;
        ev.end = sat11 + 7 * 3600;
        expect(Screensaver::time_text(ev) == "11:00 AM–6:00 PM", "12 hours: AM/PM instead of \"Uhr\"");
        ev.start = sat11 + 3 * 3600;
        expect(Screensaver::time_text(ev) == "2:00–6:00 PM", "12 hours: one PM for both");
        set_datetime_format(datetime_format_from("24h", "iso"));
        expect(date_text(t, DateStyle::weekday_day_month) == "Okt. 10 (Sa.)" &&
                   date_text(t, DateStyle::numeric) == "2026-10-10" && time_text(t) == "14:05",
               "Asian: month, day (weekday)");
        set_datetime_format(datetime_format_from(nullptr, nullptr));
        expect(date_text(t, DateStyle::long_weekday) == "Samstag, 10. Okt." && date_text(t, DateStyle::numeric) == "10.10.2026" &&
                   Screensaver::time_text(ev) == "14:00–18:00 Uhr",
               "the default: European, 24 hours");
    }
    expect(Screensaver::waste_color("Demo: Gelbe Tonne") == 0xD9B840 && Screensaver::waste_color("Papiertonne") == 0x6C97D4 &&
               Screensaver::waste_color("Restmüll") == 0x8A8A88,
           "waste bins by colour");

    auto nulls = attrs;
    nulls["events"] = R"([{"id":"x","title":"T","start":"2026-10-09","end":"2026-10-10","all_day":true,"distance_km":null}])";
    auto e3 = parse_feed("1", nulls);
    expect(e3.events.size() == 1 && !e3.events[0].distance_km && e3.events[0].all_day,
           "a null distance and an all-day event");
}

void test_climate_view()
{
    ClimateTile::Model heat;
    heat.mode = "heat";
    heat.target = 21.5;
    heat.min = 7;
    heat.max = 30;
    auto v = ClimateTile::view_for(heat);
    expect(v.main == "21,5°" && v.sub == "Heizen" && v.color == UI_HEAT, "heating: target, mode, heat colour");
    expect(v.arc_from == 0 && v.arc_to > 0.6 && v.arc_to < 0.64, "the arc fills up to the target");

    ClimateTile::Model range;
    range.mode = "heat_cool";
    range.low = 21;
    range.high = 24;
    range.current = 23.5;
    auto r = ClimateTile::view_for(range);
    expect(r.adjusts_high && r.main == "24°" && r.sub == "Heizen bis 21°" && r.color == UI_COOL,
           "room near the upper bound: it is the big number and -/+ moves it");
    range.current = 20;
    expect(!ClimateTile::view_for(range).adjusts_high, "colder than the range: the lower bound");

    ClimateTile::Model off;
    off.mode = "off";
    expect(ClimateTile::view_for(off).main == "Aus" && !ClimateTile::view_for(off).arc_visible, "off: no arc");

    ClimateTile::Model hum;
    hum.humidity = true;
    hum.mode = "on";
    hum.min = 30;
    hum.max = 80;
    hum.target = 45;
    hum.current = 38;
    auto h = ClimateTile::view_for(hum);
    expect(h.main == "45 %" && h.sub == "Jetzt 38 %", "humidifier: target and current humidity");
}

void test_cover_and_media()
{
    expect(CoverTile::shade_percent("open", 40) == 60, "40 % open: the shade covers 60 %");
    expect(CoverTile::shade_percent("closed", std::nullopt) == 100 &&
               CoverTile::shade_percent("open", std::nullopt) == 0,
           "without a position: closed or open");
    expect(MediaTile::status_text("playing", "Blue in Green", "Miles Davis") == "Spielt · Blue in Green – Miles Davis",
           "what is playing");
    expect(MediaTile::status_text("off", "", "") == "Aus", "a player that is off");
}

void test_chart_and_options()
{
    auto bars = ChartTile::bar_heights({10, 20, 40});
    expect(bars.size() == ChartTile::kBars && bars.back() == 100 && bars[ChartTile::kBars - 3] == 25,
           "bars relative to the highest");
    expect(bars.front() == 4 && ChartTile::bar_heights({5}).size() == ChartTile::kBars,
           "hours without data are minimal bars on the left, the bars keep their width");
    expect(ChartTile::bar_heights({}).empty(), "no bars without data");
    std::vector<double> many(30, 1.0);
    expect(ChartTile::bar_heights(many).size() == ChartTile::kBars, "only the last 24 hours");
    auto kw = ChartTile::value_text("1240", "W");
    expect(kw.first == "1,24" && kw.second == "kW", "1240 W is shown as 1,24 kW");

    std::string title;
    auto items = OptionsTile::parse_entities(
        R"({"type":"entities","title":"Szenen","entities":["scene.a",{"entity":"script.b","name":"B"}]})", &title);
    expect(title == "Szenen" && items.size() == 2 && items[1].entity_id == "script.b" && items[1].name == "B",
           "an entities card: title, plain ids and objects with names");
}

void test_formatting()
{
    expect(TileWidget::strip_prefix("TempHumidServerschrank Temperatur", "TempHumidServerschrank") == "Temperatur",
           "the section title in front of a name is left out");
    expect(TileWidget::strip_prefix("Küche Licht", "küche") == "Licht", "case does not matter");
    expect(TileWidget::strip_prefix("Küchenlicht", "Küche") == "Küchenlicht", "only whole words are left out");
    expect(TileWidget::strip_prefix("Küche", "Küche") == "Küche", "a name that is only the title stays");
    expect(format_duration(30) == "gerade eben" && format_duration(125) == "2 Min." &&
               format_duration(7200) == "2 Std." && format_duration(86400) == "1 Tag",
           "durations in German");
}

} // namespace

void test_compact_tile()
{
    using app::ui::CompactTile;
    CompactTile::Facts f;
    f.domain = "sensor";
    f.state = "1234.5";
    f.unit = "W";
    f.device_class = "power";
    CompactTile::Look l = CompactTile::look_for(f);
    expect(l.icon == LI_ZAP && l.state_text == "1234,5 W", "compact sensor: power icon, value with unit");
    f.available = false;
    expect(CompactTile::look_for(f).state_text == "Nicht verfügbar", "compact sensor: unavailable");

    CompactTile::Facts light;
    light.domain = "light";
    light.state = "on";
    light.brightness = 127.5;
    l = CompactTile::look_for(light);
    expect(l.look == UI_LOOK_ON && l.state_text == "50 %", "compact light: on with brightness");

    CompactTile::Facts cover;
    cover.domain = "cover";
    cover.state = "open";
    cover.position = 100;
    expect(CompactTile::look_for(cover).state_text == "Offen · 100 %", "compact cover: open with position");

    expect(CompactTile::tap_service("light", "off") == "turn_on" && CompactTile::tap_service("cover", "open") == "close_cover" &&
               CompactTile::tap_service("lock", "locked").empty() && CompactTile::tap_service("sensor", "1").empty(),
           "compact tap: toggle, open/close, never unlock, nothing for sensors");
    expect(CompactTile::tap_opens_detail("cover") && CompactTile::tap_opens_detail("climate") &&
               !CompactTile::tap_opens_detail("light") && !CompactTile::tap_opens_detail("lock"),
           "compact tap: the detail page where the domain's own tile opens it");
    expect(app::ui::EntityDetail::exists_for("light") && app::ui::EntityDetail::exists_for("media_player") &&
               !app::ui::EntityDetail::exists_for("switch"),
           "detail pages: light, cover, climate, media player, alarm panel");
}

int main()
{
    i18n_set_language("de"); // the expected texts are German
    test_entity_tile_looks();
    test_compact_tile();
    test_value_tile_texts();
    test_more_value_domains();
    test_alarm_keypad_logic();
    test_calendar_and_todo();
    test_pictures();
    test_doorbell_rules();
    test_eventcalendar_feed();
    test_climate_view();
    test_cover_and_media();
    test_chart_and_options();
    test_formatting();
    if (g_failures == 0) {
        std::printf("All tile tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}
