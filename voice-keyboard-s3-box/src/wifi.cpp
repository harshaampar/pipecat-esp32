#include <assert.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "config.h"
#include "config_serial.h"

static bool g_wifi_connected = false;
static voice_keyboard_config_t g_current_config;

static void pipecat_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data) {
  static int s_retry_num = 0;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < 5) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(LOG_TAG, "retry to connect to the AP");
    } else {
      ESP_LOGE(LOG_TAG, "Failed to connect to WiFi after 5 retries");
      g_wifi_connected = false;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(LOG_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    g_wifi_connected = true;
    s_retry_num = 0; // Reset retry counter on successful connection
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
    wifi_scan_done_handler();
  }
}

bool pipecat_connect_wifi_with_config(const voice_keyboard_config_t *config) {
  if (!config || !config_is_valid(config)) {
    ESP_LOGE(LOG_TAG, "Invalid WiFi configuration");
    return false;
  }

  ESP_LOGI(LOG_TAG, "Connecting to WiFi SSID: %s", config->wifi_ssid);
  wifi_config_t wifi_config;
  memset(&wifi_config, 0, sizeof(wifi_config));
  strncpy((char *)wifi_config.sta.ssid, config->wifi_ssid,
          sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, config->wifi_password,
          sizeof(wifi_config.sta.password));

  esp_err_t ret = esp_wifi_set_config(
      static_cast<wifi_interface_t>(ESP_IF_WIFI_STA), &wifi_config);
  if (ret != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_wifi_connect();
  if (ret != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
    return false;
  }

  // Wait for connection with timeout (30 seconds)
  int timeout_count = 0;
  const int max_timeout = 150; // 30 seconds (150 * 200ms)
  
  g_wifi_connected = false;
  while (!g_wifi_connected && timeout_count < max_timeout) {
    vTaskDelay(pdMS_TO_TICKS(200));
    timeout_count++;
  }

  if (g_wifi_connected) {
    ESP_LOGI(LOG_TAG, "WiFi connected successfully");
    return true;
  } else {
    ESP_LOGE(LOG_TAG, "WiFi connection timeout");
    return false;
  }
}

void pipecat_init_wifi() {
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &pipecat_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &pipecat_event_handler, NULL));

  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(LOG_TAG, "WiFi initialized");
}

bool pipecat_is_wifi_connected() {
  return g_wifi_connected;
}
