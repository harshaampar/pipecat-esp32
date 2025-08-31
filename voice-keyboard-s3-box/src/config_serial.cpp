#include "config_serial.h"
#include "config.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <cJSON.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <driver/usb_serial_jtag.h>
#include <esp_vfs_usb_serial_jtag.h>
#include <esp_vfs_dev.h>
#include <fcntl.h>
#include <unistd.h>

static const char* CONFIG_SERIAL_TAG = "CONFIG_SERIAL";

// Global state
static bool config_mode_active = false;
static uint32_t config_mode_start_time = 0;
static TaskHandle_t config_serial_task_handle = NULL;
static wifi_scan_config_t scan_config = {0};
static wifi_ap_record_t* scan_results = NULL;
static uint16_t scan_result_count = 0;

void config_serial_init(void) {
    // Configure USB Serial/JTAG driver for bidirectional communication
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = CONFIG_SERIAL_BUFFER_SIZE * 2,
        .rx_buffer_size = CONFIG_SERIAL_BUFFER_SIZE * 2,
    };
    
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    
    // Configure VFS to use USB Serial/JTAG driver
    esp_vfs_usb_serial_jtag_use_driver();
    
    // Configure line endings for proper communication
    esp_vfs_dev_usb_serial_jtag_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_usb_serial_jtag_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    
    // Disable buffering on stdin for immediate input
    setvbuf(stdin, NULL, _IONBF, 0);
    
    ESP_LOGI(CONFIG_SERIAL_TAG, "USB Serial/JTAG configuration interface initialized");
}

void config_serial_enter_mode(void) {
    if (config_mode_active) {
        return;
    }
    
    config_mode_active = true;
    config_mode_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    ESP_LOGI(CONFIG_SERIAL_TAG, "Entering configuration mode");
    
    // Send all messages via printf (will go to USB CDC)
    printf("\n=== ESP32 Voice Keyboard Configuration Mode ===\n");
    printf("Ready for configuration commands. Timeout: 120 seconds\n");
    printf("Run: python esp32_voice_keyboard_config.py\n");
    printf("Commands: CONFIG_IDENTIFY, CONFIG_GET_ALL, CONFIG_WIFI_SCAN, CONFIG_SAVE, CONFIG_RESTART\n");
    printf("CONFIG_MODE_READY\n");
    fflush(stdout);
        
    // Create configuration task
    xTaskCreate(config_serial_task, "config_serial", 4096, NULL, 5, &config_serial_task_handle);
}

void config_serial_exit_mode(void) {
    if (!config_mode_active) {
        return;
    }
    
    config_mode_active = false;
    
    if (config_serial_task_handle) {
        vTaskDelete(config_serial_task_handle);
        config_serial_task_handle = NULL;
    }
    
    // Clean up scan results
    if (scan_results) {
        free(scan_results);
        scan_results = NULL;
        scan_result_count = 0;
    }
    
    ESP_LOGI(CONFIG_SERIAL_TAG, "Exiting configuration mode");
    printf("=== Configuration mode ended ===\n");
}

bool config_serial_is_active(void) {
    return config_mode_active;
}

void config_serial_task(void *pvParameter) {
    char input_buffer[CONFIG_SERIAL_BUFFER_SIZE];
    int buffer_pos = 0;
    uint32_t last_ready_signal = 0;
    static int loop_counter = 0;
    
    printf("CONFIG_SERIAL_TASK_STARTED\n");
    fflush(stdout);
    
    while (config_mode_active) {
        loop_counter++;
        
        // Check for timeout
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - config_mode_start_time > CONFIG_MODE_TIMEOUT_MS) {
            ESP_LOGI(CONFIG_SERIAL_TAG, "Configuration mode timeout");
            printf("Configuration mode timeout - exiting to normal mode\n");
            config_serial_exit_mode();
            break;
        }
        
        // Send periodic ready signal every 5 seconds
        if (current_time - last_ready_signal > 5000) {
            printf("CONFIG_MODE_READY\n");
            fflush(stdout);
            last_ready_signal = current_time;
        }
        
        // Use USB Serial/JTAG direct API for reliable communication
        uint8_t temp_data[CONFIG_SERIAL_BUFFER_SIZE];
        int len = usb_serial_jtag_read_bytes(temp_data, CONFIG_SERIAL_BUFFER_SIZE - 1, pdMS_TO_TICKS(50));
        
        if (len > 0) {
            // Reset timeout on activity
            config_mode_start_time = current_time;
            
            // Process each received byte
            for (int i = 0; i < len; i++) {
                int c = temp_data[i];                
                if (c == '\n' || c == '\r') {
                    if (buffer_pos > 0) {
                        input_buffer[buffer_pos] = '\0';
                        
                        printf("INPUT_RECEIVED: %s\n", input_buffer);
                        fflush(stdout);
                        
                        // Process command
                        ESP_LOGI(CONFIG_SERIAL_TAG, "Received command: %s", input_buffer);
                        printf("DEBUG: Processing command: %s\n", input_buffer);
                        fflush(stdout);
                        
                        if (strncmp(input_buffer, "CONFIG_IDENTIFY", 15) == 0) {
                            handle_identify_command();
                        } else if (strncmp(input_buffer, "CONFIG_GET_ALL", 14) == 0) {
                            handle_get_all_command();
                        } else if (strncmp(input_buffer, "CONFIG_WIFI_SCAN", 16) == 0) {
                            handle_wifi_scan_command();
                        } else if (strncmp(input_buffer, "CONFIG_SAVE ", 12) == 0) {
                            handle_save_command(input_buffer + 12);
                        } else if (strncmp(input_buffer, "CONFIG_RESTART", 14) == 0) {
                            handle_restart_command();
                        } else {
                            send_error_response("Unknown command");
                        }
                        
                        buffer_pos = 0;
                    }
                } else if (buffer_pos < CONFIG_SERIAL_BUFFER_SIZE - 1) {
                    input_buffer[buffer_pos++] = c;
                } else {
                    // Buffer overflow - reset
                    buffer_pos = 0;
                    send_error_response("Command too long");
                }
            }
        }        
        // Short delay since uart_read_bytes has timeout
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    config_serial_task_handle = NULL;
    vTaskDelete(NULL);
}

void handle_identify_command(void) {
    cJSON *response = cJSON_CreateObject();
    cJSON *device = cJSON_CreateString("ESP32_VOICE_KEYBOARD");
    cJSON *version = cJSON_CreateString("1.0.0");
    cJSON *status = cJSON_CreateString("config_mode");
    
    cJSON_AddItemToObject(response, "device", device);
    cJSON_AddItemToObject(response, "version", version);
    cJSON_AddItemToObject(response, "status", status);
    
    char *json_string = cJSON_PrintUnformatted(response);
    if (json_string) {
        send_json_response(json_string);
        free(json_string);
    }
    
    cJSON_Delete(response);
}

void handle_get_all_command(void) {
    voice_keyboard_config_t current_config;
    config_load(&current_config);
    
    cJSON *response = cJSON_CreateObject();
    cJSON *config_obj = cJSON_CreateObject();
    
    cJSON *wifi_ssid = cJSON_CreateString(current_config.wifi_ssid);
    cJSON *wifi_password = cJSON_CreateString(strlen(current_config.wifi_password) > 0 ? "[SET]" : "");
    cJSON *server_ip = cJSON_CreateString(current_config.server_ip);
    cJSON *server_port = cJSON_CreateNumber(current_config.server_port);
    cJSON *server_url = cJSON_CreateString(current_config.server_url);
    cJSON *config_valid = cJSON_CreateBool(current_config.config_valid);
    
    cJSON_AddItemToObject(config_obj, "wifi_ssid", wifi_ssid);
    cJSON_AddItemToObject(config_obj, "wifi_password", wifi_password);
    cJSON_AddItemToObject(config_obj, "server_ip", server_ip);
    cJSON_AddItemToObject(config_obj, "server_port", server_port);
    cJSON_AddItemToObject(config_obj, "server_url", server_url);
    cJSON_AddItemToObject(config_obj, "config_valid", config_valid);
    
    cJSON_AddItemToObject(response, "config", config_obj);
    
    char *json_string = cJSON_PrintUnformatted(response);
    if (json_string) {
        send_json_response(json_string);
        free(json_string);
    }
    
    cJSON_Delete(response);
}

void handle_wifi_scan_command(void) {
    esp_err_t ret = wifi_scan_networks();
    if (ret != ESP_OK) {
        send_error_response("WiFi scan failed");
        return;
    }
    
    // Wait for scan to complete (handled by event handler)
    for (int i = 0; i < 100; i++) {  // Wait up to 10 seconds
        if (scan_results != NULL) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (scan_results == NULL) {
        send_error_response("WiFi scan timeout");
        return;
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    
    for (int i = 0; i < scan_result_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON *ssid = cJSON_CreateString((char*)scan_results[i].ssid);
        cJSON *rssi = cJSON_CreateNumber(scan_results[i].rssi);
        cJSON *auth_mode = cJSON_CreateNumber(scan_results[i].authmode);
        cJSON *channel = cJSON_CreateNumber(scan_results[i].primary);
        
        cJSON_AddItemToObject(network, "ssid", ssid);
        cJSON_AddItemToObject(network, "rssi", rssi);
        cJSON_AddItemToObject(network, "auth_mode", auth_mode);
        cJSON_AddItemToObject(network, "channel", channel);
        
        cJSON_AddItemToArray(networks, network);
    }
    
    cJSON_AddItemToObject(response, "networks", networks);
    
    char *json_string = cJSON_PrintUnformatted(response);
    if (json_string) {
        send_json_response(json_string);
        free(json_string);
    }
    
    cJSON_Delete(response);
    
    // Clean up scan results
    free(scan_results);
    scan_results = NULL;
    scan_result_count = 0;
}

void handle_save_command(const char* json_data) {
    cJSON *json = cJSON_Parse(json_data);
    if (json == NULL) {
        send_error_response("Invalid JSON");
        return;
    }
    
    voice_keyboard_config_t new_config;
    config_set_defaults(&new_config);
    
    // Parse configuration from JSON
    cJSON *wifi_ssid = cJSON_GetObjectItem(json, "wifi_ssid");
    if (cJSON_IsString(wifi_ssid)) {
        strncpy(new_config.wifi_ssid, wifi_ssid->valuestring, CONFIG_MAX_SSID_LEN - 1);
    }
    
    cJSON *wifi_password = cJSON_GetObjectItem(json, "wifi_password");
    if (cJSON_IsString(wifi_password)) {
        strncpy(new_config.wifi_password, wifi_password->valuestring, CONFIG_MAX_PASSWORD_LEN - 1);
    }
    
    cJSON *server_ip = cJSON_GetObjectItem(json, "server_ip");
    if (cJSON_IsString(server_ip)) {
        strncpy(new_config.server_ip, server_ip->valuestring, CONFIG_MAX_IP_LEN - 1);
    }
    
    cJSON *server_port = cJSON_GetObjectItem(json, "server_port");
    if (cJSON_IsNumber(server_port)) {
        new_config.server_port = server_port->valueint;
    }
    
    cJSON *server_url = cJSON_GetObjectItem(json, "server_url");
    if (cJSON_IsString(server_url)) {
        strncpy(new_config.server_url, server_url->valuestring, CONFIG_MAX_URL_LEN - 1);
    }
    
    // Validate and save
    if (config_is_valid(&new_config)) {
        new_config.config_valid = true;
        esp_err_t ret = config_save(&new_config);
        
        if (ret == ESP_OK) {
            send_success_response("Configuration saved");
            ESP_LOGI(CONFIG_SERIAL_TAG, "New configuration saved successfully");
        } else {
            send_error_response("Failed to save configuration");
        }
    } else {
        send_error_response("Invalid configuration data");
    }
    
    cJSON_Delete(json);
}

void handle_restart_command(void) {
    cJSON *response = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString("restarting");
    cJSON *message = cJSON_CreateString("ESP32 will restart in 2 seconds");
    
    cJSON_AddItemToObject(response, "status", status);
    cJSON_AddItemToObject(response, "message", message);
    
    char *json_string = cJSON_PrintUnformatted(response);
    if (json_string) {
        send_json_response(json_string);
        free(json_string);
    }
    
    cJSON_Delete(response);
    
    // Exit configuration mode and restart after delay
    config_serial_exit_mode();
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

void send_json_response(const char* json) {
    // Send compact JSON on a single line with JSON_RESPONSE prefix for easy parsing
    printf("JSON_RESPONSE: %s\n", json);
    fflush(stdout);
    ESP_LOGI(CONFIG_SERIAL_TAG, "Sent JSON response: %.100s%s", json, strlen(json) > 100 ? "..." : "");
}

void send_error_response(const char* error_msg) {
    cJSON *response = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString("error");
    cJSON *message = cJSON_CreateString(error_msg);
    
    cJSON_AddItemToObject(response, "status", status);
    cJSON_AddItemToObject(response, "message", message);
    
    char *json_string = cJSON_PrintUnformatted(response);
    if (json_string) {
        send_json_response(json_string);
        free(json_string);
    }
    
    cJSON_Delete(response);
}

void send_success_response(const char* message) {
    cJSON *response = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString("success");
    cJSON *msg = cJSON_CreateString(message);
    
    cJSON_AddItemToObject(response, "status", status);
    cJSON_AddItemToObject(response, "message", msg);
    
    char *json_string = cJSON_PrintUnformatted(response);
    if (json_string) {
        send_json_response(json_string);
        free(json_string);
    }
    
    cJSON_Delete(response);
}

esp_err_t wifi_scan_networks(void) {
    // Initialize WiFi if not already done
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) {
        return ret;
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) {
        return ret;
    }
    
    // Configure scan
    scan_config.ssid = NULL;
    scan_config.bssid = NULL;
    scan_config.channel = 0;
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 100;
    scan_config.scan_time.active.max = 300;
    
    // Start scan
    return esp_wifi_scan_start(&scan_config, false);
}

void wifi_scan_done_handler(void) {
    esp_err_t ret = esp_wifi_scan_get_ap_num(&scan_result_count);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_SERIAL_TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        return;
    }
    
    if (scan_result_count == 0) {
        ESP_LOGW(CONFIG_SERIAL_TAG, "No WiFi networks found");
        return;
    }
    
    // Limit results to avoid memory issues
    if (scan_result_count > 20) {
        scan_result_count = 20;
    }
    
    scan_results = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * scan_result_count);
    if (scan_results == NULL) {
        ESP_LOGE(CONFIG_SERIAL_TAG, "Failed to allocate memory for scan results");
        return;
    }
    
    ret = esp_wifi_scan_get_ap_records(&scan_result_count, scan_results);
    if (ret != ESP_OK) {
        ESP_LOGE(CONFIG_SERIAL_TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
        free(scan_results);
        scan_results = NULL;
        return;
    }
    
    ESP_LOGI(CONFIG_SERIAL_TAG, "WiFi scan completed, found %d networks", scan_result_count);
}