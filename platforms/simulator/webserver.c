#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifndef ESP_PLATFORM
#include <microhttpd.h>
#endif
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "lvgl/lvgl.h"
#include "webserver.h"
#include "utils.h"
#include "config.h"
#include "websocket_client.h"
#include "cpp/app_facade.h"
#include "views/views.h"

struct ResponseData {
    char *payload;
    size_t size;
};

#ifndef ESP_PLATFORM
struct MHD_Daemon *webServerDaemon = NULL;
#endif

static size_t writeResponseCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;                             /* calculate buffer size */
    struct ResponseData *p = (struct ResponseData *) userp;   /* cast pointer to fetch struct */

    /* expand buffer */
    p->payload = (char *) realloc(p->payload, p->size + realsize + 1);

    /* check buffer */
    if (p->payload == NULL) {
      /* this isn't good */
      fprintf(stderr, "ERROR: Failed to expand buffer in curl_callback");
      /* free buffer */
      free(p->payload);
      /* return */
      return -1;
    }

    /* copy contents to buffer */
    memcpy(&(p->payload[p->size]), contents, realsize);

    /* set new buffer size */
    p->size += realsize;

    /* ensure null termination */
    p->payload[p->size] = 0;

    /* return size */
    return realsize;
}

#ifndef ESP_PLATFORM
typedef struct {
    char *access_token;
    char *client_name;
} PairingRequest;

// Runs on the UI thread (posted via app_cpp_post_to_ui from
// handleOAuthRedirect(), which itself runs on an MHD worker thread) —
// HaClient/WebSocketConnection have no synchronization against
// app_cpp_poll()'s single-threaded pump, so construction must happen here,
// not on the calling thread. See app_cpp_pairing_exchange_long_lived_token()'s
// doc comment in app_facade.h.
static void pairing_on_success(const char *long_lived_token) {
    updateConfig("haas_token", long_lived_token);
    saveConfig();

    isLoading = false;
    if (loadingOverlay) lv_obj_del(loadingOverlay);
    loadingOverlay = NULL;

    enter_setup_state();
}

static void pairing_on_error(const char *message) {
    fprintf(stderr, "getLongLivedToken failed: %s\n", message);

    isLoading = false;
    if (loadingOverlay) lv_obj_del(loadingOverlay);
    loadingOverlay = NULL;
    initialization_pairing_failed(message);
}

static void start_pairing_on_ui_thread(void *ctx_ptr) {
    PairingRequest *ctx = (PairingRequest *)ctx_ptr;
    app_cpp_pairing_exchange_long_lived_token(getConfig("haas_url"), ctx->access_token, ctx->client_name,
                                               pairing_on_success, pairing_on_error);
    free(ctx->access_token);
    free(ctx->client_name);
    free(ctx);
}

static enum MHD_Result handleOAuthRedirect(struct MHD_Connection *connection, const char *url) {
    const char *authorization_code = NULL;

    // Parse the URL to get the authorization code
    authorization_code = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "code");

    if (authorization_code) {
        printf("Received authorization code\n");

        if(!isLoading){
            isLoading = true;

            loadingOverlay = lv_obj_create(lv_screen_active());
            lv_obj_remove_style_all(loadingOverlay);  // No borders/padding
            lv_obj_set_size(loadingOverlay, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(loadingOverlay, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(loadingOverlay, LV_OPA_70, 0);

            lv_obj_t * spinner = lv_spinner_create(loadingOverlay);
            lv_obj_set_size(spinner, 100, 100);
            lv_obj_center(spinner);
            lv_spinner_set_anim_params(spinner, 2000, 200);
            //lv_obj_set_style_arc_color(spinner, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_INDICATOR);

            char* data;
            char* haas_url;
            bool pairing_started = false;

            asprintf(&data, "%s%s%s%s%s", "grant_type=authorization_code&code=", authorization_code, "&client_id=http%3A%2F%2F", getLocalIp(), "%3A8989");
            asprintf(&haas_url, "%s%s", getConfig("haas_url"), "/auth/token");

            struct ResponseData response;
            CURL *curl = curl_easy_init();
            if(curl) {
                curl_easy_setopt(curl, CURLOPT_URL, haas_url);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResponseCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

                // Optional: Set headers
                struct curl_slist *headers = NULL;
                headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
                headers = curl_slist_append(headers, "Accept: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                } else {
                    printf("Received JSON:\n%s\n", response.payload);

                    // Parse the JSON response
                    cJSON *json = cJSON_Parse(response.payload);
                    if (json == NULL) {
                        fprintf(stderr, "Failed to parse JSON.\n");
                    } else {
                        // Example: extract "access_token"
                        cJSON *accessToken = cJSON_GetObjectItemCaseSensitive(json, "access_token");
                        if (cJSON_IsString(accessToken) && (accessToken->valuestring != NULL)) {
                            printf("Access token received\n");
                            updateConfig("haas_access_token", accessToken->valuestring);
                        } else {
                            printf("No access_token found in JSON.\n");
                        }

                        cJSON *refreshToken = cJSON_GetObjectItemCaseSensitive(json, "refresh_token");
                        if (cJSON_IsString(refreshToken) && (refreshToken->valuestring != NULL)) {
                            printf("Refresh token received\n");
                            updateConfig("haas_refresh_token", refreshToken->valuestring);
                        } else {
                            printf("No refresh_token found in JSON.\n");
                        }

                        if (cJSON_IsString(accessToken) && accessToken->valuestring) {
                            PairingRequest *ctx = (PairingRequest *)malloc(sizeof(PairingRequest));
                            ctx->access_token = strdup(accessToken->valuestring);
                            asprintf(&ctx->client_name, "LVGL %s", getLocalIp());
                            app_cpp_post_to_ui(start_pairing_on_ui_thread, ctx);
                            pairing_started = true;
                        }

                        cJSON_Delete(json);

                        saveConfig();
                    }
                }

                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
                free(response.payload);
            }

            free(data);
            free(haas_url);

            // If the WS half was actually kicked off, pairing_on_success()/
            // pairing_on_error() own the overlay/isLoading cleanup once it
            // truly completes; otherwise (curl failure, unparseable
            // response, missing access_token) clean up right away.
            if (!pairing_started) {
                isLoading = false;
                if (loadingOverlay) lv_obj_del(loadingOverlay);
                loadingOverlay = NULL;
            }
        }
    }

    // Send a response to the user, indicating the OAuth2 process is complete
    const char *response = "OAuth2 authentication complete. You can close this window.";
    struct MHD_Response *mhd_response = MHD_create_response_from_buffer_static(strlen(response), (void*)response);
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, mhd_response);
    MHD_destroy_response(mhd_response);

    return ret;
    }
#endif

#ifndef ESP_PLATFORM
static enum MHD_Result handleRoutes(void *cls,
                                            struct MHD_Connection *connection,
                                            const char *url,
                                            const char *method,
                                            const char *version,
                                            const char *upload_data,
                                            size_t *upload_data_size,
                                            void **req_cls)
{
    if (strcmp(url, "/oauth2callback") == 0)
    {
        return handleOAuthRedirect(connection, url);
    }
    enum MHD_Result ret;


    // Return a basic HTML page with a link to the OAuth2 authentication page
    const char *page = "<html><body><h1>OAuth2 Authentication</h1>"
                       "<p>Click <a href='" AUTHORIZATION_CODE_URL "?client_id=" CLIENT_ID "'>here</a> to authenticate with Home Assistant.</p></body></html>";

    struct MHD_Response *response = MHD_create_response_from_buffer_static(strlen(page), (void *)page);
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

    return ret;
}
#endif

bool startServer()
{
    #ifndef ESP_PLATFORM
    if (webServerDaemon) return true; // the setup can come back to the login step
    webServerDaemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT, NULL, NULL,
                              &handleRoutes, NULL, MHD_OPTION_END);
    if (webServerDaemon == NULL)
    {
        fprintf(stderr, "Error starting server\n");
        return false;
    }
    printf("Server running on http://localhost:%d\n", PORT);
    return true;
    #else
    extern bool startServer();
    return startServer();
    #endif
}

void stopServer(){
    #ifndef ESP_PLATFORM
    MHD_stop_daemon(webServerDaemon);
    webServerDaemon = NULL;
    #else
    extern void stopServer();
    stopServer();
    #endif
}

void restartServer(){}
// On ESP32, webserver_esp32.c provides restartServer()
