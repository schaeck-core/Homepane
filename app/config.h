#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
// On ESP-IDF, use FATFS (wear_levelling) mounted at /fs for writable storage
#define CONFIG_FILE_PATH "/littlefs/config.ini"
// Runtime override for config path on ESP (e.g., switch to SD card)
void set_config_path(const char *path);
const char *get_config_path(void);
#else
#define CONFIG_FILE_PATH "./config.ini"
#endif

typedef struct Config {
    char *key;
    char *value;
    struct Config *next;
} Config;

extern Config *config;

bool configExists();
void createConfig();
void loadConfig();
void saveConfig();
const char * getConfig(const char * key);
void updateConfig(const char *key, const char *value);
void releaseConfig();

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H
