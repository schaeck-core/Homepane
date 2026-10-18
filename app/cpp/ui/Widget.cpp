#include "Widget.h"

#include "lvgl/lvgl.h"

namespace app::ui {

namespace {
void raw_delete_cb(lv_event_t* e) { Widget::on_root_deleted_trampoline(e); }
} // namespace

void Widget::track_root(lv_obj_t* root) {
    root_ = root;
    if (root_) {
        lv_obj_add_event_cb(root_, raw_delete_cb, LV_EVENT_DELETE, this);
    }
}

void Widget::on_root_deleted_trampoline(void* event) {
    auto* e = reinterpret_cast<lv_event_t*>(event);
    auto* self = reinterpret_cast<Widget*>(lv_event_get_user_data(e));
    if (self) self->root_ = nullptr;
}

} // namespace app::ui
