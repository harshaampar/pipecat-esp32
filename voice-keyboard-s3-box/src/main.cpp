#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#ifndef LINUX_BUILD
#include "nvs_flash.h"
#endif

// Global keyboard state
keyboard_state_t current_keyboard_state = KEYBOARD_STATE_IDLE;
static unsigned long session_start_time = 0;

// Shared variables for cross-core communication (no direct LVGL calls from main task)
volatile keyboard_state_t shared_keyboard_state = KEYBOARD_STATE_IDLE;
volatile bool shared_keyboard_ui_update_needed = false;

// Voice session control functions
void pipecat_start_voice_session() {
    if (current_keyboard_state == KEYBOARD_STATE_IDLE) {
        ESP_LOGI(LOG_TAG, "Starting voice session...");
        current_keyboard_state = KEYBOARD_STATE_STARTING;
        session_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        
        // Add small delay to prevent immediate crash
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Initialize WebRTC connection for this session
        pipecat_init_webrtc();
    }
}

void pipecat_stop_voice_session() {
    if (current_keyboard_state == KEYBOARD_STATE_TRANSCRIBING || current_keyboard_state == KEYBOARD_STATE_STARTING) {
        ESP_LOGI(LOG_TAG, "Stopping voice session from state: %d", current_keyboard_state);
        current_keyboard_state = KEYBOARD_STATE_STOPPING;
        
        
        // Stop WebRTC connection (this should notify server of disconnection)
        pipecat_stop_webrtc();
        
        // Give some time for disconnection to be processed
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Reset shared variables (no UI updates)
        shared_keyboard_state = KEYBOARD_STATE_IDLE;
        
        // Reset to idle state 
        current_keyboard_state = KEYBOARD_STATE_IDLE;
        
        ESP_LOGI(LOG_TAG, "Voice session stopped successfully, returned to IDLE state");
    }
}

bool pipecat_is_voice_active() {
    return current_keyboard_state == KEYBOARD_STATE_TRANSCRIBING || current_keyboard_state == KEYBOARD_STATE_TYPING;
}

#ifndef LINUX_BUILD
// Main task function that runs on Core 0
void main_task(void *pvParameter) {
  ESP_LOGI(LOG_TAG, "Main task started on Core 0");
  pipecat_send_debug_message("MAIN TASK STARTED - entering main loop");
  
  // Debug counter for periodic heartbeat
  static int loop_count = 0;
  
  // Main loop - handle keyboard state and button presses
  while (1) {    
    // Periodic heartbeat every 10 seconds (200 iterations * 50ms = 10s)
    if (loop_count % 500 == 0) {
      ESP_LOGI(LOG_TAG, "Main loop heartbeat - state: %d, count: %d", current_keyboard_state, loop_count);
      pipecat_send_debug_message("Main loop running - waiting for button");
    }

    loop_count++;
    
    // Handle button presses based on current state (minimal debug to prevent crashes)
    if (pipecat_check_button_pressed()) {
      ESP_LOGI(LOG_TAG, "Button pressed - state: %d", current_keyboard_state);
      switch (current_keyboard_state) {
        case KEYBOARD_STATE_IDLE:
          pipecat_start_voice_session();
          break;
        case KEYBOARD_STATE_STARTING:
          pipecat_stop_voice_session();
          break;
        case KEYBOARD_STATE_TRANSCRIBING:
        case KEYBOARD_STATE_TYPING:
          pipecat_stop_voice_session();
          break;
        default:
          ESP_LOGI(LOG_TAG, "Button ignored - transitional state");
          break;
      }
    }
    
    // Timer logic disabled - even simple state assignment breaks audio
    // Will need alternative approach for UI updates
    
    // No UI updates - screen stays static
    
    // Run WebRTC loop if not idle (includes STARTING state)
    if (current_keyboard_state != KEYBOARD_STATE_IDLE) {
      pipecat_webrtc_loop();
    }
            
    // Reduced delay to make button detection more responsive
    vTaskDelay(pdMS_TO_TICKS(20));
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
  ESP_LOGI(LOG_TAG, "Initializing screen...");
  pipecat_init_screen();
  ESP_LOGI(LOG_TAG, "Screen initialized, testing display...");
  
  // Set static transcribing message once
  ESP_LOGI(LOG_TAG, "Screen initialized with static message");
  
  peer_init();
  
  ESP_LOGI(LOG_TAG, "Initializing audio systems...");
  pipecat_init_audio_capture();
  ESP_LOGI(LOG_TAG, "Audio systems initialized");
  pipecat_init_wifi();

  // Initialize USB HID keyboard (function kept same name for compatibility)
  ESP_LOGI(LOG_TAG, "Initializing USB HID...");
  pipecat_init_bluetooth_hid();  // Actually initializes USB HID now
  ESP_LOGI(LOG_TAG, "USB HID initialized");

  // Skip screen updates during init to prevent task creation issues
  pipecat_send_debug_message("=== SYSTEM STARTING ===");
  ESP_LOGI(LOG_TAG, "=== Voice Keyboard System Starting ===");

  pipecat_send_debug_message("Voice Keyboard initialized");
  ESP_LOGI(LOG_TAG, "Voice Keyboard ESP32 client initialized");

  pipecat_send_debug_message("Starting delay");
  // Give all systems time to stabilize
  vTaskDelay(pdMS_TO_TICKS(100));
  pipecat_send_debug_message("Delay complete");
  
  // Check available memory before task creation
  size_t free_heap = esp_get_free_heap_size();
  ESP_LOGI(LOG_TAG, "Free heap before main task: %d bytes", free_heap);
  char free_heap_str[50];
  snprintf(free_heap_str, sizeof(free_heap_str), "Free heap before main task: %zu bytes", free_heap);
  pipecat_send_debug_message(free_heap_str);
  
  // Create main task with moderate priority for reliable startup
  ESP_LOGI(LOG_TAG, "Creating main task...");
  pipecat_send_debug_message("About to create main task with standard settings");
  BaseType_t task_result = xTaskCreate(main_task, "MainTask", 8192, NULL, 4, NULL); // Reasonable stack size
  if (task_result == pdPASS) {
    ESP_LOGI(LOG_TAG, "Main task created successfully");
    pipecat_send_debug_message("Main task created successfully with priority 4");
  } else {
    ESP_LOGE(LOG_TAG, "Failed to create main task! Result: %d", task_result);
    size_t free_heap_after = esp_get_free_heap_size();
    ESP_LOGE(LOG_TAG, "Free heap after failure: %d bytes", free_heap_after);
    pipecat_send_debug_message("FAILED to create main task - insufficient memory?");
  }
  
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
  pipecat_init_bluetooth_hid(); // Linux stub
  
  ESP_LOGI(LOG_TAG, "Voice Keyboard Linux build initialized");

  while (1) {
    // Handle button simulation for Linux build
    if (pipecat_check_button_pressed()) {
      switch (current_keyboard_state) {
        case KEYBOARD_STATE_IDLE:
          pipecat_start_voice_session();
          break;
        case KEYBOARD_STATE_TRANSCRIBING:
          pipecat_stop_voice_session();
          break;
        default:
          break;
      }
    }
    
    if (current_keyboard_state != KEYBOARD_STATE_IDLE) {
      pipecat_webrtc_loop();
    }
    
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#endif
