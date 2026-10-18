#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

typedef void (*callback_t)(cJSON *data);
typedef void (*ws_error_callback_t)(void);

// Multi-connection support: opaque client handle
typedef struct WsClient WsClient;

extern void enter_setup_state();

// Named-connection send / subscribe — still live: Music Assistant (the one
// consumer not yet migrated to the new C++ stack, see
// C++ migration phase 10/section 16) is the sole remaining caller.
int ws_send_message_named(const char *name, char* msg, callback_t callback);

// Multi-connection API (Music Assistant's transport).
WsClient* ws_create_connection_url(const char *ws_url, callback_t on_success, ws_error_callback_t on_error);
WsClient* ws_create_connection_url_named(const char *name, const char *ws_url, callback_t on_success, ws_error_callback_t on_error);
int ws_send_message(WsClient *client, char* msg, callback_t callback);
void ws_close(WsClient *client);
void ws_service_all();
WsClient* ws_get_client_by_name(const char *name);

#endif
