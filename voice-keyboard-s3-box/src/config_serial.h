#pragma once

#include "config.h"
#include <esp_log.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration mode timeout (2 minutes)
#define CONFIG_MODE_TIMEOUT_MS 120000

// Serial buffer sizes
#define CONFIG_SERIAL_BUFFER_SIZE 512
#define CONFIG_JSON_BUFFER_SIZE 1024

// Configuration serial interface functions
void config_serial_init(void);
void config_serial_task(void *pvParameter);
bool config_serial_is_active(void);
void config_serial_enter_mode(void);
void config_serial_exit_mode(void);

// Command handlers
void handle_identify_command(void);
void handle_get_all_command(void);
void handle_wifi_scan_command(void);
void handle_save_command(const char* json_data);
void handle_restart_command(void);

// Utility functions
void send_json_response(const char* json);
void send_error_response(const char* error_msg);
void send_success_response(const char* message);

// WiFi scanning functions
esp_err_t wifi_scan_networks(void);
void wifi_scan_done_handler(void);

#ifdef __cplusplus
}
#endif