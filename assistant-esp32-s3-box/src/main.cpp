#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#ifndef LINUX_BUILD
#include "nvs_flash.h"

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  
  // Initialize components
  pipecat_init_screen();
  peer_init();
  pipecat_init_audio_capture();
  pipecat_init_audio_decoder();
  
  // Connect to WiFi automatically
  pipecat_screen_system_log("Connecting to WiFi...\n");
  pipecat_init_wifi();
  pipecat_screen_system_log("WiFi connected!\n");
  
  // Initialize WebRTC but don't connect yet
  pipecat_init_webrtc();
  
  pipecat_screen_system_log("Pipecat ESP32 client initialized\n");
  pipecat_screen_update_status("Ready - Tap Connect button to start");

  // Main loop
  while (1) {
    // Handle WebRTC only if connected
    if (pipecat_get_connection_state() == CONNECTION_STATE_CONNECTED ||
        pipecat_get_connection_state() == CONNECTION_STATE_CONNECTING) {
      pipecat_webrtc_loop();
    }
    
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#else
int main(void) {
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  peer_init();
  pipecat_webrtc();

  while (1) {
    pipecat_webrtc_loop();
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#endif
