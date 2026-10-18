#ifndef LOADING_SPINNER_H
#define LOADING_SPINNER_H

#include "lvgl/lvgl.h"

/**
 * @brief Create a modern loading overlay with animated spinner
 *
 * Creates a full-screen semi-transparent overlay with a custom animated spinner.
 * The spinner features three rotating circles in a circular pattern for a modern look.
 *
 * @param parent Parent object (use NULL or lv_screen_active() for full screen)
 * @param text Optional text to display below spinner (can be NULL)
 * @return lv_obj_t* The overlay object (store this to delete later)
 */
lv_obj_t* loading_spinner_create(lv_obj_t* parent, const char* text);

/**
 * @brief Delete the loading spinner overlay
 *
 * Safely removes the loading spinner and cleans up all animations.
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
