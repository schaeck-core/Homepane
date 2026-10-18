/**
 * @file addons.c
 * @brief Addon system loader implementation
 *
 * Manages loading and initialization of all addons.
 */

#include <stdio.h>
#include <string.h>
#include "addons.h"
#include "doorbell/settings.h"
#include "musicassistant/settings.h"
#include "../config.h"

// Include all addon headers
#include "musicassistant/addon.h"
#include "doorbell/addon.h"

#define MAX_ADDONS 16

static const addon_t *registered_addons[MAX_ADDONS];
static int addon_count = 0;
static bool addons_initialized = false;

void addons_register(const addon_t *addon)
{
    if (addon_count < MAX_ADDONS && addon != NULL) {
        registered_addons[addon_count++] = addon;
        printf("[Addons] Registered addon: %s\n", addon->name);
    } else {
        printf("[Addons] Warning: Cannot register addon, limit reached\n");
    }
}

const addon_t** addons_get_list(int *count)
{
    if (count) {
        *count = addon_count;
    }
    return registered_addons;
}

bool addons_is_enabled(const char *addon_id)
{
    if (!addon_id) return false;

    char config_key[64];
    snprintf(config_key, sizeof(config_key), "addon_%s_enabled", addon_id);

    const char *value = getConfig(config_key);
    // Default to enabled if not configured
    return (value == NULL || strcmp(value, "1") == 0);
}

void addons_set_enabled(const char *addon_id, bool enabled)
{
    if (!addon_id) return;

    char config_key[64];
    snprintf(config_key, sizeof(config_key), "addon_%s_enabled", addon_id);

    updateConfig(config_key, enabled ? "1" : "0");
    saveConfig();

    printf("[Addons] Addon '%s' %s\n", addon_id, enabled ? "enabled" : "disabled");
}

void addons_init(void)
{
    if (addons_initialized) return;

    printf("[Addons] Initializing addon system...\n");

    // Register all available addons
    // Each addon's register function adds it to the list
    addon_musicassistant_register();
    addon_doorbell_register();

    // Initialize enabled addons
    for (int i = 0; i < addon_count; i++) {
        const addon_t *addon = registered_addons[i];
        if (addon && addon->init && addons_is_enabled(addon->id)) {
            printf("[Addons] Initializing: %s\n", addon->name);
            addon->init();
        }
    }

    addons_initialized = true;
    printf("[Addons] Addon system initialized with %d addons\n", addon_count);
}

void addons_register_settings(void)
{
    if (addons_is_enabled("musicassistant")) addon_musicassistant_settings_init();
    if (addons_is_enabled("doorbell")) addon_doorbell_settings_init();
}

void addons_deinit(void)
{
    if (!addons_initialized) return;

    printf("[Addons] Shutting down addon system...\n");

    for (int i = 0; i < addon_count; i++) {
        const addon_t *addon = registered_addons[i];
        if (addon && addon->deinit && addons_is_enabled(addon->id)) {
            printf("[Addons] Deinitializing: %s\n", addon->name);
            addon->deinit();
        }
    }

    addon_count = 0;
    addons_initialized = false;
}
