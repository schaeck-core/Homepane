/**
 * @file clock.h
 * @brief Clock settings tab
 */

#ifndef SETTINGS_CLOCK_H
#define SETTINGS_CLOCK_H

#include "settings_tab.h"

/* The time and date format rows, with an example (Settings > General). */
void settings_clock_rows(lv_obj_t *page);

/**
 * @brief Get the clock settings tab descriptor
 */
const settings_tab_t* settings_clock_get_tab(void);

#endif // SETTINGS_CLOCK_H
