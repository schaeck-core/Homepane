/**
 * @file homeassistant.h
 * @brief Home Assistant settings tab header
 */

#ifndef SETTINGS_HOMEASSISTANT_H
#define SETTINGS_HOMEASSISTANT_H

#include "settings_tab.h"

/**
 * @brief Initialize and register the Home Assistant settings tab
 */
void settings_homeassistant_init(void);

/* Registers the dashboard tab (also in homeassistant.c). */
void settings_dashboard_init(void);

/* The room rows (the panel's HA area) for Settings > General. */
void settings_room_rows(lv_obj_t *page);

/**
 * @brief Get the Home Assistant settings tab descriptor
 * @return Pointer to the settings tab structure
 */
const settings_tab_t* settings_homeassistant_get_tab(void);

#endif // SETTINGS_HOMEASSISTANT_H
