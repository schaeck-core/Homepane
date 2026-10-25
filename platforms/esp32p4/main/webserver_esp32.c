#ifdef ESP_PLATFORM
#include <string.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "config.h"
#include "utils.h"
#include "lvgl/lvgl.h"
#include "esp_lvgl_port.h"
#include "cpp/app_facade.h"
#include "components/loading_spinner.h"
#include "webserver_esp32.h"

// Forward declare — avoid pulling in app/views/views.h just for this one
// symbol (matches how websocket_client_esp.c declared it locally too).
extern void enter_setup_state(void);
extern void initialization_pairing_failed(const char *detail);
extern void initialization_pairing_token_exists(const char *token_name);

/* The long-lived token's name in Home Assistant: "Homepane" and the IP
 * address, so the user can tell which panel a token belongs to. */
static char s_token_name[40];

static const char *TAG = "webserver_esp32";
static httpd_handle_t s_server = NULL;

extern lv_obj_t * loadingOverlay;
extern bool isLoading;

/* ── HTTP response collector for esp_http_client ───────────── */
typedef struct {
    char *buf;
    int   len;
} http_response_t;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    http_response_t *resp = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp) {
            char *tmp = realloc(resp->buf, resp->len + evt->data_len + 1);
            if (tmp) {
                resp->buf = tmp;
                memcpy(resp->buf + resp->len, evt->data, evt->data_len);
                resp->len += evt->data_len;
                resp->buf[resp->len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}


typedef struct {
    char *access_token;
    char *client_name;
} PairingRequest;

// Runs on the UI thread (posted via app_cpp_post_to_ui from oauth_handler(),
// which itself runs on an esp_http_server worker task) — HaClient/
// WebSocketConnection have no synchronization against app_cpp_poll()'s
// single-threaded pump, so construction must happen here, not on the
// calling task. See app_cpp_pairing_exchange_long_lived_token()'s doc
// comment in app_facade.h.
static void pairing_on_success(const char *long_lived_token) {
    updateConfig("haas_token", long_lived_token);
    saveConfig();
    stopServer(); /* only needed for the login; frees its stack */

    isLoading = false;
    if (lvgl_port_lock(0)) {
        if (loadingOverlay) lv_obj_del(loadingOverlay);
        loadingOverlay = NULL;
        lvgl_port_unlock();
    }

    enter_setup_state();
}

static void pairing_on_error(const char *message) {
    ESP_LOGE(TAG, "getLongLivedToken failed: %s", message);

    isLoading = false;
    if (lvgl_port_lock(0)) {
        if (loadingOverlay) lv_obj_del(loadingOverlay);
        loadingOverlay = NULL;
        /* HA answers a second token with an existing name only with
         * "Unknown error": a token of an earlier pairing that was not deleted */
        if (strcmp(message, "Unknown error") == 0) initialization_pairing_token_exists(s_token_name);
        else initialization_pairing_failed(message);
        lvgl_port_unlock();
    }
}

static void start_pairing_on_ui_thread(void *ctx_ptr) {
    PairingRequest *ctx = (PairingRequest *)ctx_ptr;
    app_cpp_pairing_exchange_long_lived_token(getConfig("haas_url"), ctx->access_token, ctx->client_name,
                                               pairing_on_success, pairing_on_error);
    free(ctx->access_token);
    free(ctx->client_name);
    free(ctx);
}

static esp_err_t oauth_handler(httpd_req_t *req) {
    char query[256];
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > sizeof(query)) qlen = sizeof(query);
    if (httpd_req_get_url_query_str(req, query, qlen) == ESP_OK) {
        char code[128];
        if (httpd_query_key_value(query, "code", code, sizeof(code)) == ESP_OK) {
            ESP_LOGI(TAG, "Received authorization code");
            if(!isLoading){
                isLoading = true;
                bool pairing_started = false;

                /* ── Create loading overlay (must hold LVGL mutex) ── */
                if (lvgl_port_lock(0)) {
                    loadingOverlay = loading_spinner_create(lv_screen_active(), NULL);
                    lvgl_port_unlock();
                }

                /* ── Exchange authorization code for tokens ─── */
                const char *haas_base = getConfig("haas_url");
                if (!haas_base) haas_base = "http://homeassistant.local:8123";

                char token_url[256];
                snprintf(token_url, sizeof(token_url), "%s/auth/token", haas_base);

                char post_data[512];
                snprintf(post_data, sizeof(post_data),
                    "grant_type=authorization_code&code=%s&client_id=http%%3A%%2F%%2F%s%%3A8989",
                    code, getLocalIp());

                http_response_t resp = { .buf = NULL, .len = 0 };

                esp_http_client_config_t config = {
                    .url = token_url,
                    .method = HTTP_METHOD_POST,
                    .event_handler = _http_event_handler,
                    .user_data = &resp,
                    .disable_auto_redirect = true,
                };

                esp_http_client_handle_t client = esp_http_client_init(&config);
                esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
                esp_http_client_set_header(client, "Accept", "application/json");
                esp_http_client_set_post_field(client, post_data, strlen(post_data));

                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK && resp.buf) {
                    int status = esp_http_client_get_status_code(client);
                    // Status only: the body contains the access + refresh tokens.
                    ESP_LOGI(TAG, "Token exchange status=%d", status);
                    // A refused exchange has no tokens, only HA's reason.
                    if (status != 200) ESP_LOGW(TAG, "Token exchange refused: %.160s", resp.buf);

                    cJSON *json = cJSON_Parse(resp.buf);
                    if (json) {
                        cJSON *accessToken  = cJSON_GetObjectItemCaseSensitive(json, "access_token");
                        cJSON *refreshToken = cJSON_GetObjectItemCaseSensitive(json, "refresh_token");

                        if (cJSON_IsString(accessToken) && accessToken->valuestring) {
                            ESP_LOGI(TAG, "Access Token received");
                            updateConfig("haas_access_token", accessToken->valuestring);
                        }
                        if (cJSON_IsString(refreshToken) && refreshToken->valuestring) {
                            ESP_LOGI(TAG, "Refresh Token received");
                            updateConfig("haas_refresh_token", refreshToken->valuestring);
                        }
                        saveConfig();

                        /* Exchange short-lived token for long-lived token via WS */
                        if (cJSON_IsString(accessToken) && accessToken->valuestring) {
                            PairingRequest *ctx = (PairingRequest *)malloc(sizeof(PairingRequest));
                            ctx->access_token = strdup(accessToken->valuestring);
                            snprintf(s_token_name, sizeof(s_token_name), "Homepane %s", getLocalIp());
                            ctx->client_name = strdup(s_token_name);
                            app_cpp_post_to_ui(start_pairing_on_ui_thread, ctx);
                            pairing_started = true;
                        }

                        cJSON_Delete(json);
                    } else {
                        ESP_LOGE(TAG, "Failed to parse token response JSON");
                    }
                } else {
                    ESP_LOGE(TAG, "Token exchange HTTP request failed: %s", esp_err_to_name(err));
                }

                free(resp.buf);
                esp_http_client_cleanup(client);

                // If the WS half was actually kicked off, pairing_on_success()/
                // pairing_on_error() own the overlay/isLoading cleanup once it
                // truly completes; otherwise (HTTP failure, unparseable
                // response, missing access_token) clean up right away.
                if (!pairing_started) {
                    isLoading = false;
                    if (lvgl_port_lock(0)) {
                        if (loadingOverlay) lv_obj_del(loadingOverlay);
                        loadingOverlay = NULL;
                        lvgl_port_unlock();
                    }
                }
            }
        }
    }

    const char *html = "OAuth2 authentication complete. You can close this window.";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req) {
    char page[512];
    const char *auth_base = getConfig("haas_url");
    if(!auth_base) auth_base = "http://homeassistant.local:8123";
    // Build simple landing page
    snprintf(page, sizeof(page),
        "<html><body><h1>OAuth2 Authentication</h1>"
        "<p>Click <a href='%s/auth/authorize?client_id=http%%3A%%2F%%2F%s%%3A8989&redirect_uri=http%%3A%%2F%%2F%s%%3A8989%%2Foauth2callback'>here</a> to authenticate with Home Assistant.</p></body></html>",
        auth_base, getLocalIp(), getLocalIp());
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* /ha: the setup screen's QR code holds this short link; the panel sends
 * the phone on to Home Assistant's login. */
static esp_err_t ha_handler(httpd_req_t *req) {
    char url[384];
    const char *auth_base = getConfig("haas_url");
    if (!auth_base || !auth_base[0]) auth_base = "http://homeassistant.local:8123";
    snprintf(url, sizeof(url),
        "%s/auth/authorize?client_id=http%%3A%%2F%%2F%s%%3A8989&redirect_uri=http%%3A%%2F%%2F%s%%3A8989%%2Foauth2callback",
        auth_base, getLocalIp(), getLocalIp());
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", url);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

bool startServer(){
    if(s_server) return true;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8989;
    /* The OAuth callback does the whole token exchange on this task: an HTTP
     * request, JSON and two config writes (LittleFS). The default 4 KB
     * overflowed and crashed the panel when the login came back (v0.2.0).
     * Internal RAM: the config writes need a stack outside PSRAM. The server
     * stops once pairing succeeded (pairing_on_success). */
    config.stack_size = 12288;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server");
        return false;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &root);

    httpd_uri_t oauth = {
        .uri = "/oauth2callback",
        .method = HTTP_GET,
        .handler = oauth_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &oauth);

    httpd_uri_t ha = {
        .uri = "/ha",
        .method = HTTP_GET,
        .handler = ha_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &ha);


    ESP_LOGI(TAG, "Server running on http://%s:%d", getLocalIp(), config.server_port);
    return true;
}

void stopServer(){
    if(s_server){
        httpd_stop(s_server);
        s_server = NULL;
    }
}

void restartServer(){
    stopServer();
    startServer();
}

#endif
