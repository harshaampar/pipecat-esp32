#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#ifndef LINUX_BUILD
#include "nvs_flash.h"
#endif

// Global meeting state
meeting_state_t current_meeting_state = MEETING_STATE_IDLE;
static unsigned long meeting_start_time = 0;

// Shared variables for cross-core communication (no direct LVGL calls from main task)
volatile unsigned long shared_meeting_duration = 0;
volatile bool shared_meeting_ui_update_needed = false;

// Meeting control functions
void pipecat_start_meeting() {
    if (current_meeting_state == MEETING_STATE_IDLE) {
        ESP_LOGI(LOG_TAG, "Starting meeting...");
        current_meeting_state = MEETING_STATE_STARTING;
        meeting_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        
        // Initialize WebRTC connection for this meeting
        pipecat_init_webrtc();
        pipecat_screen_system_log("Connecting to server...");
    }
}

void pipecat_stop_meeting() {
    if (current_meeting_state == MEETING_STATE_ACTIVE || current_meeting_state == MEETING_STATE_STARTING) {
        ESP_LOGI(LOG_TAG, "Stopping meeting from state: %d", current_meeting_state);
        current_meeting_state = MEETING_STATE_STOPPING;
        
        pipecat_screen_system_log("Ending meeting...");
        
        // Stop WebRTC connection (this should notify server of disconnection)
        pipecat_stop_webrtc();
        
        // Give some time for disconnection to be processed
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Reset shared variables
        shared_meeting_ui_update_needed = false;
        shared_meeting_duration = 0;
        
        // Reset to idle state and show start button
        pipecat_screen_show_start_button();
        current_meeting_state = MEETING_STATE_IDLE;
        pipecat_screen_system_log("Meeting ended - Ready for next meeting");
        
        ESP_LOGI(LOG_TAG, "Meeting stopped successfully, returned to IDLE state");
    }
}

bool pipecat_is_meeting_active() {
    return current_meeting_state == MEETING_STATE_ACTIVE;
}

#ifndef LINUX_BUILD
// Main task function that runs on Core 0
void main_task(void *pvParameter) {
  ESP_LOGI(LOG_TAG, "Main task started on Core 0");  
  // Main loop - handle meeting state and button presses
  while (1) {
    // Handle button presses based on current state
    if (pipecat_check_button_pressed()) {
      ESP_LOGI(LOG_TAG, "Button pressed in state: %d", current_meeting_state);
      switch (current_meeting_state) {
        case MEETING_STATE_IDLE:
          ESP_LOGI(LOG_TAG, "Starting meeting from IDLE state");
          pipecat_start_meeting();
          break;
        case MEETING_STATE_STARTING:
          ESP_LOGI(LOG_TAG, "Stopping meeting from STARTING state (cancel connection)");
          pipecat_stop_meeting();
          break;
        case MEETING_STATE_ACTIVE:
          ESP_LOGI(LOG_TAG, "Stopping meeting from ACTIVE state");
          pipecat_stop_meeting();
          break;
        default:
          ESP_LOGI(LOG_TAG, "Ignoring button press in transitional state: %d", current_meeting_state);
          break;
      }
    }
    
    // Timer logic disabled - even simple state assignment breaks audio
    // Will need alternative approach for UI updates
    
    // Update shared variables for UI (no direct LVGL calls from main task)
    static unsigned long last_ui_update = 0;
    if (current_meeting_state == MEETING_STATE_ACTIVE) {
      unsigned long current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
      
      // Update shared variables once per second for screen task to read
      if (current_time - last_ui_update >= 1) {
        shared_meeting_duration = current_time - meeting_start_time;
        shared_meeting_ui_update_needed = true;
        last_ui_update = current_time;
      }
    }
    
    // Run WebRTC loop if not idle (includes STARTING state)
    if (current_meeting_state != MEETING_STATE_IDLE) {
      pipecat_webrtc_loop();
    }
            
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  
  // Initialize system components
  pipecat_init_screen();
  peer_init();
  
  ESP_LOGI(LOG_TAG, "Initializing audio systems...");
  pipecat_init_audio_capture();
  ESP_LOGI(LOG_TAG, "Audio systems initialized");
  pipecat_init_wifi();

  pipecat_screen_system_log("Meeting Assistant initialized");
  ESP_LOGI(LOG_TAG, "Meeting Assistant ESP32 client initialized");

  // Create main task on Core 0 (audio task will run on Core 1)
  xTaskCreatePinnedToCore(main_task, "Main Task", 8192, NULL, 5, NULL, 0);
  ESP_LOGI(LOG_TAG, "Main task created on Core 0");
  
  // Keep app_main alive - it should not return
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
#else
int main(void) {
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  peer_init();
  
  // Initialize system for Linux build
  pipecat_init_wifi();
  
  ESP_LOGI(LOG_TAG, "Meeting Assistant Linux build initialized");

  while (1) {
    // Handle button simulation for Linux build
    if (pipecat_check_button_pressed()) {
      switch (current_meeting_state) {
        case MEETING_STATE_IDLE:
          pipecat_start_meeting();
          break;
        case MEETING_STATE_ACTIVE:
          pipecat_stop_meeting();
          break;
        default:
          break;
      }
    }
    
    if (current_meeting_state != MEETING_STATE_IDLE) {
      pipecat_webrtc_loop();
    }
    
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#endif
