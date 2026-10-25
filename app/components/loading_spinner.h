#ifndef LOADING_SPINNER_H
#define LOADING_SPINNER_H

#include "lvgl/lvgl.h"

/**
 * @brief Create the loading overlay
 *
 * A full-screen overlay on the screen's black with the logo loader (the logo
 * builds up, then its lit pane pulses: ui_loader() in ui_kit.h).
 *
 * @param parent Parent object (use NULL or lv_screen_active() for full screen)
 * @param text Optional text to display below spinner (can be NULL)
 * @return lv_obj_t* The overlay object (store this to delete later)
 */
lv_obj_t* loading_spinner_create(lv_obj_t* parent, const char* text);

/**
 * @brief Delete the loading spinner overlay
 *
 * Removes the overlay; its animations go with it (lv_obj_delete() works too).
 *
 * @param overlay The overlay object returned by loading_spinner_create()
 */
void loading_spinner_delete(lv_obj_t* overlay);

/**
 * @brief Update the text of an existing loading spinner
 *
 * @param overlay The overlay object
 * @param text New text to display (can be NULL to hide text)
 */
void loading_spinner_set_text(lv_obj_t* overlay, const char* text);

#endif // LOADING_SPINNER_H
