/**
 * @file addon.h
 * @brief Music Assistant addon header
 */

#ifndef ADDON_MUSICASSISTANT_H
#define ADDON_MUSICASSISTANT_H

#include "../addons.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Register the Music Assistant addon
 */
void addon_musicassistant_register(void);

/**
 * @brief Initialize the Music Assistant addon
 */
void addon_musicassistant_init(void);

/**
 * @brief Deinitialize the Music Assistant addon
 */
void addon_musicassistant_deinit(void);

/**
 * @brief Get the Music Assistant addon descriptor
 */
const addon_t* addon_musicassistant_get(void);

/**
 * @brief Music Assistant as Home Assistant's add-on: its URL from haas_url
 * (same host, port 8095, /ws), e.g. "ws://192.0.2.5:8095/ws". "" if unknown.
 */
void addon_musicassistant_addon_url(char *buf, size_t size);

/** @brief true: the add-on of HA (URL from haas_url); false: own installation. */
bool addon_musicassistant_is_addon(void);
void addon_musicassistant_set_addon(bool addon);

#endif // ADDON_MUSICASSISTANT_H
