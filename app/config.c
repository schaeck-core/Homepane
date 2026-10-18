#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include "config.h"

Config *config = NULL;
#ifdef ESP_PLATFORM
static const char *s_config_path_override = NULL;
void set_config_path(const char *path){ s_config_path_override = path; }
const char *get_config_path(void){ return s_config_path_override ? s_config_path_override : CONFIG_FILE_PATH; }
#endif

char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

bool configExists() {
#ifdef ESP_PLATFORM
    const char *path = get_config_path();
#else
    const char *path = CONFIG_FILE_PATH;
#endif
    // stat(), not access(): the LittleFS VFS on the ESP32 implements no
    // access(), which made every boot without SD card recreate the config.
    struct stat st;
    return stat(path, &st) == 0;
}

void createConfig() {
#ifdef ESP_PLATFORM
    const char *path = get_config_path();
#else
    const char *path = CONFIG_FILE_PATH;
#endif
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("Failed to create default config file");
        return;
    }

    fprintf(f, "haas_token=\n");
    fprintf(f, "haas_dashboard=\n");
    fprintf(f, "haas_url=http://homeassistant.local:8123\n");
    fflush(f);
#ifdef ESP_PLATFORM
    fsync(fileno(f));
#endif
    fclose(f);
}

void loadConfig(){
    printf("Start loading Config.\n");
    if (!configExists()) {
        printf("Config file not found. Creating default config.\n");
        createConfig();
        printf("Created default config.\n");
    }

        const char *path =
    #ifdef ESP_PLATFORM
        get_config_path();
    #else
        CONFIG_FILE_PATH;
    #endif
        FILE *f = fopen(path, "r");
    if (!f) {
        perror("Could not open config file");
        return;
    }

    char line[512];
    printf("loading config file.\n");
    while (fgets(line, sizeof(line), f)) {
        char *equals = strchr(line, '=');
        if (!equals) continue;

        *equals = '\0';
        char *key = trim(line);
        char *value = trim(equals + 1);

        Config *entry = malloc(sizeof(Config));
        entry->key = strdup(key);
        entry->value = strdup(value);
        entry->next = config;
        config = entry;
    }

    fclose(f);
}

void saveConfig(){
#ifdef ESP_PLATFORM
    const char *path = get_config_path();
#else
    const char *path = CONFIG_FILE_PATH;
#endif
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("Failed to open config file for saving");
        return;
    }

    for (Config *e = config; e; e = e->next) {
        fprintf(f, "%s=%s\n", e->key, e->value);
    }

    fclose(f);
}

const char * getConfig(const char * key){
    for (Config *e = config; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->value;
    }
    return NULL;
}

void updateConfig(const char *key, const char *value){
    Config *entry = config;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            /* Copy before freeing: value may be this entry's own getConfig() pointer. */
            char *copy = strdup(value);
            free(entry->value);
            entry->value = copy;
            return;
        }
        entry = entry->next;
    }

    // Key not found, create new entry
    Config *new_entry = malloc(sizeof(Config));
    new_entry->key = strdup(key);
    new_entry->value = strdup(value);
    new_entry->next = config;
    config = new_entry;
}

void releaseConfig(){
    Config *e = config;
    while (e) {
        Config *next = e->next;
        free(e->key);
        free(e->value);
        free(e);
        e = next;
    }
    config = NULL;
}
