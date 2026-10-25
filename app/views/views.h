

#ifndef VIEWS_H
#define VIEWS_H
#include <stdbool.h>
#include "lvgl/lvgl.h"
#include "../components/loading_spinner.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * loadingOverlay;
extern bool isLoading;

extern lv_obj_t *menu;
extern lv_obj_t *menuContent;

// State-Callback-Funktionszeiger für State-Übergabe zwischen main und Views
extern void enter_complete_state();
extern void enter_setup_state();

void dashboard_render(lv_obj_t *parent);
/* Rebuilds the dashboard with the default one (haas_dashboard), e.g. after
 * the settings changed it or the dashboard mode. */
void dashboard_show_default(void);
void setup_render(lv_obj_t *parent);
void initialization_render(lv_obj_t *parent);
/* Developer builds: the setup's QR card as a preview over everything. */
void initialization_qr_preview(void);
/* The QR login failed (pairing in webserver.c / webserver_esp32.c); UI thread. */
void initialization_pairing_failed(const char *detail);
void initialization_pairing_token_exists(const char *token_name);
/* Setup step "network". autoskip: continue at once when Ethernet is up
 * (false when the user came back to this step). */
void network_setup_render(lv_obj_t *parent, void (*continue_callback)(void), void (*back_callback)(void),
                          bool autoskip);

#ifdef __cplusplus
}
#endif

#endif
