#include "config.h"
#include "main.h"

static const char* CONFIG_TAG = "CONFIG";

esp_err_t config_init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(CONFIG_TAG, "NVS initialized successfully");
    return ESP_OK;
}

esp_err_t config_load(voice_keyboard_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(CONFIG_TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        config_set_defaults(config);
        return ret;
    }
    
    // Initialize with defaults first
    config_set_defaults(config);
    
    // Load configuration values
    size_t required_size;
    
    // Load WiFi SSID
    required_size = CONFIG_MAX_SSID_LEN;
    ret = nvs_get_str(nvs_handle, CONFIG_KEY_WIFI_SSID, config->wifi_ssid, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(CONFIG_TAG, "Failed to load WiFi SSID: %s", esp_err_to_name(ret));
    }
    
    // Load WiFi password
    required_size = CONFIG_MAX_PASSWORD_LEN;
    ret = nvs_get_str(nvs_handle, CONFIG_KEY_WIFI_PASSWORD, config->wifi_password, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(CONFIG_TAG, "Failed to load WiFi password: %s", esp_err_to_name(ret));
    }
    
    // Load server IP
    required_size = CONFIG_MAX_IP_LEN;
    ret = nvs_get_str(nvs_handle, CONFIG_KEY_SERVER_IP, config->server_ip, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(CONFIG_TAG, "Failed to load server IP: %s", esp_err_to_name(ret));
    }
    
    // Load server port
    ret = nvs_get_u16(nvs_handle, CONFIG_KEY_SERVER_PORT, &config->server_port);
    if (ret != ESP_OK) {
        ESP_LOGW(CONFIG_TAG, "Failed to load server port: %s", esp_err_to_name(ret));
    }
    
    // Load server URL
    required_size = CONFIG_MAX_URL_LEN;
    ret = nvs_get_str(nvs_handle, CONFIG_KEY_SERVER_URL, config->server_url, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(CONFIG_TAG, "Failed to load server URL: %s", esp_err_to_name(ret));
    }
    
    // Load config valid flag
    uint8_t config_valid_flag = 0;
    ret = nvs_get_u8(nvs_handle, CONFIG_KEY_CONFIG_VALID, &config_valid_flag);
    if (ret == ESP_OK) {
        config->config_valid = (config_valid_flag != 0);
    } else {
        config->config_valid = false;
        ESP_LOGW(CONFIG_TAG, "Failed to load config valid flag: %s", esp_err_to_name(ret));
    }
    
    nvs_close(nvs_handle);
    
    // Validate loaded configuration
    if (!config_is_valid(config)) {
        ESP_LOGW(CONFIG_TAG, "Loaded configuration is invalid, using defaults");
        config->config_valid = false;
    }
    
    ESP_LOGI(CONFIG_TAG, "Configuration loaded (valid: %s)", 
             config->config_valid ? "yes" : "no");
    
    return ESP_OK;
}

esp_err_t config_save(const voice_keyboard_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    uint8_t config_valid_flag; // Declare at top to avoid goto issues
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to open NVS namespace for writing: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save all configuration values
    ret = nvs_set_str(nvs_handle, CONFIG_KEY_WIFI_SSID, config->wifi_ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = nvs_set_str(nvs_handle, CONFIG_KEY_WIFI_PASSWORD, config->wifi_password);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to save WiFi password: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = nvs_set_str(nvs_handle, CONFIG_KEY_SERVER_IP, config->server_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to save server IP: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = nvs_set_u16(nvs_handle, CONFIG_KEY_SERVER_PORT, config->server_port);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to save server port: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = nvs_set_str(nvs_handle, CONFIG_KEY_SERVER_URL, config->server_url);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to save server URL: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    config_valid_flag = config->config_valid ? 1 : 0;
    ret = nvs_set_u8(nvs_handle, CONFIG_KEY_CONFIG_VALID, config_valid_flag);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to save config valid flag: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Commit changes
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to commit NVS changes: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(CONFIG_TAG, "Configuration saved successfully");
    
cleanup:
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t config_clear(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to open NVS namespace for clearing: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_erase_all(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_TAG, "Failed to clear NVS namespace: %s", esp_err_to_name(ret));
    } else {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(CONFIG_TAG, "Configuration cleared successfully");
        }
    }
    
    nvs_close(nvs_handle);
    return ret;
}

bool config_is_valid(const voice_keyboard_config_t *config) {
    if (!config) {
        return false;
    }
    
    // Check if required fields are not empty
    if (strlen(config->wifi_ssid) == 0) {
        ESP_LOGD(CONFIG_TAG, "Config invalid: empty WiFi SSID");
        return false;
    }
    
    if (strlen(config->server_ip) == 0) {
        ESP_LOGD(CONFIG_TAG, "Config invalid: empty server IP");
        return false;
    }
    
    if (config->server_port == 0) {
        ESP_LOGD(CONFIG_TAG, "Config invalid: server port is 0");
        return false;
    }
    
    if (strlen(config->server_url) == 0) {
        ESP_LOGD(CONFIG_TAG, "Config invalid: empty server URL");
        return false;
    }
    
    return true;
}

void config_set_defaults(voice_keyboard_config_t *config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(voice_keyboard_config_t));
    
    // Set default values
    strcpy(config->wifi_ssid, "");
    strcpy(config->wifi_password, "");
    strcpy(config->server_ip, "192.168.1.100");
    config->server_port = 8765;
    strcpy(config->server_url, "http://192.168.1.100:8765/api/offer");
    config->config_valid = false;
    
    ESP_LOGD(CONFIG_TAG, "Configuration set to defaults");
}

void config_print(const voice_keyboard_config_t *config) {
    if (!config) {
        ESP_LOGI(CONFIG_TAG, "Configuration is NULL");
        return;
    }
    
    ESP_LOGI(CONFIG_TAG, "Current Configuration:");
    ESP_LOGI(CONFIG_TAG, "  WiFi SSID: %s", config->wifi_ssid);
    ESP_LOGI(CONFIG_TAG, "  WiFi Password: %s", strlen(config->wifi_password) > 0 ? "[SET]" : "[EMPTY]");
    ESP_LOGI(CONFIG_TAG, "  Server IP: %s", config->server_ip);
    ESP_LOGI(CONFIG_TAG, "  Server Port: %u", config->server_port);
    ESP_LOGI(CONFIG_TAG, "  Server URL: %s", config->server_url);
    ESP_LOGI(CONFIG_TAG, "  Config Valid: %s", config->config_valid ? "yes" : "no");
}