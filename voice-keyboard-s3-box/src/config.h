#pragma once

#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration constants
#define CONFIG_NVS_NAMESPACE "voice_kb"
#define CONFIG_MAX_SSID_LEN 32
#define CONFIG_MAX_PASSWORD_LEN 64
#define CONFIG_MAX_URL_LEN 256
#define CONFIG_MAX_IP_LEN 16

// Configuration structure
typedef struct {
    char wifi_ssid[CONFIG_MAX_SSID_LEN];
    char wifi_password[CONFIG_MAX_PASSWORD_LEN];
    char server_ip[CONFIG_MAX_IP_LEN];
    uint16_t server_port;
    char server_url[CONFIG_MAX_URL_LEN];
    bool config_valid;
} voice_keyboard_config_t;

// Configuration functions
esp_err_t config_init_nvs(void);
esp_err_t config_load(voice_keyboard_config_t *config);
esp_err_t config_save(const voice_keyboard_config_t *config);
esp_err_t config_clear(void);
bool config_is_valid(const voice_keyboard_config_t *config);
void config_set_defaults(voice_keyboard_config_t *config);
void config_print(const voice_keyboard_config_t *config);

// Configuration keys
#define CONFIG_KEY_WIFI_SSID "wifi_ssid"
#define CONFIG_KEY_WIFI_PASSWORD "wifi_pass"
#define CONFIG_KEY_SERVER_IP "server_ip"
#define CONFIG_KEY_SERVER_PORT "server_port"
#define CONFIG_KEY_SERVER_URL "server_url"
#define CONFIG_KEY_CONFIG_VALID "config_valid"

#ifdef __cplusplus
}
#endif