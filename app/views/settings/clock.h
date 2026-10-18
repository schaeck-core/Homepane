/**
 * @file clock.h
 * @brief Clock settings tab
 */

#ifndef SETTINGS_CLOCK_H
#define SETTINGS_CLOCK_H

#include "settings_tab.h"

/**
 * @brief Initialize and register the clock settings tab
 */
void settings_clock_init(void);

/**
 * @brief Get the clock settings tab descriptor
 */
const settings_tab_t* settings_clock_get_tab(void);

#endif // SETTINGS_CLOCK_H
