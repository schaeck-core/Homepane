#ifndef ESP_TIME_SYNC_H
#define ESP_TIME_SYNC_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Must run before DHCP starts: asks DHCP for the router's NTP server.
void esp_time_sync_init(void);

// The network is up (again): starts the sync task, or makes it ask again
// at once (another network may have another router). Any thread.
void esp_time_sync_start(void);

// Where the clock came from last (app::net::timesync::Source as int, 0 =
// not set yet); `server` gets the address that answered.
int esp_time_sync_source(char *server, size_t size);

#ifdef __cplusplus
}
#endif

#endif // ESP_TIME_SYNC_H
