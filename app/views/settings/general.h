/**
 * @file general.h
 * @brief General settings tab
 */

#ifndef SETTINGS_GENERAL_H
#define SETTINGS_GENERAL_H

#include "settings_tab.h"

/**
 * @brief Initialize and register the general settings tab
 */
void settings_general_init(void);

/**
 * @brief Get the general settings tab descriptor
 */
const settings_tab_t* settings_general_get_tab(void);

#endif // SETTINGS_GENERAL_H
