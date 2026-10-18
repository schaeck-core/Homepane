#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>

#define PORT 8989
#define CLIENT_ID "YOUR_CLIENT_ID"
#define CLIENT_SECRET "YOUR_CLIENT_SECRET"
#define AUTHORIZATION_CODE_URL "http://homeassistant.local:8123/auth/authorize"
#define TOKEN_URL "http://homeassistant.local:8123/auth/token"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * loadingOverlay;
extern bool isLoading;

bool startServer();
void stopServer();
void restartServer();

#ifdef __cplusplus
}
#endif

#ifdef ESP_PLATFORM
#include "webserver_esp32.h"
#endif

#endif
