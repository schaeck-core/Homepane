/**
 * @file addon.c
 * @brief Music Assistant addon implementation
 *
 * Entry point for the Music Assistant integration.
 */

#include <stdio.h>
#include <string.h>
#include "addon.h"
#include "settings.h"
#include "../addons.h"
#include "../../config.h"
#include "../../cpp/app_facade.h"

static bool is_initialized = false;

// Persists a freshly (re-)issued MA session token so the next boot can try
// it before falling back to username/password (docs/ARCHITECTURE.md §14:
// MaClient auth contract). Not read by anything else yet — mirrors
// haas_access_token/haas_refresh_token, which are likewise cache-only
// config keys with no dedicated settings-UI field.
static void on_musicassistant_token_issued(const char *token)
{
    updateConfig("musicassistant_token", token);
    saveConfig();
}

void addon_musicassistant_addon_url(char *buf, size_t size)
{
    if (!buf || !size) return;
    buf[0] = '\0';
    const char *ha = getConfig("haas_url");
    if (!ha || !ha[0]) return;
    const char *host = strstr(ha, "://");
    host = host ? host + 3 : ha;
    size_t n = strcspn(host, ":/"); /* the host, without port and path */
    if (n == 0) return;
    snprintf(buf, size, "ws://%.*s:8095/ws", (int)n, host);
}

bool addon_musicassistant_is_addon(void)
{
    const char *mode = getConfig("musicassistant_mode");
    if (mode && mode[0]) return strcmp(mode, "custom") != 0;
    /* not chosen yet: the add-on, unless a URL other than the add-on's is set */
    const char *url = getConfig("musicassistant_url");
    char addon[160];
    addon_musicassistant_addon_url(addon, sizeof(addon));
    return !url || !url[0] || strcmp(url, addon) == 0;
}

void addon_musicassistant_set_addon(bool addon)
{
    updateConfig("musicassistant_mode", addon ? "addon" : "custom");
    if (addon) {
        char url[160];
        addon_musicassistant_addon_url(url, sizeof(url));
        if (url[0]) updateConfig("musicassistant_url", url);
    }
    saveConfig();
}

void addon_musicassistant_init(void)
{
    if (is_initialized) return;

    printf("[MusicAssistant] Initializing addon...\n");

    /* the add-on of HA: its URL follows haas_url */
    if (addon_musicassistant_is_addon()) {
        char addon[160];
        addon_musicassistant_addon_url(addon, sizeof(addon));
        const char *now = getConfig("musicassistant_url");
        if (addon[0] && (!now || strcmp(now, addon) != 0)) {
            updateConfig("musicassistant_url", addon);
            saveConfig();
        }
    }
    const char *url = getConfig("musicassistant_url");
    const char *username = getConfig("musicassistant_username");
    const char *password = getConfig("musicassistant_password");
    const char *cached_token = getConfig("musicassistant_token");

    if (url && url[0] && username && username[0] && password && password[0]) {
        app_cpp_music_assistant_connect(url, username, password, cached_token ? cached_token : "",
                                         on_musicassistant_token_issued);
    } else {
        printf("[MusicAssistant] Not configured (URL/username/password missing) - skipping connect\n");
    }

    is_initialized = true;
    printf("[MusicAssistant] Addon initialized\n");
}

void addon_musicassistant_deinit(void)
{
    if (!is_initialized) return;

    printf("[MusicAssistant] Deinitializing addon...\n");

    // TODO: Cleanup Music Assistant connections

    is_initialized = false;
}

static const addon_t musicassistant_addon = {
    .name = "Music Assistant",
    .id = "musicassistant",
    .description = "Control Music Assistant media players",
    .init = addon_musicassistant_init,
    .deinit = addon_musicassistant_deinit,
    .enabled = true
};

void addon_musicassistant_register(void)
{
    addons_register(&musicassistant_addon);
}

const addon_t* addon_musicassistant_get(void)
{
    return &musicassistant_addon;
}
