#include "UnsupportedWidget.h"

#include "lvgl/lvgl.h"
#include "../../i18n.h"

#include "../../ui_kit.h"

namespace app::ui {

// A tile in the design guide's look that names the card type the panel
// cannot show (yet), instead of leaving a hole in the grid.
void UnsupportedWidget::create(lv_obj_t* parent) {
    lv_obj_t* tile = ui_tile(parent);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 14, 0);
    ui_bubble(tile, UI_BUBBLE_SMALL, UI_ICON_VALUE, LI_CIRCLE_HELP, UI_LOOK_OFF);
    lv_obj_t* name = ui_label_line(tile, UI_FONT_NAME, UI_TEXT2, tr(TR_DASHBOARD_UNSUPPORTED));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* type = ui_label_line(tile, UI_FONT_LABEL, UI_TEXT3, card_type_.c_str());
    lv_obj_set_style_text_align(type, LV_TEXT_ALIGN_CENTER, 0);
    track_root(tile);
}

} // namespace app::ui
