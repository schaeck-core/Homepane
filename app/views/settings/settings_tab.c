/**
 * @file settings_tab.c
 * @brief Tab registration implementation for settings menu
 */

#include <stdlib.h>
#include "settings_tab.h"

#define MAX_SETTINGS_TABS 16

static const settings_tab_t *registered_tabs[MAX_SETTINGS_TABS];
static int tab_count = 0;

void settings_register_tab(const settings_tab_t *tab)
{
    if (tab_count < MAX_SETTINGS_TABS && tab != NULL) {
        registered_tabs[tab_count++] = tab;
    }
}

const settings_tab_t** settings_get_tabs(int *count)
{
    if (count) {
        *count = tab_count;
    }
    return registered_tabs;
}

void settings_clear_tabs(void)
{
    tab_count = 0;
}
