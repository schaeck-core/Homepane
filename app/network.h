#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NET_MODE_DHCP,
    NET_MODE_STATIC
} net_mode_t;

typedef enum {
    NET_TYPE_NONE,
    NET_TYPE_ETHERNET,
    NET_TYPE_WIFI
} net_type_t;

typedef enum {
    NET_STATUS_DISCONNECTED,
    NET_STATUS_CONNECTING,
    NET_STATUS_CONNECTED,
    NET_STATUS_ERROR
} net_status_t;

typedef struct {
    char ip[16];
    char gateway[16];
    char netmask[16];
    char dns[16];
    net_mode_t mode;
    net_type_t type;
    net_status_t status;
    char ssid[33];   // WiFi only: network connected/connecting to ("" otherwise)
} network_info_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t auth_mode;
} network_ap_record_t;

// Network initialization and control
bool network_init(void);
bool network_ethernet_init(void);
bool network_wifi_init(const char* ssid, const char* password);

/**
 * Connect (or switch) to a WiFi network without blocking the caller.
 * Returns immediately with status = NET_STATUS_CONNECTING and info.ssid set
 * to the requested network; progress is reported via network_get_info():
 * NET_STATUS_CONNECTED once an IP was obtained, NET_STATUS_ERROR after the
 * retries are exhausted (e.g. wrong password). Safe to call while already
 * connected to another network. Does not persist anything.
 * Returns false if the WiFi co-processor is unavailable.
 */
bool network_wifi_connect_async(const char* ssid, const char* password);

/**
 * Start WiFi and connect to saved credentials non-blocking.
 * Call once at startup (after loadConfig) regardless of which app state
 * follows. Safe to call even if WiFi is already initialised.
 * Returns true if a connection attempt was kicked off, false if no
 * credentials are saved or the WiFi co-processor is unavailable.
 */
bool network_auto_connect_async(void);

void network_disconnect(void);
network_info_t network_get_info(void);
bool network_set_static_ip(const char* ip, const char* gateway, const char* netmask, const char* dns);
bool network_set_dhcp(void);
bool network_is_connected(void);
net_status_t network_get_status(void);

// WiFi scanning
// Standard-Scan über ESP-Hosted mit Event-basierter Synchronisation
int network_wifi_scan(network_ap_record_t* ap_records, uint16_t max_records);

// Vergleichs-Scan nach ESP-IDF-Beispiel (wifi/scan)
// Verwendet direkt esp_wifi_scan_start(NULL, true) und esp_wifi_scan_get_ap_records
int network_wifi_scan_example(uint16_t max_records);

// Low-Level-Test für ESP-Hosted-RPC: Verbindet sich mit dem Coprozessor
// und prüft per Heartbeat-Konfiguration, ob Transport + RPC-Core
// grundsätzlich funktionieren.
bool network_test_brookesia_rpc_alive(void);

// Zusätzlicher Low-Level-Test: sendet einen WiFi-spezifischen RPC-Call
// (esp_wifi_remote_get_mode) an den Coprozessor, um den WiFi-RPC-Pfad
// explizit zu prüfen.
bool network_test_brookesia_wifi_rpc_ping(void);

#endif // NETWORK_H
