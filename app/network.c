#include "network.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"
#include "esp_wifi_remote.h"
#include "esp_hosted.h"
#include "esp_wifi_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "slave_ota.h"
#include "esp_sntp.h"
#else
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

static const char *TAG = "network";

#ifdef ESP_PLATFORM
static bool s_sntp_started = false;

static void start_sntp_once(void) {
    if (s_sntp_started) return;
    s_sntp_started = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    /* Apply timezone: read from config, default = Central European Time */
    const char *tz = getConfig("timezone");
    setenv("TZ", tz ? tz : "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    ESP_LOGI(TAG, "SNTP started, TZ=%s", tz ? tz : "CET-1CEST,M3.5.0,M10.5.0/3");
}
#endif

static network_info_t s_network_info = {
    .ip = "0.0.0.0",
    .gateway = "0.0.0.0",
    .netmask = "0.0.0.0",
    .dns = "0.0.0.0",
    .mode = NET_MODE_DHCP,
    .type = NET_TYPE_NONE,
    .status = NET_STATUS_DISCONNECTED
};

#ifdef ESP_PLATFORM
static esp_netif_t *s_eth_netif = NULL;
static esp_netif_t *s_wifi_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static EventGroupHandle_t s_network_event_group = NULL;
static EventGroupHandle_t s_wifi_scan_event_group = NULL;

static bool s_wifi_core_initialized = false;
static bool s_nvs_initialized = false;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define ETH_CONNECTED_BIT  BIT2
#define ETH_FAIL_BIT       BIT3

#define WIFI_MAX_RETRY 5
static int s_wifi_retry_num = 0;

// Event-Bit für Scan-Abschluss
#define WIFI_SCAN_DONE_BIT BIT4

// Forward declarations for event handlers used in network_wifi_core_init()
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);

// Zentrale Initialisierung der WiFi-Host-Seite (ESP-Hosted, ESP32-P4)
// Orientiert sich am offiziellen Beispiel esp_hosted_wifi.c:init_wifi()
static esp_err_t network_wifi_core_init(void)
{
    if (s_wifi_core_initialized) {
        return ESP_OK;
    }

    // NVS nur einmal initialisieren
    if (!s_nvs_initialized) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        s_nvs_initialized = true;
    }

    // Netif / Event-Loop initialisieren (Mehrfachaufrufe sind erlaubt)
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Before calling esp_wifi_init() (which on ESP32-P4 goes through
     * esp_wifi_remote → esp_hosted), we must first establish the SDIO
     * transport to the ESP32-C6 co-processor.
     *
     * esp_hosted_connect_to_slave() resets the C6 via its EN pin (GPIO54,
     * CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE), initializes the SDIO card,
     * and waits up to ~20 s for the slave's INIT event.
     */
    ESP_LOGI(TAG, "Connecting to WiFi co-processor (ESP32-C6)...");
    err = esp_hosted_connect_to_slave();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi co-processor not available — WiFi is disabled. "
                 "The ESP32-C6 may need firmware flashed via UART.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "WiFi co-processor connected, initializing WiFi stack...");
    slave_ota_log_coprocessor_info();

    if (!s_wifi_netif) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
        if (!s_wifi_netif) {
            ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register event handlers BEFORE esp_wifi_start() so WIFI_EVENT_STA_START
     * is not missed. The handler checks our config for a saved SSID. */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* No modem sleep: the panel runs on mains power, and with the default
     * (WIFI_PS_MIN_MODEM) the C6 only picks up data at the access point's
     * beacons: covers came in at a few KB/s. */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_set_ps failed: %s", esp_err_to_name(err));

    /* If no SSID is saved in our config, clear any credentials stored in
     * the C6's NVS to prevent it from auto-connecting to a previous network. */
    const char *saved_ssid = getConfig("wifi_ssid");
    if (!saved_ssid || strlen(saved_ssid) == 0) {
        ESP_LOGI(TAG, "No saved SSID — clearing C6 NVS credentials to prevent auto-connect");
        wifi_config_t empty_cfg = {0};
        esp_wifi_set_config(WIFI_IF_STA, &empty_cfg);
        esp_wifi_disconnect();
    }

    s_wifi_core_initialized = true;
    return ESP_OK;
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Only auto-connect if WiFi credentials have been configured;
        // otherwise a connect attempt with empty SSID may interfere
        // with scan operations on the ESP-Hosted coprocessor.
        const char *saved_ssid = getConfig("wifi_ssid");
        if (saved_ssid && strlen(saved_ssid) > 0) {
            esp_wifi_connect();
            s_network_info.status = NET_STATUS_CONNECTING;
            ESP_LOGI(TAG, "WiFi connecting to saved SSID...");
        } else {
            ESP_LOGI(TAG, "WiFi started — no saved SSID, skipping auto-connect");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (attempt %d/%d)", s_wifi_retry_num, WIFI_MAX_RETRY);
        } else {
            if (s_network_event_group) {
                xEventGroupSetBits(s_network_event_group, WIFI_FAIL_BIT);
            }
            s_network_info.status = NET_STATUS_ERROR;
            ESP_LOGE(TAG, "Failed to connect to WiFi after %d attempts", WIFI_MAX_RETRY);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        // Scan abgeschlossen -> Bit setzen, damit network_wifi_scan weiterlaufen kann
        if (s_wifi_scan_event_group) {
            xEventGroupSetBits(s_wifi_scan_event_group, WIFI_SCAN_DONE_BIT);
        }
        ESP_LOGI(TAG, "WiFi scan done event received");
    }
}

// Event handler for Ethernet
// Ethernet is brought up after WiFi may already be connected (or connecting)
// from saved credentials. Until Ethernet actually has an address, its link
// events must not take over the shared status, or the WiFi wait on the setup
// screen sees "not WiFi" and reports a failed connection.
static bool wifi_link_active(void)
{
    return s_network_info.type == NET_TYPE_WIFI &&
           (s_network_info.status == NET_STATUS_CONNECTED || s_network_info.status == NET_STATUS_CONNECTING);
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        if (!wifi_link_active()) {
            s_network_info.status = NET_STATUS_CONNECTING;
            s_network_info.type = NET_TYPE_ETHERNET;
        }
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        if (s_network_info.type == NET_TYPE_ETHERNET) {
            s_network_info.status = NET_STATUS_DISCONNECTED;
        }
        if (s_network_event_group) {
            xEventGroupSetBits(s_network_event_group, ETH_FAIL_BIT);
        }
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

// Event handler for IP events
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        const esp_netif_ip_info_t *ip_info = &event->ip_info;

        ESP_LOGI(TAG, "Ethernet Got IP Address");
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&ip_info->ip));
        ESP_LOGI(TAG, "NETMASK:" IPSTR, IP2STR(&ip_info->netmask));
        ESP_LOGI(TAG, "GATEWAY:" IPSTR, IP2STR(&ip_info->gw));

        snprintf(s_network_info.ip, sizeof(s_network_info.ip),
                 IPSTR, IP2STR(&ip_info->ip));
        snprintf(s_network_info.gateway, sizeof(s_network_info.gateway),
                 IPSTR, IP2STR(&ip_info->gw));
        snprintf(s_network_info.netmask, sizeof(s_network_info.netmask),
                 IPSTR, IP2STR(&ip_info->netmask));

        // Get DNS server
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            snprintf(s_network_info.dns, sizeof(s_network_info.dns),
                     IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }

        s_network_info.status = NET_STATUS_CONNECTED;
        s_network_info.type = NET_TYPE_ETHERNET;
        start_sntp_once();

        if (s_network_event_group) {
            xEventGroupSetBits(s_network_event_group, ETH_CONNECTED_BIT);
        }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        const esp_netif_ip_info_t *ip_info = &event->ip_info;

        ESP_LOGI(TAG, "WiFi Got IP Address");
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&ip_info->ip));
        ESP_LOGI(TAG, "NETMASK:" IPSTR, IP2STR(&ip_info->netmask));
        ESP_LOGI(TAG, "GATEWAY:" IPSTR, IP2STR(&ip_info->gw));

        snprintf(s_network_info.ip, sizeof(s_network_info.ip),
                 IPSTR, IP2STR(&ip_info->ip));
        snprintf(s_network_info.gateway, sizeof(s_network_info.gateway),
                 IPSTR, IP2STR(&ip_info->gw));
        snprintf(s_network_info.netmask, sizeof(s_network_info.netmask),
                 IPSTR, IP2STR(&ip_info->netmask));

        // Get DNS server
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(s_wifi_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            snprintf(s_network_info.dns, sizeof(s_network_info.dns),
                     IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }

        s_network_info.status = NET_STATUS_CONNECTED;
        s_network_info.type = NET_TYPE_WIFI;
        s_wifi_retry_num = 0;
        {
            // Record which network we actually ended up on (the C6 may also
            // have auto-connected from its own stored credentials).
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                snprintf(s_network_info.ssid, sizeof(s_network_info.ssid), "%s", (const char*)ap.ssid);
            }
        }
        start_sntp_once();

        if (s_network_event_group) {
            xEventGroupSetBits(s_network_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

bool network_ethernet_init(void)
{
    ESP_LOGI(TAG, "Initializing Ethernet for ESP32-P4 with IP101...");

    // Create MAC and PHY config
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // Configure PHY (IP101) - based on Waveshare ESP32-P4-86-Panel-ETH-2RO
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = 51;  // RESET pin for IP101

    // Configure ESP32 EMAC for RMII
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = 31;   // MDC
    esp32_emac_config.smi_gpio.mdio_num = 52;  // MDIO

    // RMII interface pins (ESP32-P4 specific)
    // TXD0: GPIO34, TXD1: GPIO35
    // RXD0: GPIO30, RXD1: GPIO29
    // TX_EN: GPIO49
    // CRS_DV: GPIO28
    // REF_CLK: GPIO50

    // Create MAC and PHY instances
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);

    // Install Ethernet driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&config, &s_eth_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed");
        return false;
    }

    // Create netif for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);

    // Attach Ethernet driver to TCP/IP stack
    esp_eth_netif_glue_handle_t eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    if (esp_netif_attach(s_eth_netif, eth_glue) != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet netif attach failed");
        return false;
    }

    // Register event handlers
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler, NULL);

    // Start Ethernet driver
    if (esp_eth_start(s_eth_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet start failed");
        return false;
    }

    ESP_LOGI(TAG, "Ethernet initialization completed");
    if (!wifi_link_active()) {
        s_network_info.type = NET_TYPE_ETHERNET;
        s_network_info.status = NET_STATUS_CONNECTING;
    }
    return true;
}

bool network_wifi_init(const char* ssid, const char* password)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is required");
        return false;
    }

    ESP_LOGI(TAG, "Initializing WiFi for ESP32-P4 via ESP-Hosted (ESP32-C6)...");

    // Zentrale Host-WiFi-Initialisierung (IDF + ESP-Hosted)
    esp_err_t ret = network_wifi_core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi core init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Event-Handler für WiFi und IP (Mehrfachregistrierung ist unkritisch)
    (void)esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);

    // Event-Group für Verbindungsstatus
    if (!s_network_event_group) {
        s_network_event_group = xEventGroupCreate();
    }

    // Configure WiFi
    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Copy SSID
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    // Copy password if provided
    if (password && strlen(password) > 0) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    }

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "WiFi initialization completed");
    s_network_info.type = NET_TYPE_WIFI;
    s_network_info.status = NET_STATUS_CONNECTING;

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_network_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000)); // 30 second timeout

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", ssid);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi SSID: %s", ssid);
        return false;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return false;
    }
}

typedef struct {
    char ssid[33];
    char password[65];
} wifi_connect_req_t;

// Performs the (RPC-backed, briefly blocking) ESP-Hosted calls for
// network_wifi_connect_async() off the caller's (UI) thread.
static void wifi_connect_task(void *arg)
{
    wifi_connect_req_t *req = (wifi_connect_req_t *)arg;

    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = req->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    // Both fields may use their full size without a terminating NUL
    // (32-byte SSID, 64-byte passphrase) — wifi_config_t is zeroed above.
    memcpy(wifi_config.sta.ssid, req->ssid, strnlen(req->ssid, sizeof(wifi_config.sta.ssid)));
    memcpy(wifi_config.sta.password, req->password, strnlen(req->password, sizeof(wifi_config.sta.password)));

    // Drop any current/ongoing association first. Its DISCONNECTED event is
    // handled by wifi_event_handler(), which reconnects — with the new
    // config set right below, i.e. to the requested network.
    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
        // ESP_ERR_WIFI_CONN: a (re)connect is already in progress — fine.
        if (err == ESP_ERR_WIFI_CONN) err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect to '%s' failed to start: %s", req->ssid, esp_err_to_name(err));
        s_network_info.status = NET_STATUS_ERROR;
    } else {
        ESP_LOGI(TAG, "WiFi connecting to '%s'...", req->ssid);
    }

    memset(req->password, 0, sizeof(req->password));
    free(req);
    vTaskDelete(NULL);
}

bool network_wifi_connect_async(const char* ssid, const char* password)
{
    if (!ssid || !ssid[0]) return false;

    esp_err_t ret = network_wifi_core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi core init failed: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_connect_req_t *req = calloc(1, sizeof(*req));
    if (!req) return false;
    snprintf(req->ssid, sizeof(req->ssid), "%s", ssid);
    snprintf(req->password, sizeof(req->password), "%s", password ? password : "");

    // Reset per-attempt state *before* anything can report progress, so a
    // stale CONNECTED/ERROR from a previous network or attempt can never be
    // mistaken for the outcome of this one.
    s_wifi_retry_num = 0;
    if (s_network_event_group) xEventGroupClearBits(s_network_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    snprintf(s_network_info.ssid, sizeof(s_network_info.ssid), "%s", ssid);
    s_network_info.ip[0] = '\0';
    s_network_info.type = NET_TYPE_WIFI;
    s_network_info.status = NET_STATUS_CONNECTING;

    if (xTaskCreate(wifi_connect_task, "wifi_connect", 4096, req, 5, NULL) != pdPASS) {
        free(req);
        s_network_info.status = NET_STATUS_ERROR;
        return false;
    }
    return true;
}

int network_wifi_scan(network_ap_record_t* ap_records, uint16_t max_records)
{
    if (!ap_records || max_records == 0) {
        ESP_LOGE(TAG, "Invalid scan parameters");
        return 0;
    }

    // Sicherstellen, dass die WiFi-Host-Seite initialisiert ist
    esp_err_t ret = network_wifi_core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi core init failed for scan: %s", esp_err_to_name(ret));
        return 0;
    }

    // Event-Group für Scan-Ergebnis anlegen (einmalig)
    if (!s_wifi_scan_event_group) {
        s_wifi_scan_event_group = xEventGroupCreate();
    }

    // Event handler already registered in network_wifi_core_init() for ESP_EVENT_ANY_ID.
    // Register here as fallback in case scan is called before core_init (e.g. on retry).
    (void)esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_event_handler, NULL);

    // Brief settle delay: give the WiFi stack time to finish initialization
    // and any pending auto-connect/disconnect operations before scanning.
    vTaskDelay(pdMS_TO_TICKS(500));

    // Scan-Konfiguration
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    // Event-Bit vorher löschen
    xEventGroupClearBits(s_wifi_scan_event_group, WIFI_SCAN_DONE_BIT);

    ESP_LOGI(TAG, "Starting WiFi scan (ESP-Hosted, async)...");
    ret = esp_wifi_scan_start(&scan_cfg, false); // nicht blockierend
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(ret));
        return 0;
    }

    // Auf WIFI_EVENT_STA_SCAN_DONE warten (max. 5 Sekunden)
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_scan_event_group,
        WIFI_SCAN_DONE_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(5000));

    if (!(bits & WIFI_SCAN_DONE_BIT)) {
        ESP_LOGE(TAG, "WiFi scan timeout (no WIFI_EVENT_STA_SCAN_DONE)");
        return 0;
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(ret));
        return 0;
    }

    if (ap_count == 0) {
        ESP_LOGI(TAG, "No access points found");
        return 0;
    }

    // Wir lesen höchstens max_records in unser Zielarray
    uint16_t number = ap_count;
    if (number > max_records) {
        number = max_records;
    }

    // Temporärer Puffer für ESP-IDF WiFi-Records
    wifi_ap_record_t *temp_records = malloc(number * sizeof(wifi_ap_record_t));
    if (!temp_records) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return 0;
    }

    // Scan-Records vom ESP-Hosted-Stack abrufen
    ret = esp_wifi_scan_get_ap_records(&number, temp_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan records: %s", esp_err_to_name(ret));
        free(temp_records);
        return 0;
    }

    ESP_LOGI(TAG, "esp_wifi_scan_get_ap_records returned %u networks", number);

    // Debug: Dump first network's raw ESP-IDF structure BEFORE any copying
    if (number > 0) {
        ESP_LOGI(TAG, "=== RAW ESP-IDF DATA (first network) ===");
        ESP_LOGI(TAG, "  SSID bytes[0-7]: %02x %02x %02x %02x %02x %02x %02x %02x",
                 temp_records[0].ssid[0], temp_records[0].ssid[1],
                 temp_records[0].ssid[2], temp_records[0].ssid[3],
                 temp_records[0].ssid[4], temp_records[0].ssid[5],
                 temp_records[0].ssid[6], temp_records[0].ssid[7]);
        ESP_LOGI(TAG, "  SSID as string: '%s'", (char*)temp_records[0].ssid);
        ESP_LOGI(TAG, "  RSSI: %d dBm", temp_records[0].rssi);
        ESP_LOGI(TAG, "  Authmode: %d", temp_records[0].authmode);
        ESP_LOGI(TAG, "  Primary channel: %d", temp_records[0].primary);
        ESP_LOGI(TAG, "  BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
                 temp_records[0].bssid[0], temp_records[0].bssid[1],
                 temp_records[0].bssid[2], temp_records[0].bssid[3],
                 temp_records[0].bssid[4], temp_records[0].bssid[5]);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, &temp_records[0], sizeof(wifi_ap_record_t), ESP_LOG_INFO);
    }

    // Copy ESP-IDF records to our simplified network_ap_record_t structure
    for (int i = 0; i < number; i++) {
        // Copy SSID from uint8_t[] to char[] using memcpy
        size_t ssid_len = strnlen((const char*)temp_records[i].ssid, 32);
        memcpy(ap_records[i].ssid, temp_records[i].ssid, ssid_len);
        ap_records[i].ssid[ssid_len] = '\0';

        ap_records[i].rssi = temp_records[i].rssi;
        ap_records[i].auth_mode = temp_records[i].authmode;

        // Debug output for each network
        const char *auth_str = "Open";
        if (temp_records[i].authmode == WIFI_AUTH_WEP) auth_str = "WEP";
        else if (temp_records[i].authmode == WIFI_AUTH_WPA_PSK) auth_str = "WPA";
        else if (temp_records[i].authmode == WIFI_AUTH_WPA2_PSK) auth_str = "WPA2";
        else if (temp_records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK) auth_str = "WPA/WPA2";
        else if (temp_records[i].authmode == WIFI_AUTH_WPA3_PSK) auth_str = "WPA3";
        else if (temp_records[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK) auth_str = "WPA2/WPA3";

        ESP_LOGI(TAG, "Network %d: SSID='%s' (len=%d), RSSI=%d dBm, Auth=%s",
                 i + 1,
                 ap_records[i].ssid,
                 ssid_len,
                 ap_records[i].rssi,
                 auth_str);

        // Debug: dump raw SSID bytes
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, temp_records[i].ssid, 33, ESP_LOG_INFO);
    }

    free(temp_records);
    ESP_LOGI(TAG, "Found %u WiFi networks", number);
    return number;
}

static void print_auth_mode(int authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OPEN");
        break;
    case WIFI_AUTH_OWE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OWE");
        break;
    case WIFI_AUTH_WEP:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WEP");
        break;
    case WIFI_AUTH_WPA_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_PSK");
        break;
    case WIFI_AUTH_WPA2_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_PSK");
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
        break;
    case WIFI_AUTH_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
        break;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    case WIFI_AUTH_WPA3_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_ENTERPRISE");
        break;
#endif
    case WIFI_AUTH_WPA3_ENT_192:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENT_192");
        break;
    default:
        ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_UNKNOWN");
        break;
    }
}

static void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    switch (pairwise_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_AES_CMAC128:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_AES_CMAC128");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }

    switch (group_cipher) {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }
}

// Zweite Scan-Implementierung nach ESP-IDF Beispiel "examples/wifi/scan/main/scan.c"
// Nutzt den einfachen Blocking-Scan-Flow:
//   esp_wifi_scan_start(NULL, true);
//   esp_wifi_scan_get_ap_num(&ap_count);
//   esp_wifi_scan_get_ap_records(&number, ap_info);
// und kopiert die Ergebnisse in network_ap_record_t.
int network_wifi_scan_example(uint16_t max_records)
{
    esp_err_t ret = network_wifi_core_init();          // Initialisiert esp_hosted / esp_wifi_remote
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Remote WiFi core init failed: %s", esp_err_to_name(ret));
        return 0;
    }

    uint16_t number = max_records;
    if (number == 0) {
        return 0;
    }

    wifi_ap_record_t *ap_info = calloc(number, sizeof(wifi_ap_record_t));
    if (!ap_info) {
        ESP_LOGE(TAG, "No mem for ap_info");
        return 0;
    }

    ESP_LOGI(TAG, "Starting REMOTE WiFi scan (blocking on P4)...");
    // WICHTIG: hier **Remote**‑Scan‑API verwenden, nicht esp_wifi_scan_start
    ret = esp_wifi_remote_scan_start(NULL, true);             // Wrapper, der RPC zum C6 macht
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "remote_scan_start failed: %s", esp_err_to_name(ret));
        free(ap_info);
        return 0;
    }

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_remote_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "APs found remotely = %u", ap_count);

    if (ap_count == 0) {
        free(ap_info);
        return 0;
    }

    if (ap_count < number) {
        number = ap_count;
    }

    ESP_ERROR_CHECK(esp_wifi_remote_scan_get_ap_records(&number, ap_info));

    ESP_LOGI(TAG, "APs copied to buffer = %u", number);
    for (int i = 0; i < number; i++) {
        ESP_LOGI(TAG, "SSID    \t%s", (char *)ap_info[i].ssid);
        ESP_LOGI(TAG, "RSSI    \t%d", ap_info[i].rssi);
        print_auth_mode(ap_info[i].authmode);
        if (ap_info[i].authmode != WIFI_AUTH_WEP) {
            print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
        }
        ESP_LOGI(TAG, "Channel \t%d", ap_info[i].primary);
    }

    free(ap_info);
    return number;
}

bool network_set_static_ip(const char* ip, const char* gateway, const char* netmask, const char* dns)
{
    esp_netif_t *netif = s_eth_netif ? s_eth_netif : s_wifi_netif;
    if (!netif) {
        ESP_LOGE(TAG, "No network interface available");
        return false;
    }

    // Stop DHCP client
    esp_netif_dhcpc_stop(netif);

    // Set static IP
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));

    // Parse IP addresses using esp_netif API
    esp_netif_str_to_ip4(ip, &ip_info.ip);
    esp_netif_str_to_ip4(gateway, &ip_info.gw);
    esp_netif_str_to_ip4(netmask, &ip_info.netmask);

    if (esp_netif_set_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set static IP");
        return false;
    }

    // Set DNS server
    if (dns && strlen(dns) > 0) {
        esp_netif_dns_info_t dns_info;
        esp_netif_str_to_ip4(dns, &dns_info.ip.u_addr.ip4);
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    }

    s_network_info.mode = NET_MODE_STATIC;
    strncpy(s_network_info.ip, ip, sizeof(s_network_info.ip) - 1);
    strncpy(s_network_info.gateway, gateway, sizeof(s_network_info.gateway) - 1);
    strncpy(s_network_info.netmask, netmask, sizeof(s_network_info.netmask) - 1);
    if (dns) {
        strncpy(s_network_info.dns, dns, sizeof(s_network_info.dns) - 1);
    }

    ESP_LOGI(TAG, "Static IP configured: %s", ip);
    return true;
}

bool network_set_dhcp(void)
{
    esp_netif_t *netif = s_eth_netif ? s_eth_netif : s_wifi_netif;
    if (!netif) {
        ESP_LOGE(TAG, "No network interface available");
        return false;
    }

    if (esp_netif_dhcpc_start(netif) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DHCP client");
        return false;
    }

    s_network_info.mode = NET_MODE_DHCP;
    ESP_LOGI(TAG, "DHCP client started");
    return true;
}

#else
// Desktop implementation
bool network_ethernet_init(void)
{
    // On desktop, we just read the current network info
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        return false;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        int family = ifa->ifa_addr->sa_family;

        // Look for IPv4 address on non-loopback interface
        if (family == AF_INET && strcmp(ifa->ifa_name, "lo") != 0) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, s_network_info.ip, sizeof(s_network_info.ip));

            if (ifa->ifa_netmask) {
                struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;
                inet_ntop(AF_INET, &netmask->sin_addr, s_network_info.netmask, sizeof(s_network_info.netmask));
            }

            s_network_info.type = NET_TYPE_ETHERNET;
            s_network_info.status = NET_STATUS_CONNECTED;
            s_network_info.mode = NET_MODE_DHCP;
            strcpy(s_network_info.gateway, "-");  // not known here
            strcpy(s_network_info.dns, "8.8.8.8");  // Default assumption
            break;
        }
    }

    freeifaddrs(ifaddr);
    return s_network_info.status == NET_STATUS_CONNECTED;
}

bool network_wifi_init(const char* ssid, const char* password)
{
    // Not supported on desktop
    return false;
}

bool network_wifi_connect_async(const char* ssid, const char* password)
{
    (void)ssid; (void)password;
    // Not supported on desktop
    return false;
}

bool network_set_static_ip(const char* ip, const char* gateway, const char* netmask, const char* dns)
{
    // Not supported on desktop
    return false;
}

bool network_set_dhcp(void)
{
    // Always DHCP on desktop
    return true;
}
#endif

bool network_init(void)
{
#ifdef ESP_PLATFORM
    // Create event group (once — network_setup_render() calls this every
    // time the setup screen is shown).
    if (!s_network_event_group) s_network_event_group = xEventGroupCreate();
#endif

    // Don't clobber a connection that is already up or being brought up
    // (WiFi is started at boot by network_auto_connect_async(), before any
    // screen calls network_init()).
    if (s_network_info.status != NET_STATUS_CONNECTED && s_network_info.status != NET_STATUS_CONNECTING) {
        s_network_info.status = NET_STATUS_DISCONNECTED;
        s_network_info.type = NET_TYPE_NONE;
    }
    s_network_info.mode = NET_MODE_DHCP;

    return true;
}

#ifdef ESP_PLATFORM
bool network_auto_connect_async(void)
{
    const char *ssid = getConfig("wifi_ssid");
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGI(TAG, "network_auto_connect: no saved SSID — skipping");
        return false;
    }
    const char *pass = getConfig("wifi_password");

    if (!s_network_event_group)
        s_network_event_group = xEventGroupCreate();

    /* esp_hosted_connect_to_slave() is called inside network_wifi_core_init()
     * and blocks until the SDIO transport is up (a few seconds after boot). */
    esp_err_t ret = network_wifi_core_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "network_auto_connect: WiFi core init failed (%s) — no WiFi",
                 esp_err_to_name(ret));
        return false;
    }

    /* Configure credentials so esp_wifi_connect() uses the right AP. */
    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable    = true;
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (pass && strlen(pass) > 0)
        strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
    snprintf(s_network_info.ssid, sizeof(s_network_info.ssid), "%s", ssid);
    s_network_info.type = NET_TYPE_WIFI;
    s_network_info.status = NET_STATUS_CONNECTING;
    ESP_LOGI(TAG, "network_auto_connect: connecting to '%s' (async)", ssid);
    return true;
}
#else
bool network_auto_connect_async(void)
{
    return false; /* not supported on desktop */
}
#endif

void network_disconnect(void)
{
#ifdef ESP_PLATFORM
    if (s_eth_handle) {
        esp_eth_stop(s_eth_handle);
    }
#endif
    s_network_info.status = NET_STATUS_DISCONNECTED;
    s_network_info.type = NET_TYPE_NONE;
}

network_info_t network_get_info(void)
{
    return s_network_info;
}

bool network_is_connected(void)
{
    return s_network_info.status == NET_STATUS_CONNECTED;
}

net_status_t network_get_status(void)
{
    return s_network_info.status;
}


// Low-Level-Test, ob der ESP-Hosted-RPC-Pfad grundsätzlich funktioniert.
// Anstatt einen (in Brookesia nicht implementierten) FW-Version-Request zu
// verwenden, schicken wir einen Heartbeat-Konfigurations-Request, der im
// ESP-Hosted-Stack immer vorhanden ist. Wenn dieser erfolgreich ist, wissen
// wir, dass Transport + RPC-Core sauber laufen und der Slave antwortet.
bool network_test_brookesia_rpc_alive(void)
{
    #ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "[brookesia-test] Testing ESP-Hosted RPC via heartbeat config...");

    // Sicherstellen, dass der Transport initialisiert ist
    // esp_hosted_init() wird über den Konstruktor in
    // port_esp_hosted_host_init.c bereits einmalig aufgerufen.
    // Hier stellen wir nur noch sicher, dass die Verbindung zum Slave
    // hergestellt ist.

    int ret = esp_hosted_connect_to_slave();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[brookesia-test] esp_hosted_connect_to_slave failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Heartbeat-Konfiguration triggert einen definierten RPC-Request
    // (RPC_ID__Req_ConfigHeartbeat über rpc_config_heartbeat()).
    // Schlägt dieser fehl, ist der generische RPC-Pfad gestört oder der
    // Slave-Firmware fehlen die erwarteten Handler.
    extern int rpc_config_heartbeat(void);

    int hb_ret = rpc_config_heartbeat();
    if (hb_ret == ESP_OK) {
        ESP_LOGI(TAG, "[brookesia-test] ✅ RPC heartbeat config succeeded");
        return true;
    }

    ESP_LOGE(TAG, "[brookesia-test] ❌ RPC heartbeat config failed (ret=%d)", hb_ret);
    #endif
    return false;
}

// Zusätzlicher Test, der einen WiFi-spezifischen RPC-Call an den C6 schickt.
// Hier wird bewusst eine einfache, nicht-destruktive Operation gewählt:
// esp_wifi_remote_get_mode(). Wenn dieser Aufruf erfolgreich ist, wissen
// wir, dass nicht nur der generische RPC-Core (Heartbeat), sondern auch der
// WiFi-spezifische RPC-Pfad (WiFi-Service im Brookesia-Firmwareteil) aktiv ist.
bool network_test_brookesia_wifi_rpc_ping(void)
{
    #ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "[brookesia-test] WiFi RPC ping via esp_wifi_remote_get_mode()...");

    int ret = esp_hosted_connect_to_slave();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[brookesia-test] esp_hosted_connect_to_slave failed: %s", esp_err_to_name(ret));
        return false;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[brookesia-test] ✅ WiFi RPC ping OK, remote mode=%d", (int)mode);
        return true;
    }

    ESP_LOGE(TAG, "[brookesia-test] ❌ WiFi RPC ping failed: %s", esp_err_to_name(err));
    #endif
    return false;
}
