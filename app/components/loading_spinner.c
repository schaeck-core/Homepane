#include "loading_spinner.h"

#include "../fonts.h"
#include "../ui_kit.h"

/* The loading overlay: the logo builds up and its lit pane pulses (ui_loader),
 * on the screen's black, with an optional status line below. The label is
 * the overlay's second child when there is one. */

#define LOADER_PANE 28

lv_obj_t* loading_spinner_create(lv_obj_t* parent, const char* text) {
    lv_obj_t* overlay = ui_box(parent ? parent : lv_screen_active());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE); /* nothing below gets the touches */
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* logo = ui_loader(overlay, LOADER_PANE);
    lv_obj_center(logo);
    loading_spinner_set_text(overlay, text);
    return overlay;
}

void loading_spinner_delete(lv_obj_t* overlay) {
    if(overlay) lv_obj_delete(overlay); /* its animations go with the objects */
}

void loading_spinner_set_text(lv_obj_t* overlay, const char* text) {
    if(!overlay) return;
    lv_obj_t* logo = lv_obj_get_child(overlay, 0);
    lv_obj_t* label = lv_obj_get_child(overlay, 1);
    if(!text) {
        if(label) lv_obj_delete(label);
        return;
    }
    if(!label) {
        label = ui_label(overlay, UI_FONT_LABEL, UI_TEXT2, "");
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(label, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 28);
    }
    lv_label_set_text(label, text);
}
