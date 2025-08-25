#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <string.h>

#include "main.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  static int output_len;
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

      if (output_len == 0 && evt->user_data) {
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
      }

      // If user_data buffer is configured, copy the response into the buffer
      int copy_len = 0;
      if (evt->user_data) {
        // The last byte in evt->user_data is kept for the NULL character in
        // case of out-of-bound access.
        copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
        if (copy_len) {
          memcpy(((char *)evt->user_data) + output_len, evt->data, copy_len);
        }
      }
      output_len += copy_len;

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
  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));

  config.url = PIPECAT_SMALLWEBRTC_URL;
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
  
  // Create debug URL - extract base URL from PIPECAT_SMALLWEBRTC_URL and append /debug
  char debug_url[512];  // Increased buffer size
  char base_url[512];   // Increased buffer size
  
  // PIPECAT_SMALLWEBRTC_URL is like "http://192.168.0.105:8765/api/offer"
  // We need "http://192.168.0.105:8765/debug"
  strncpy(base_url, PIPECAT_SMALLWEBRTC_URL, sizeof(base_url) - 1);
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
