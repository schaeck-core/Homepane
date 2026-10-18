#include "MusicPullGesture.h"

#include "MusicPlayer.h"

#include <cstdlib>

namespace app::ui {

namespace {
constexpr int32_t kStartDy = 16; // a pull, not a tap
constexpr uint32_t kTickMs = 15;

void zone_deleted_cb(lv_event_t* e)
{
    auto* self = static_cast<MusicPullGesture*>(lv_event_get_user_data(e));
    self->set_zone(nullptr);
}
} // namespace

void MusicPullGesture::set_zone(lv_obj_t* zone)
{
    if (zone_ && zone) lv_obj_remove_event_cb_with_user_data(zone_, zone_deleted_cb, this);
    zone_ = zone;
    if (zone_) lv_obj_add_event_cb(zone_, zone_deleted_cb, LV_EVENT_DELETE, this);
    if (zone_ && !timer_) {
        timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<MusicPullGesture*>(lv_timer_get_user_data(t))->on_tick(); },
                                 kTickMs, this);
    }
    if (!zone_ && timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
        pressed_ = candidate_ = pulling_ = false;
    }
}

// The press is on the dashboard (nothing over it): on the header (the
// zone), or anywhere on a page that is scrolled to its top (pulling further
// down there opens the player, like overscrolling).
bool MusicPullGesture::in_zone(lv_point_t p) const
{
    if (!zone_ || lv_obj_get_screen(zone_) != lv_screen_active()) return false;
    for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); ++i) {
        if (!lv_obj_has_flag(lv_obj_get_child(lv_layer_top(), static_cast<int32_t>(i)), LV_OBJ_FLAG_HIDDEN)) return false;
    }
    lv_obj_t* dashboard = lv_obj_get_parent(zone_);
    auto inside = [](lv_obj_t* child, lv_obj_t* parent) {
        for (lv_obj_t* o = child; o; o = lv_obj_get_parent(o)) {
            if (o == parent) return true;
        }
        return false;
    };
    // what is hit there: part of the dashboard, or nothing clickable (the
    // screen); else something covers it (e.g. the settings)
    lv_obj_t* hit = lv_indev_search_obj(lv_screen_active(), &p);
    if (hit && !inside(hit, dashboard) && !inside(dashboard, hit)) return false;
    lv_area_t a;
    lv_obj_get_coords(zone_, &a);
    if (p.y <= a.y2 + 24) return true; // the header, and a margin below it
    return at_top_ && at_top_();
}

void MusicPullGesture::on_tick()
{
    // the press being watched, or a new one on any pointer device
    if (!pressed_) {
        for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i)) {
            if (lv_indev_get_type(i) != LV_INDEV_TYPE_POINTER || lv_indev_get_state(i) != LV_INDEV_STATE_PRESSED) continue;
            indev_ = i;
            pressed_ = true;
            lv_indev_get_point(i, &start_);
            candidate_ = player_ && in_zone(start_);
            pulling_ = false;
            last_dy_ = 0;
            last_at_ = lv_tick_get();
            speed_dy_ = 0;
            break;
        }
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev_, &p);
    const int32_t dy = p.y - start_.y;
    if (lv_indev_get_state(indev_) != LV_INDEV_STATE_PRESSED) { // released
        if (pulling_) {
            if (MusicPlayer* player = player_()) player->end_pull(dy, speed_dy_ > 60);
        }
        pressed_ = candidate_ = pulling_ = false;
        return;
    }
    if (!candidate_) return;
    if (!pulling_) {
        if (dy > kStartDy && dy > std::abs(p.x - start_.x)) { // more down than across
            MusicPlayer* player = player_();
            if (!player) {
                candidate_ = false;
                return;
            }
            pulling_ = true;
            // the header's button under the finger (the views menu, ...) gets no tap
            lv_indev_reset(indev_, nullptr);
            lv_indev_wait_release(indev_);
            player->begin_pull();
        } else {
            if (std::abs(p.x - start_.x) > 2 * kStartDy && std::abs(p.x - start_.x) > 2 * dy) candidate_ = false; // across: not ours
            return;
        }
    }
    if (MusicPlayer* player = player_()) player->pull(dy);
    const uint32_t now = lv_tick_get();
    if (now - last_at_ >= 100) {
        speed_dy_ = dy - last_dy_;
        last_dy_ = dy;
        last_at_ = now;
    }
}

} // namespace app::ui
