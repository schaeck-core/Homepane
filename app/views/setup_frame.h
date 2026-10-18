/* Frame of the first-run setup screens in the design guide's look: step
 * indicator, optional back button (top left), title 28 + subtitle, a content
 * column and a footer with large buttons. Steps: language, network, Home
 * Assistant, dashboard. */
#ifndef APP_VIEWS_SETUP_FRAME_H
#define APP_VIEWS_SETUP_FRAME_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SETUP_STEP_LANGUAGE = 1,
    SETUP_STEP_NETWORK,
    SETUP_STEP_HOME_ASSISTANT,
    SETUP_STEP_DASHBOARD,
    SETUP_STEP_COUNT = SETUP_STEP_DASHBOARD,
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *content; /* column, gap 12, takes the remaining height */
    lv_obj_t *footer;  /* row of buttons at the bottom (hidden while empty) */
} setup_frame_t;

/* Fills `parent` (cleaned first). on_back NULL: no back button. */
setup_frame_t setup_frame_create(lv_obj_t *parent, int step, const char *title, const char *subtitle,
                                 lv_event_cb_t on_back);

/* Footer button (88 high, radius 22, shares the width): primary is sand with
 * black text, otherwise surface. icon may be NULL. */
lv_obj_t *setup_button(lv_obj_t *footer, const char *icon, const char *label, bool primary, lv_event_cb_t on_click,
                       void *user);
void setup_button_set_enabled(lv_obj_t *button, bool enabled);

/* Selectable list row (88 high, radius 20): icon circle, title, optional
 * subtitle; a check mark when selected. */
lv_obj_t *setup_choice_row(lv_obj_t *parent, const char *icon, const char *title, const char *subtitle,
                           bool selected, lv_event_cb_t on_click, void *user);
void setup_choice_row_set_selected(lv_obj_t *row, bool selected);

/* Message line in the content (warning colour when error). */
lv_obj_t *setup_message(lv_obj_t *parent, const char *text, bool error);

#ifdef __cplusplus
}
#endif

#endif /* APP_VIEWS_SETUP_FRAME_H */
