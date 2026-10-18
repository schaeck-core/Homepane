#pragma once
#ifdef ESP_PLATFORM
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool startServer();
void stopServer();
void restartServer();

#ifdef __cplusplus
}
#endif

#endif
