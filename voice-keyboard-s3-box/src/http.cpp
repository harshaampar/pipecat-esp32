#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <string.h>
#include <driver/usb_serial_jtag.h>

#include "main.h"
#include "config.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  static int output_len = 0;
  switch (evt->event_id) {
    case HTTP_EVENT_REDIRECT:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_REDIRECT");
      esp_http_client_set_header(evt->client, "From", "user@example.com");
      esp_http_client_set_header(evt->client, "Accept", "text/html");
      esp_http_client_set_redirection(evt->client);
      break;
    case HTTP_EVENT_ERROR:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ERROR");
      break;
    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_CONNECTED");
      break;
    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_HEADER_SENT");
      break;
    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
               evt->header_key, evt->header_value);
      break;
    case HTTP_EVENT_ON_DATA: {
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      if (esp_http_client_is_chunked_response(evt->client)) {
        ESP_LOGE(LOG_TAG, "Chunked HTTP response not supported");
#ifndef LINUX_BUILD
        esp_restart();
#endif
      }

      // If user_data buffer is configured, copy the response into the buffer
      int copy_len = 0;
      if (evt->user_data) {
        // Initialize buffer on first data event
        if (output_len == 0) {
          memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
        }
        
        // The last byte in evt->user_data is kept for the NULL character in
        // case of out-of-bound access.
        copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len - 1));
        if (copy_len > 0) {
          memcpy(((char *)evt->user_data) + output_len, evt->data, copy_len);
          output_len += copy_len;
          // Ensure null termination
          ((char *)evt->user_data)[output_len] = '\0';
        }
      }

      break;
    }
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_FINISH");
      output_len = 0;
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_DISCONNECTED");
      output_len = 0;
      break;
  }
  return ESP_OK;
}

void pipecat_http_request(char *offer, char *answer) {
  // Get current configuration for server URL
  const voice_keyboard_config_t* device_config = pipecat_get_config();
  if (!device_config || !config_is_valid(device_config)) {
    ESP_LOGE(LOG_TAG, "Invalid configuration - cannot make HTTP request");
    return;
  }

  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));

  config.url = device_config->server_url;
  config.event_handler = http_event_handler;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.user_data = answer;

  ESP_LOGI(LOG_TAG, "Connecting to %s", config.url);

  cJSON *j_offer = cJSON_CreateObject();
  if (j_offer == NULL) {
    ESP_LOGE(LOG_TAG, "Unable to create JSON offer");
    return;
  }
  if (cJSON_AddStringToObject(j_offer, "sdp", offer) == NULL) {
    cJSON_Delete(j_offer);
    ESP_LOGE(LOG_TAG, "Unable to create JSON offer");
    return;
  }
  if (cJSON_AddStringToObject(j_offer, "type", "offer") == NULL) {
    cJSON_Delete(j_offer);
    ESP_LOGE(LOG_TAG, "Unable to create JSON offer");
    return;
  }

  ESP_LOGD(LOG_TAG, "OFFER\n%s", offer);

  char *j_offer_str = cJSON_Print(j_offer);

  cJSON_Delete(j_offer);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, j_offer_str, strlen(j_offer_str));

  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  if (err != ESP_OK || status_code != 200) {
    ESP_LOGE(LOG_TAG, "Error perform http request %s (status %d)",
             esp_err_to_name(err), status_code);
#ifndef LINUX_BUILD
    esp_restart();
#endif
  }

  cJSON *j_response = cJSON_Parse((const char *)answer);
  if (j_response == NULL) {
    ESP_LOGE(LOG_TAG, "Error parsing HTTP response");
#ifndef LINUX_BUILD
    esp_restart();
#endif
  }

  cJSON *j_answer = cJSON_GetObjectItem(j_response, "sdp");
  if (j_answer == NULL) {
    ESP_LOGE(LOG_TAG, "Unable to find `sdp` field in response");
#ifndef LINUX_BUILD
    esp_restart();
#endif
  }

  memset(answer, 0, MAX_HTTP_OUTPUT_BUFFER + 1);
  memcpy(answer, j_answer->valuestring, strlen(j_answer->valuestring));

  ESP_LOGD(LOG_TAG, "ANSWER\n%s", answer);

  cJSON_Delete(j_response);

  esp_http_client_cleanup(client);
}

// Health check function - simple HTTP GET to verify server availability
bool pipecat_http_health_check(const char* server_ip, const char* server_port) {
    char health_url[256];
    snprintf(health_url, sizeof(health_url), "http://%s:%s/api/health", server_ip, server_port);
    
    ESP_LOGI(LOG_TAG, "Checking server health at: %s", health_url);
    
    // Use simple HTTP client without shared event handler to avoid crashes
    esp_http_client_config_t config = {
        .url = health_url,
        .timeout_ms = 5000,  // 5 second timeout
        .event_handler = NULL,  // Don't use shared event handler
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to initialize HTTP client for health check");
        return false;
    }
    
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Health check HTTP request failed: %s", esp_err_to_name(err));
        return false;
    }
    
    if (status_code != 200) {
        ESP_LOGE(LOG_TAG, "Health check failed with status: %d", status_code);
        return false;
    }
    
    ESP_LOGI(LOG_TAG, "✅ Health check successful - server responded with 200 OK");
    return true;
}

// USB JTAG Response parsing
bool pipecat_usb_parse_provisioning_response(voice_keyboard_config_t* config, const char* response_line) {
    if (!strstr(response_line, "PROVISIONING_RESPONSE:")) {
        return false;
    }
    
    // Extract JSON from response line
    const char* json_start = strstr(response_line, "PROVISIONING_RESPONSE:");
    if (!json_start) {
        return false;
    }
    json_start += strlen("PROVISIONING_RESPONSE:");
    
    // Skip whitespace
    while (*json_start == ' ' || *json_start == '\t') {
        json_start++;
    }
    
    // Parse JSON response
    cJSON *response = cJSON_Parse(json_start);
    if (!response) {
        ESP_LOGE(LOG_TAG, "Failed to parse USB JTAG response JSON");
        return false;
    }
    
    cJSON *status = cJSON_GetObjectItem(response, "status");
    if (!status || !cJSON_IsString(status) || strcmp(status->valuestring, "success") != 0) {
        ESP_LOGE(LOG_TAG, "USB JTAG provisioning response unsuccessful");
        cJSON_Delete(response);
        return false;
    }
    
    bool config_updated = false;
    
    // Extract configuration fields
    cJSON *wifi_ssid = cJSON_GetObjectItem(response, "wifi_ssid");
    cJSON *wifi_password = cJSON_GetObjectItem(response, "wifi_password");
    cJSON *server_ip = cJSON_GetObjectItem(response, "server_ip");
    cJSON *server_port = cJSON_GetObjectItem(response, "server_port");
    cJSON *server_url = cJSON_GetObjectItem(response, "server_url");
    
    if (wifi_ssid && cJSON_IsString(wifi_ssid)) {
        strncpy(config->wifi_ssid, wifi_ssid->valuestring, sizeof(config->wifi_ssid) - 1);
        config->wifi_ssid[sizeof(config->wifi_ssid) - 1] = '\0';
        config_updated = true;
        ESP_LOGI(LOG_TAG, "Updated WiFi SSID: %s", config->wifi_ssid);
    }
    
    if (wifi_password && cJSON_IsString(wifi_password)) {
        strncpy(config->wifi_password, wifi_password->valuestring, sizeof(config->wifi_password) - 1);
        config->wifi_password[sizeof(config->wifi_password) - 1] = '\0';
        config_updated = true;
        ESP_LOGI(LOG_TAG, "Updated WiFi password");
    }
    
    if (server_ip && cJSON_IsString(server_ip)) {
        strncpy(config->server_ip, server_ip->valuestring, sizeof(config->server_ip) - 1);
        config->server_ip[sizeof(config->server_ip) - 1] = '\0';
        config_updated = true;
        ESP_LOGI(LOG_TAG, "Updated server IP: %s", config->server_ip);
    }
    
    if (server_port) {
        if (cJSON_IsString(server_port)) {
            config->server_port = (uint16_t)atoi(server_port->valuestring);
        } else if (cJSON_IsNumber(server_port)) {
            config->server_port = (uint16_t)server_port->valueint;
        }
        config_updated = true;
        ESP_LOGI(LOG_TAG, "Updated server port: %d", config->server_port);
    }
    
    if (server_url && cJSON_IsString(server_url)) {
        strncpy(config->server_url, server_url->valuestring, sizeof(config->server_url) - 1);
        config->server_url[sizeof(config->server_url) - 1] = '\0';
        config_updated = true;
        ESP_LOGI(LOG_TAG, "Updated server URL: %s", config->server_url);
    }
    
    if (config_updated) {
        config->config_valid = true;
        ESP_LOGI(LOG_TAG, "✅ Configuration updated from USB JTAG response");
    }
    
    cJSON_Delete(response);
    return config_updated;
}

// USB JTAG Provisioning Communication
bool pipecat_usb_request_provisioning(const char* request_type, voice_keyboard_config_t* config) {
    ESP_LOGI(LOG_TAG, "📡 Requesting provisioning via USB JTAG: %s", request_type);
    
    // Send request via USB JTAG (USB Serial JTAG interface)
    cJSON *request = cJSON_CreateObject();
    if (request == NULL) {
        return false;
    }
    
    cJSON_AddStringToObject(request, "type", "provisioning_request");
    cJSON_AddStringToObject(request, "request_type", request_type);
    cJSON_AddStringToObject(request, "device_id", "esp32_voice_keyboard");
    cJSON_AddNumberToObject(request, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);
    
    char *request_json = cJSON_PrintUnformatted(request);  // Use unformatted for single line
    cJSON_Delete(request);
    
    if (request_json == NULL) {
        return false;
    }
    
    // Send via USB Serial JTAG as single line
    printf("PROVISIONING_REQUEST: %s\n", request_json);
    fflush(stdout);
    
    // Also send via ESP_LOG for debugging
    ESP_LOGI(LOG_TAG, "📡 Provisioning request sent via USB JTAG: %s", request_type);
    ESP_LOGI(LOG_TAG, "Full request: %s", request_json);
    
    free(request_json);
    return true;
}

// Check for USB JTAG responses (non-blocking)
bool pipecat_usb_check_provisioning_response(voice_keyboard_config_t* config) {
    static char usb_buffer[1024];
    static int buffer_pos = 0;
    static int debug_counter = 0;
    
    // Read available data from USB Serial JTAG
    uint8_t data[256];
    int len = usb_serial_jtag_read_bytes(data, sizeof(data) - 1, pdMS_TO_TICKS(10));
    
    // Debug info every 100 calls
    debug_counter++;
    if (debug_counter % 100 == 0) {
        ESP_LOGI(LOG_TAG, "USB response check #%d, buffer_pos=%d, bytes_read=%d", debug_counter, buffer_pos, len);
    }
    
    if (len > 0) {
        data[len] = '\0';
        
        // Append to buffer
        for (int i = 0; i < len && buffer_pos < sizeof(usb_buffer) - 1; i++) {
            if (data[i] == '\n' || data[i] == '\r') {
                if (buffer_pos > 0) {
                    usb_buffer[buffer_pos] = '\0';
                    
                    // Check if this is a provisioning response
                    if (pipecat_usb_parse_provisioning_response(config, usb_buffer)) {
                        buffer_pos = 0;
                        return true;
                    }
                    
                    buffer_pos = 0;
                }
            } else {
                usb_buffer[buffer_pos++] = data[i];
            }
        }
    }
    
    return false;
}

// Send provisioning status via USB JTAG
void pipecat_usb_send_provisioning_status(const char* phase, const char* status, const char* message) {
    cJSON *status_update = cJSON_CreateObject();
    if (status_update == NULL) {
        return;
    }
    
    cJSON_AddStringToObject(status_update, "type", "provisioning_status");
    cJSON_AddStringToObject(status_update, "device_id", "esp32_voice_keyboard");
    cJSON_AddStringToObject(status_update, "phase", phase);
    cJSON_AddStringToObject(status_update, "status", status);
    cJSON_AddStringToObject(status_update, "message", message);
    cJSON_AddNumberToObject(status_update, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);
    
    char *status_json = cJSON_PrintUnformatted(status_update);  // Use unformatted for single line
    cJSON_Delete(status_update);
    
    if (status_json == NULL) {
        return;
    }
    
    // Send via USB Serial JTAG
    printf("PROVISIONING_STATUS: %s\n", status_json);
    fflush(stdout);
    
    ESP_LOGD(LOG_TAG, "📡 Status sent via USB JTAG: %s - %s", phase, status);
    
    free(status_json);
}

void pipecat_http_debug_message(const char* message) {
  ESP_LOGI(LOG_TAG, "Debug: %s", message);
  
  // Skip HTTP debug during early initialization to avoid crashes
  static bool early_init = true;
  static int call_count = 0;
  call_count++;
  
  // Skip first few calls during initialization, or if WiFi not connected
  if (early_init && call_count < 5) {
    ESP_LOGD(LOG_TAG, "Skipping HTTP debug during early initialization");
    return;
  }
  early_init = false;
  
  // Get current configuration for server URL
  const voice_keyboard_config_t* device_config = pipecat_get_config();
  if (!device_config || !config_is_valid(device_config)) {
    ESP_LOGD(LOG_TAG, "Invalid configuration - skipping HTTP debug");
    return;
  }

  // Create debug URL - extract base URL from server URL and append /debug
  char debug_url[512];
  char base_url[512];
  
  // server_url is like "http://192.168.0.105:8765/api/offer"
  // We need "http://192.168.0.105:8765/debug"
  strncpy(base_url, device_config->server_url, sizeof(base_url) - 1);
  base_url[sizeof(base_url) - 1] = '\0';
  
  // Find and remove "/api/offer" from the end
  char* api_pos = strstr(base_url, "/api/offer");
  if (api_pos != NULL) {
    *api_pos = '\0';
  }
  
  // Ensure we have enough space for "/debug" (6 chars + null terminator)
  int base_len = strlen(base_url);
  if (base_len + 7 < sizeof(debug_url)) {
    snprintf(debug_url, sizeof(debug_url), "%s/debug", base_url);
  } else {
    ESP_LOGE(LOG_TAG, "Debug URL too long, skipping HTTP debug");
    return;
  }
  
  esp_http_client_config_t config = {
    .url = debug_url,
    .timeout_ms = 3000,  // Short timeout for debug messages
  };
  
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to initialize HTTP client for debug");
    return;
  }
  
  // Create JSON payload
  cJSON *json = cJSON_CreateObject();
  if (json == NULL) {
    esp_http_client_cleanup(client);
    return;
  }
  
  cJSON_AddStringToObject(json, "message", message);
  cJSON_AddNumberToObject(json, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);
  cJSON_AddStringToObject(json, "session_id", "esp32_session");
  
  char *json_string = cJSON_Print(json);
  cJSON_Delete(json);
  
  if (json_string == NULL) {
    esp_http_client_cleanup(client);
    return;
  }
  
  // Set HTTP method and headers
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, json_string, strlen(json_string));
  
  // Send request (fire and forget) with error handling
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    ESP_LOGD(LOG_TAG, "HTTP debug message failed: %s", esp_err_to_name(err));
    // Don't crash on HTTP errors - just log and continue
  } else {
    ESP_LOGD(LOG_TAG, "HTTP debug message sent successfully");
  }
  
  free(json_string);
  esp_http_client_cleanup(client);
}
