#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libwebsockets.h>
#include "uthash.h"
#include "config.h"
#include "utils.h"
#include "websocket_client.h"

typedef struct PendingMsg {
    char *payload;          /* final JSON string with id embedded */
    int id;                 /* assigned id */
    callback_t cb;          /* optional callback */
    struct PendingMsg *next;
} PendingMsg;

typedef struct CallbackEntryLocal {
    int id;
    callback_t callback;
    UT_hash_handle hh;
} CallbackEntryLocal;

struct WsClient {
    struct lws *wsi;
    struct lws_context *context;
    char *url; // ws://host:port/path
    callback_t on_success;
    ws_error_callback_t on_error;
    bool connected;
    struct WsClient *next;
    char name[64];
    bool ha_mode; // if true, perform HA auth handshake and fire on_success on auth_ok
    /* Per-client monotonically increasing id counter (starts at 1, resets on reconnect) */
    int counter;
    /* Per-client callback maps (results + subscriptions) */
    CallbackEntryLocal *cb_map;        /* result callbacks keyed by id */
    CallbackEntryLocal *sub_map;       /* subscription callbacks keyed by id */
    /* FIFO queue head/tail for outgoing messages to preserve ordering */
    PendingMsg *q_head;
    PendingMsg *q_tail;
};

static WsClient *clients_head = NULL;
// name -> client map
typedef struct {
    char name[64];
    WsClient *client;
    UT_hash_handle hh;
} ClientByName;
static ClientByName *clients_by_name = NULL;

// Helper to add client to list
static void ws_add_client(WsClient *c){ c->next = clients_head; clients_head = c; }

// Helper to remove and free client
static void ws_free_client(WsClient *c){
    if(!c) return;
    if(c->context){ lws_context_destroy(c->context); }
    free(c->url);
    free(c);
}

static void ws_remove_client(WsClient *c){
    WsClient **pp = &clients_head;
    while(*pp){
        if(*pp == c){ *pp = c->next; break; }
        pp = &(*pp)->next;
    }
    ws_free_client(c);
}

static void ws_index_client_name(WsClient *c){
    if(!c || !c->name[0]) return;
    ClientByName *e = NULL;
    HASH_FIND_STR(clients_by_name, c->name, e);
    if(!e){
        e = (ClientByName*)calloc(1, sizeof(ClientByName));
        strncpy(e->name, c->name, sizeof(e->name)-1);
        e->client = c;
        HASH_ADD_STR(clients_by_name, name, e);
    } else {
        e->client = c;
    }
}

static void ws_unindex_client_name(WsClient *c){
    if(!c || !c->name[0]) return;
    ClientByName *e = NULL;
    HASH_FIND_STR(clients_by_name, c->name, e);
    if(e){ HASH_DEL(clients_by_name, e); free(e); }
}

static void client_add_callback(WsClient *client, int id, callback_t cb){
    if(!client || !cb) return;
    CallbackEntryLocal *e = malloc(sizeof(CallbackEntryLocal));
    e->id = id; e->callback = cb; HASH_ADD_INT(client->cb_map, id, e);
}

static bool client_has_callback(WsClient *client, int id){
    if(!client) return false; CallbackEntryLocal *e=NULL; HASH_FIND_INT(client->cb_map,&id,e); return e!=NULL; }

static void client_call_callback(WsClient *client, int id, cJSON *json){
    if(!client) return; CallbackEntryLocal *e=NULL; HASH_FIND_INT(client->cb_map,&id,e); if(e){ e->callback(json); HASH_DEL(client->cb_map,e); free(e);} }

static bool client_has_subscription(WsClient *client, int id){ if(!client) return false; CallbackEntryLocal *e=NULL; HASH_FIND_INT(client->sub_map,&id,e); return e!=NULL; }

static void client_call_subscription(WsClient *client, int id, cJSON *json){ if(!client) return; CallbackEntryLocal *e=NULL; HASH_FIND_INT(client->sub_map,&id,e); if(e){ e->callback(json); /* keep subscription persistent? For HA events keep it, so do not delete */ } }

static void enqueue_message(WsClient *client, char *final_json, int id, callback_t cb){
    PendingMsg *pm = malloc(sizeof(PendingMsg)); pm->payload = final_json; pm->id=id; pm->cb=cb; pm->next=NULL;
    if(!client->q_tail){ client->q_head = client->q_tail = pm; }
    else { client->q_tail->next = pm; client->q_tail = pm; }
    fprintf(stderr, "[WS][QUEUE][%s] enqueue id=%d size=%zu head=%p tail=%p\n", client->name, id, strlen(final_json), (void*)client->q_head, (void*)client->q_tail);
    lws_callback_on_writable(client->wsi);
}

int ws_send_message(WsClient *client, char* msg, callback_t callback){
    if(!client || !client->wsi) return -1;
    fprintf(stderr, "[WS][SENDREQ][%s] raw=%s queue_empty=%d\n", client->name, msg?msg:"(null)", client->q_head?0:1);
    cJSON *json = cJSON_Parse(msg);
    if(!json){ fprintf(stderr, "[WS][SENDREQ][%s] parse failed\n", client->name); return -1; }
    int id = client->counter++;
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(json, "id");
    if(cJSON_IsNumber(id_item)){ /* user provided id; ensure monotonic */
        if(id_item->valueint <= (client->counter-1)){ /* provided id must be >= previous id+1 */
            id = id_item->valueint; client->counter = id + 1;
        } else { cJSON_SetIntValue(id_item, id); }
    } else {
        if(client->ha_mode){
            /* In HA mode, ids are strictly controlled by us; override any user id */
            cJSON_AddNumberToObject(json, "id", id);
        } else {
            /* Non-HA mode, user id is allowed; ensure monotonic */
            cJSON_AddNumberToObject(json, "message_id", id);
        }
    }
    cJSON *mid_item = cJSON_GetObjectItemCaseSensitive(json, "message_id");
    if(cJSON_IsString(mid_item)){ char buf[32]; snprintf(buf,sizeof(buf),"%d", id); cJSON_SetValuestring(mid_item, buf); }
    char *out = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if(!out){ fprintf(stderr, "[WS][SENDREQ][%s] print failed\n", client->name); return -1; }
    enqueue_message(client, out, id, callback);
    return id;
}

static WsClient* find_client_by_wsi(struct lws *wsi_local){
    for(WsClient *c = clients_head; c; c = c->next){
        if(c->wsi == wsi_local) return c;
    }
    return NULL;
}

// Protocol callback for multi-client connections
static int callback_ws_multi(struct lws *wsi_local, enum lws_callback_reasons reason, void *user, void *in, size_t len){
    WsClient *client = find_client_by_wsi(wsi_local);

    switch(reason){
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            if(client){
                client->connected = true;
                client->counter = 1; /* reset per-client id sequence */
                fprintf(stderr, "[WS][EVT][%s] CLIENT_ESTABLISHED ha_mode=%d\n", client->name, client->ha_mode);
                // For HA, wait for auth_ok before signaling success
                if(!client->ha_mode){ if(client->on_success) client->on_success(NULL); }
            }
            break;
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            fprintf(stderr, "[WS][EVT][%s] CONNECTION_ERROR\n", client?client->name:"(unknown)");
            if(client && client->on_error) client->on_error();
            break;
        case LWS_CALLBACK_CLOSED:
            if(client){ client->connected = false; fprintf(stderr, "[WS][EVT][%s] CLOSED\n", client->name); }
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Try to parse and route using existing global handlers
            cJSON *json = cJSON_Parse((char*)in);
            if(json){
                // Home Assistant handshake and routing
                cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
                if (client->ha_mode && cJSON_IsString(type) && type->valuestring){
                    fprintf(stderr, "[WS][RX][%s] type=%s len=%zu\n", client?client->name:"(unknown)", type->valuestring, len);
                    if(client && client->ha_mode){
                        if(strcmp(type->valuestring, "auth_required") == 0){
                            fprintf(stderr, "[WS][AUTH][%s] auth_required -> sending token\n", client->name);
                            // send auth token
                            const char *token = getConfig("haas_token");
                            if(token && strlen(token) > 0){
                                char *auth;
                                if(asprintf(&auth, "%s%s%s", "{\"type\":\"auth\",\"access_token\":\"", token, "\"}") != -1){
                                    size_t alen = strlen(auth);
                                    unsigned char *abuf = malloc(LWS_PRE + alen);
                                    memcpy(&abuf[LWS_PRE], auth, alen);
                                    lws_write(client->wsi, &abuf[LWS_PRE], alen, LWS_WRITE_TEXT);
                                    free(abuf);
                                    free(auth);
                                }
                            }
                        } else if(strcmp(type->valuestring, "auth_ok") == 0){
                            fprintf(stderr, "[WS][AUTH][%s] auth_ok\n", client->name);
                            // Auth complete; now signal success
                            if(client->on_success) client->on_success(NULL);
                        } else if(strcmp(type->valuestring, "auth_invalid") == 0){
                            fprintf(stderr, "[WS][AUTH][%s] auth_invalid\n", client->name);
                            if(client->on_error) client->on_error();
                        }
                    }
                    // Home Assistant style routing by { type, id }
                    if(strcmp(type->valuestring, "result") == 0){
                        cJSON *messageId = cJSON_GetObjectItem(json, "id");
                        int mid = messageId?messageId->valueint:-1;
                        fprintf(stderr, "[WS][RX][%s] CLIENT_HAMODE ha_mode=%d\n", client->name, client->ha_mode);
                        fprintf(stderr, "[WS][RX][%s] result id=%d hasCb=%d\n", client?client->name:"(unknown)", mid, client?client_has_callback(client, mid):0);
                        /* Dump full result payload (success / error) for diagnostics */
                        cJSON *success = cJSON_GetObjectItemCaseSensitive(json, "success");
                        cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
                        char *dump = cJSON_PrintUnformatted(json);
                        if(dump){
                            fprintf(stderr, "[WS][RX][%s][RESULT] raw=%s\n", client?client->name:"(unknown)", dump);
                            free(dump);
                        }
                        if(cJSON_IsBool(success)){
                            fprintf(stderr, "[WS][RX][%s][RESULT] success=%s\n", client?client->name:"(unknown)", success->type==cJSON_True?"true":"false");
                        }
                        if(error && cJSON_IsObject(error)){
                            cJSON *ecode = cJSON_GetObjectItemCaseSensitive(error, "code");
                            cJSON *emsg = cJSON_GetObjectItemCaseSensitive(error, "message");
                            fprintf(stderr, "[WS][RX][%s][RESULT][ERROR] code=%s msg=%s\n", client?client->name:"(unknown)", ecode&&cJSON_IsString(ecode)?ecode->valuestring:"(none)", emsg&&cJSON_IsString(emsg)?emsg->valuestring:"(none)");
                        }
                        if(client && messageId && client_has_callback(client, mid)){
                            client_call_callback(client, mid, json);
                        }
                    }
                    if(strcmp(type->valuestring, "event") == 0){
                        cJSON *messageId = cJSON_GetObjectItem(json, "id");
                        int mid = messageId?messageId->valueint:-1;
                        fprintf(stderr, "[WS][RX][%s] event id=%d hasSub=%d\n", client?client->name:"(unknown)", mid, client?client_has_subscription(client, mid):0);
                        if(client && messageId && client_has_subscription(client, mid)){
                            client_call_subscription(client, mid, json);
                        }
                    }
                }
                // Music Assistant style routing by { message_id }
                cJSON *mid = cJSON_GetObjectItem(json, "message_id");
                if(cJSON_IsNumber(mid)){
                    int id = mid->valueint;
                    if(client_has_callback(client, id)){
                        client_call_callback(client, id, json);
                    }
                }
                cJSON_Delete(json);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            while(client && client->q_head){
                PendingMsg *pm = client->q_head;
                size_t len_payload = strlen(pm->payload);
                unsigned char *buf = malloc(LWS_PRE + len_payload);
                memcpy(&buf[LWS_PRE], pm->payload, len_payload);
                fprintf(stderr, "[WS][WRITE][%s][%s] sending id=%d bytes=%zu\n", client->name, client->url, pm->id, len_payload);
                int n = lws_write(client->wsi, &buf[LWS_PRE], len_payload, LWS_WRITE_TEXT);
                if(n < (int)len_payload){ fprintf(stderr, "[WS][WRITE][%s] partial/failed (%d/%zu)\n", client->name, n, len_payload); }
                if(pm->cb){ client_add_callback(client, pm->id, pm->cb); }
                printf("Sent message (multi): %s \n", pm->payload);
                free(buf);
                free(pm->payload);
                client->q_head = pm->next;
                if(!client->q_head) client->q_tail = NULL;
                free(pm);
                /* If write was partial, break to retry later */
                if(n < (int)len_payload) break;
            }
            if(client && client->q_head){ lws_callback_on_writable(client->wsi); }
            break;
        }
        default:
            break;
    }
    return 0;
}


static void parse_ws_url(const char *url, char **host, int *port, const char **path){
    // very small parser: ws://host:port/path
    *host = NULL; *port = 80; *path = "/ws";
    if(!url) return;
    const char *p = strstr(url, "://");
    const char *h = p ? p+3 : url;
    const char *colon = strchr(h, ':');
    const char *slash = strchr(h, '/');
    if(!slash) slash = h + strlen(h);
    size_t host_len = (colon && colon < slash) ? (size_t)(colon - h) : (size_t)(slash - h);
    *host = (char*)malloc(host_len+1);
    memcpy(*host, h, host_len); (*host)[host_len] = '\0';
    if(colon && colon < slash){ *port = atoi(colon+1); }
    if(*slash) *path = slash;
}

WsClient* ws_create_connection_url(const char *ws_url, callback_t on_success, ws_error_callback_t on_error){
    struct lws_context_creation_info info; memset(&info, 0, sizeof(info));
    struct lws_client_connect_info i; memset(&i, 0, sizeof(i));

    struct lws_protocols protocols[] = {
        { "http", callback_ws_multi, 0, 524288 },
        { NULL, NULL, 0, 0 }
    };
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;

    struct lws_context *ctx = lws_create_context(&info);
    if(!ctx){ if(on_error) on_error(); return NULL; }

    char *host = NULL; int port = 80; const char *path = "/ws";
    parse_ws_url(ws_url, &host, &port, &path);

    i.context = ctx;
    i.address = host;
    i.port = port;
    i.path = path;
    i.host = i.address;
    i.origin = i.address;
    i.protocol = protocols[0].name;
    i.ssl_connection = 0;
    i.pwsi = NULL;

    struct lws *w = lws_client_connect_via_info(&i);
    if(!w){ lws_context_destroy(ctx); free(host); if(on_error) on_error(); return NULL; }

    WsClient *c = (WsClient*)calloc(1, sizeof(WsClient));
    c->context = ctx; c->wsi = w; c->url = strdup(ws_url); c->on_success = on_success; c->on_error = on_error; c->connected=false;
    c->counter = 1; c->cb_map=NULL; c->sub_map=NULL; c->q_head=NULL; c->q_tail=NULL; c->ha_mode=false;
    ws_add_client(c);
    free(host);
    return c;
}

WsClient* ws_create_connection_url_named(const char *name, const char *ws_url, callback_t on_success, ws_error_callback_t on_error){
    WsClient *c = ws_create_connection_url(ws_url, on_success, on_error);
    if(c && name){
        strncpy(c->name, name, sizeof(c->name)-1);
        ws_index_client_name(c);
    }
    return c;
}

void ws_close(WsClient *client){
    if(!client) return;
    ws_unindex_client_name(client);
    ws_remove_client(client);
}

void ws_service_all(){
    for(WsClient *c = clients_head; c; c = c->next){
        if(c->context){
            lws_service(c->context, -1);
        }
    }
}

WsClient* ws_get_client_by_name(const char *name){
    if(!name) return NULL;
    ClientByName *e = NULL;
    HASH_FIND_STR(clients_by_name, name, e);
    return e ? e->client : NULL;
}

int ws_send_message_named(const char *name, char* msg, callback_t callback){
    WsClient *c = ws_get_client_by_name(name);
    if(!c){ fprintf(stderr, "[WS][SEND][%s] no client found. msg=%s\n", name?name:"(null)", msg?msg:"(null)"); return -1; }
    if(!c->connected){ fprintf(stderr, "[WS][SEND][%s] not connected yet; will send after establish\n", name); }
    return ws_send_message(c, msg, callback);
}

