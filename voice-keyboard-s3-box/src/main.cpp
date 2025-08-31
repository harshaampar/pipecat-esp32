#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#ifndef LINUX_BUILD
#include "nvs_flash.h"
#endif

#include "config.h"
#include "config_serial.h"

#include <driver/usb_serial_jtag.h>

// Global keyboard state - start with CONFIG to detect if provisioning needed
keyboard_state_t current_keyboard_state = KEYBOARD_STATE_CONFIG_MODE;
static unsigned long session_start_time = 0;

// Shared variables for cross-core communication (no direct LVGL calls from main task)
volatile keyboard_state_t shared_keyboard_state = KEYBOARD_STATE_CONFIG_MODE;
volatile bool shared_keyboard_ui_update_needed = false;

// Global configuration
static voice_keyboard_config_t g_device_config;

// Voice session control functions
void pipecat_start_voice_session() {
    if (current_keyboard_state == KEYBOARD_STATE_IDLE) {
        ESP_LOGI(LOG_TAG, "Starting voice session...");
        current_keyboard_state = KEYBOARD_STATE_STARTING;
        
        // Update UI to show starting state
        shared_keyboard_state = KEYBOARD_STATE_STARTING;
        shared_keyboard_ui_update_needed = true;
        
        // First check if server is available before starting WebRTC
        ESP_LOGI(LOG_TAG, "Checking server availability before starting voice session...");
        
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", g_device_config.server_port);
        if (!pipecat_http_health_check(g_device_config.server_ip, port_str)) {
            ESP_LOGW(LOG_TAG, "Server not available - showing error screen");
            current_keyboard_state = KEYBOARD_STATE_SERVER_UNAVAILABLE;
            shared_keyboard_state = KEYBOARD_STATE_SERVER_UNAVAILABLE;
            shared_keyboard_ui_update_needed = true;
            return;
        }
        
        ESP_LOGI(LOG_TAG, "Server is available - proceeding with voice session");
        current_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
        shared_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
        shared_keyboard_ui_update_needed = true;
        
        session_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        
        // Add small delay to prevent immediate crash
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Initialize WebRTC connection for this session
        pipecat_init_webrtc();
    }
}

void pipecat_stop_voice_session() {
    pipecat_send_debug_message("=== STOP SESSION CALLED ===");
    ESP_LOGI(LOG_TAG, "pipecat_stop_voice_session() called - current state: %d", current_keyboard_state);
    
    if (current_keyboard_state == KEYBOARD_STATE_TRANSCRIBING || current_keyboard_state == KEYBOARD_STATE_TYPING) {
        pipecat_send_debug_message("State check passed - proceeding with stop");
        ESP_LOGI(LOG_TAG, "Stopping voice session from state: %d", current_keyboard_state);
        current_keyboard_state = KEYBOARD_STATE_STOPPING;
        
        // Send disconnect message to server
        pipecat_send_debug_message("Sending disconnect message to server");
        pipecat_send_disconnect_message();
        pipecat_send_debug_message("Disconnect message sent");
        
        // Stop WebRTC connection locally
        pipecat_send_debug_message("Stopping WebRTC connection");
        pipecat_stop_webrtc();
        pipecat_send_debug_message("WebRTC connection stopped");
        
        // Give some time for disconnection to be processed
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Reset to idle state immediately 
        current_keyboard_state = KEYBOARD_STATE_IDLE;
        pipecat_send_debug_message("Current state set to IDLE");
        
        // Reset shared variables and trigger UI update via screen task (thread-safe)
        shared_keyboard_state = KEYBOARD_STATE_IDLE;
        shared_keyboard_ui_update_needed = true;
        pipecat_send_debug_message("Shared state set to IDLE - UI update requested");
        
        pipecat_send_debug_message("=== VOICE SESSION STOPPED - NOW IN IDLE ===");
        ESP_LOGI(LOG_TAG, "Voice session stopped successfully, returned to IDLE state");
    } else {
        char debug_msg[100];
        snprintf(debug_msg, sizeof(debug_msg), "Stop ignored - wrong state: %d", current_keyboard_state);
        pipecat_send_debug_message(debug_msg);
        ESP_LOGI(LOG_TAG, "Stop voice session ignored - current state: %d", current_keyboard_state);
    }
}

bool pipecat_is_voice_active() {
    return current_keyboard_state == KEYBOARD_STATE_TRANSCRIBING || current_keyboard_state == KEYBOARD_STATE_TYPING;
}

// Configuration system functions
void pipecat_init_config() {
    config_init_nvs();
    config_serial_init();
}

bool pipecat_load_config() {
    config_load(&g_device_config);
    return config_is_valid(&g_device_config);
}

bool pipecat_is_config_valid() {
    return g_device_config.config_valid && config_is_valid(&g_device_config);
}

const voice_keyboard_config_t* pipecat_get_config() {
    return &g_device_config;
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
      char heartbeat_msg[100];
      snprintf(heartbeat_msg, sizeof(heartbeat_msg), "Main loop running - state: %d - waiting for button", current_keyboard_state);
      pipecat_send_debug_message(heartbeat_msg);
    }

    loop_count++;
    
    // Handle button presses based on current state (minimal debug to prevent crashes)
    if (pipecat_check_button_pressed()) {
      ESP_LOGI(LOG_TAG, "Button pressed - state: %d", current_keyboard_state);
      char button_debug[100];
      snprintf(button_debug, sizeof(button_debug), "Button pressed in state: %d", current_keyboard_state);
      pipecat_send_debug_message(button_debug);
      
      switch (current_keyboard_state) {
        case KEYBOARD_STATE_IDLE:
          pipecat_send_debug_message("Button -> Starting voice session");
          pipecat_start_voice_session();
          break;
        case KEYBOARD_STATE_STARTING:
          pipecat_send_debug_message("Button -> Stopping from STARTING state");
          pipecat_stop_voice_session();
          break;
        case KEYBOARD_STATE_TRANSCRIBING:
          pipecat_send_debug_message("Button -> Stopping from TRANSCRIBING state");
          pipecat_stop_voice_session();
          break;
        case KEYBOARD_STATE_TYPING:
          pipecat_send_debug_message("Button -> Stopping from TYPING state");
          pipecat_stop_voice_session();
          break;
        case KEYBOARD_STATE_SERVER_UNAVAILABLE:
          pipecat_send_debug_message("Button -> Retrying voice session (server was unavailable)");
          // Return to idle and try again
          current_keyboard_state = KEYBOARD_STATE_IDLE;
          shared_keyboard_state = KEYBOARD_STATE_IDLE;
          shared_keyboard_ui_update_needed = true;
          ESP_LOGI(LOG_TAG, "Returning to idle state for retry");
          break;
        default:
          pipecat_send_debug_message("Button ignored - transitional state");
          ESP_LOGI(LOG_TAG, "Button ignored - transitional state");
          break;
      }
    }
    
    // Timer logic disabled - even simple state assignment breaks audio
    // Will need alternative approach for UI updates
    
    // No UI updates - screen stays static
    
    // Run WebRTC loop if not idle (includes STARTING state but excludes SERVER_UNAVAILABLE)
    if (current_keyboard_state != KEYBOARD_STATE_IDLE && current_keyboard_state != KEYBOARD_STATE_SERVER_UNAVAILABLE) {
      pipecat_webrtc_loop();
    }
            
    // Reduced delay to make button detection more responsive
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(LOG_TAG, "=== ESP32 Voice Keyboard Starting ===");
  
  // Initialize NVS first
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  
  // Initialize screen early to show config status
  pipecat_init_screen();
  ESP_LOGI(LOG_TAG, "Screen initialized");
  
  // Trigger initial config UI display
  shared_keyboard_ui_update_needed = true;
  
  // Phase 1: Initialize configuration system
  ESP_LOGI(LOG_TAG, "Phase 1: Initializing configuration system...");
  pipecat_init_config();
  
  // Check for manual configuration mode trigger
  ESP_LOGI(LOG_TAG, "Checking for manual configuration trigger...");
  printf("=== ESP32 Voice Keyboard Starting ===\n");
  printf("💡 Send 'C' within 3 seconds to force configuration mode...\n");
  fflush(stdout);
  
  bool force_config_mode = false;
  uint32_t start_time = xTaskGetTickCount();
  
  while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(3000)) {
    uint8_t input_data[10];
    int len = usb_serial_jtag_read_bytes(input_data, sizeof(input_data) - 1, pdMS_TO_TICKS(100));
    
    if (len > 0) {
      for (int i = 0; i < len; i++) {
        if (input_data[i] == 'C' || input_data[i] == 'c') {
          force_config_mode = true;
          printf("✅ Manual configuration mode triggered!\n");
          fflush(stdout);
          break;
        }
      }
      if (force_config_mode) break;
    }
  }
  
  // Phase 2: Load and validate configuration
  ESP_LOGI(LOG_TAG, "Phase 2: Loading configuration...");
  bool config_loaded = pipecat_load_config();
  bool config_valid = pipecat_is_config_valid() && !force_config_mode;
  
  config_print(&g_device_config);
  
  if (!config_valid) {
    if (force_config_mode) {
      ESP_LOGW(LOG_TAG, "Manual configuration mode requested");
      current_keyboard_state = KEYBOARD_STATE_CONFIG_MODE;
      printf("\n🔧 ESP32 Voice Keyboard - Manual Configuration Mode\n");
      printf("Previous configuration will be overwritten\n");
      printf("Run: python esp32_voice_keyboard_config.py\n\n");
      config_serial_enter_mode();
      
      // Wait in manual configuration mode
      while (current_keyboard_state == KEYBOARD_STATE_CONFIG_MODE) {
        if (!config_serial_is_active()) {
          ESP_LOGW(LOG_TAG, "Configuration mode timeout - cannot proceed without valid config");
          printf("❌ Configuration timeout - device requires setup\n");
          vTaskDelay(pdMS_TO_TICKS(5000));
          esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    } else {
      // First time setup or invalid config - request via USB JTAG immediately
      ESP_LOGI(LOG_TAG, "Configuration invalid or missing - requesting via USB JTAG");
      printf("\n📡 ESP32 Voice Keyboard - First Time Setup\n");
      printf("Requesting configuration from server via USB...\n");
      
      // Screen already initialized early - just update status
      ESP_LOGI(LOG_TAG, "Updating screen for provisioning status");
      
      current_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
      shared_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
      shared_keyboard_ui_update_needed = true;
      
      // Request full configuration via USB JTAG
      pipecat_usb_request_provisioning("full_config", &g_device_config);
      
      // Stay in provisioning state forever until valid config received
      current_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
      shared_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
      shared_keyboard_ui_update_needed = true;
      
      while (!pipecat_is_config_valid()) {
        // Check for USB JTAG response and update config
        if (pipecat_usb_check_provisioning_response(&g_device_config)) {
          ESP_LOGI(LOG_TAG, "Received provisioning response!");
          config_save(&g_device_config);
          // Re-check if config is now valid
          if (pipecat_is_config_valid()) {
            ESP_LOGI(LOG_TAG, "Configuration is now valid after provisioning!");
            break;
          } else {
            ESP_LOGW(LOG_TAG, "Configuration still invalid after provisioning response");
            config_print(&g_device_config);
          }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Re-request every 5 seconds
        static int attempts = 0;
        if (attempts % 5 == 0) {
          ESP_LOGI(LOG_TAG, "Requesting provisioning via USB JTAG...");
          pipecat_usb_request_provisioning("full_config", &g_device_config);
        }
        attempts++;
      }
      
      ESP_LOGI(LOG_TAG, "Configuration received via USB JTAG");
    }
  }
  
  // Phase 3: Initialize remaining hardware systems
  ESP_LOGI(LOG_TAG, "Phase 3: Initializing hardware systems...");
  ESP_LOGI(LOG_TAG, "Screen already initialized early");
  
  peer_init();
  ESP_LOGI(LOG_TAG, "Peer connection initialized");
  
  pipecat_init_audio_capture();
  ESP_LOGI(LOG_TAG, "Audio systems initialized");
  
  pipecat_init_wifi();
  ESP_LOGI(LOG_TAG, "WiFi system initialized");

  // Phase 4: Connect to WiFi using stored configuration
  ESP_LOGI(LOG_TAG, "Phase 4: Connecting to WiFi...");
  current_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_CONNECTING;
  shared_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_CONNECTING;
  shared_keyboard_ui_update_needed = true;
  pipecat_usb_send_provisioning_status("wifi", "connecting", "Attempting WiFi connection");
  
  if (!pipecat_connect_wifi_with_config(&g_device_config)) {
    ESP_LOGE(LOG_TAG, "Failed to connect to WiFi - requesting new credentials");
    current_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_FAILED;
    shared_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_FAILED;
    shared_keyboard_ui_update_needed = true;
    vTaskDelay(pdMS_TO_TICKS(2000));  // Show failure state
    
    // Request new WiFi config from server via USB JTAG provisioning
    ESP_LOGI(LOG_TAG, "Requesting new WiFi credentials via USB JTAG");
    current_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
    shared_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
    shared_keyboard_ui_update_needed = true;
    
    // Request new WiFi credentials via USB JTAG
    pipecat_usb_request_provisioning("wifi_config", &g_device_config);
    
    // Wait for new credentials (with timeout)
    int wifi_provisioning_attempts = 0;
    const int max_wifi_attempts = 20;  // 20 seconds timeout
    bool wifi_config_updated = false;
    
    while (!wifi_config_updated && wifi_provisioning_attempts < max_wifi_attempts) {
      // Check for USB JTAG response and update WiFi config
      if (pipecat_usb_check_provisioning_response(&g_device_config)) {
        ESP_LOGI(LOG_TAG, "Received WiFi provisioning response!");
        config_save(&g_device_config);
        wifi_config_updated = true;
        break;
      }
      
      vTaskDelay(pdMS_TO_TICKS(1000));
      wifi_provisioning_attempts++;
      
      if (wifi_provisioning_attempts % 5 == 0) {
        ESP_LOGI(LOG_TAG, "Retrying WiFi provisioning request...");
        pipecat_usb_request_provisioning("wifi_config", &g_device_config);
      }
    }
    
    if (!wifi_config_updated) {
      ESP_LOGE(LOG_TAG, "WiFi provisioning timeout - restarting to configuration mode");
      g_device_config.config_valid = false;
      config_save(&g_device_config);
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_restart();
    } else {
      // Try connecting with new credentials
      ESP_LOGI(LOG_TAG, "Received new WiFi credentials, retrying connection...");
      current_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_CONNECTING;
      shared_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_CONNECTING;
      shared_keyboard_ui_update_needed = true;
      
      if (!pipecat_connect_wifi_with_config(&g_device_config)) {
        ESP_LOGE(LOG_TAG, "WiFi connection failed with new credentials - restarting");
        g_device_config.config_valid = false;
        config_save(&g_device_config);
        esp_restart();
      }
    }
  }
  
  // WiFi connected successfully
  current_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_CONNECTED;
  shared_keyboard_state = KEYBOARD_STATE_CONFIG_WIFI_CONNECTED;
  shared_keyboard_ui_update_needed = true;
  pipecat_usb_send_provisioning_status("wifi", "connected", "WiFi connection successful");
  vTaskDelay(pdMS_TO_TICKS(1000));  // Show success state

  // Phase 4.5: Verify server connectivity
  ESP_LOGI(LOG_TAG, "Phase 4.5: Checking server connectivity...");
  current_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CHECKING;
  shared_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CHECKING;
  shared_keyboard_ui_update_needed = true;
  pipecat_usb_send_provisioning_status("server", "checking", "Verifying server connection");
  
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", g_device_config.server_port);
  if (pipecat_http_health_check(g_device_config.server_ip, port_str)) {
    ESP_LOGI(LOG_TAG, "✅ Server connectivity verified");
    current_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CONNECTED;
    shared_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CONNECTED;
    shared_keyboard_ui_update_needed = true;
    pipecat_usb_send_provisioning_status("server", "connected", "Server connection verified");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Show success state
  } else {
    ESP_LOGW(LOG_TAG, "❌ Server connectivity failed");
    current_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_FAILED;
    shared_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_FAILED;
    shared_keyboard_ui_update_needed = true;
    vTaskDelay(pdMS_TO_TICKS(2000));  // Show error state
    
    // Request new server configuration via USB JTAG first
    ESP_LOGI(LOG_TAG, "Requesting new server configuration via USB JTAG");
    current_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
    shared_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
    shared_keyboard_ui_update_needed = true;
    
    pipecat_usb_request_provisioning("server_config", &g_device_config);
    
    // Wait for new server config (with timeout)
    int server_provisioning_attempts = 0;
    const int max_server_attempts = 20;  // 20 seconds timeout
    bool server_config_updated = false;
    
    while (!server_config_updated && server_provisioning_attempts < max_server_attempts) {
      // Check for USB JTAG response and update server config
      if (pipecat_usb_check_provisioning_response(&g_device_config)) {
        ESP_LOGI(LOG_TAG, "Received server provisioning response!");
        config_save(&g_device_config);
        server_config_updated = true;
        break;
      }
      
      vTaskDelay(pdMS_TO_TICKS(1000));
      server_provisioning_attempts++;
      
      if (server_provisioning_attempts % 5 == 0) {
        ESP_LOGI(LOG_TAG, "Retrying server provisioning request...");
        pipecat_usb_request_provisioning("server_config", &g_device_config);
      }
    }
    
    if (!server_config_updated) {
      ESP_LOGW(LOG_TAG, "Server provisioning timeout - continuing anyway (server may start later)");
      // Continue with initialization - server might start later
    } else {
      // Try health check with new server config
      ESP_LOGI(LOG_TAG, "Received new server config, retrying health check...");
      current_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CHECKING;
      shared_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CHECKING;
      shared_keyboard_ui_update_needed = true;
      
      char port_str2[8];
      snprintf(port_str2, sizeof(port_str2), "%d", g_device_config.server_port);
      if (pipecat_http_health_check(g_device_config.server_ip, port_str2)) {
        ESP_LOGI(LOG_TAG, "✅ Server connectivity verified with new config");
        current_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CONNECTED;
        shared_keyboard_state = KEYBOARD_STATE_CONFIG_SERVER_CONNECTED;
        shared_keyboard_ui_update_needed = true;
        pipecat_usb_send_provisioning_status("server", "connected", "Server connection verified");
        vTaskDelay(pdMS_TO_TICKS(1000));
      } else {
        ESP_LOGW(LOG_TAG, "Server check still failed with new config - assuming WiFi network issue");
        
        // If server still can't be reached after getting new server config,
        // assume we're on different networks and request WiFi re-provisioning
        ESP_LOGI(LOG_TAG, "Requesting WiFi re-provisioning due to persistent server connectivity failure");
        current_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
        shared_keyboard_state = KEYBOARD_STATE_CONFIG_PROVISIONING;
        shared_keyboard_ui_update_needed = true;
        
        pipecat_usb_request_provisioning("wifi_config", &g_device_config);
        
        // Wait for WiFi re-provisioning response
        int wifi_reprovisioning_attempts = 0;
        const int max_wifi_attempts = 20;
        bool wifi_config_updated = false;
        
        while (!wifi_config_updated && wifi_reprovisioning_attempts < max_wifi_attempts) {
          if (pipecat_usb_check_provisioning_response(&g_device_config)) {
            ESP_LOGI(LOG_TAG, "Received WiFi re-provisioning response!");
            config_save(&g_device_config);
            wifi_config_updated = true;
            
            // Restart WiFi connection with new credentials
            ESP_LOGI(LOG_TAG, "Restarting with new WiFi configuration...");
            esp_restart(); // Simplest approach - restart the entire device
          }
          
          vTaskDelay(pdMS_TO_TICKS(1000));
          wifi_reprovisioning_attempts++;
          
          if (wifi_reprovisioning_attempts % 5 == 0) {
            ESP_LOGI(LOG_TAG, "Retrying WiFi re-provisioning request...");
            pipecat_usb_request_provisioning("wifi_config", &g_device_config);
          }
        }
        
        if (!wifi_config_updated) {
          ESP_LOGW(LOG_TAG, "WiFi re-provisioning timeout - continuing with existing config");
        }
      }
    }
  }
  
  // Phase 5: Initialize HID system (only after WiFi is connected)
  ESP_LOGI(LOG_TAG, "Phase 5: Initializing USB HID system...");
  current_keyboard_state = KEYBOARD_STATE_CONFIG_HID_READY;
  shared_keyboard_state = KEYBOARD_STATE_CONFIG_HID_READY;
  shared_keyboard_ui_update_needed = true;
  
  pipecat_init_bluetooth_hid();
  ESP_LOGI(LOG_TAG, "USB HID initialized");
  pipecat_usb_send_provisioning_status("hid", "ready", "HID system initialized");
  vTaskDelay(pdMS_TO_TICKS(2000));  // Show HID ready state
  
  // Transition to idle mode - system is now ready for normal operation
  ESP_LOGI(LOG_TAG, "System setup complete - transitioning to idle mode");
  current_keyboard_state = KEYBOARD_STATE_IDLE;
  shared_keyboard_state = KEYBOARD_STATE_IDLE;
  shared_keyboard_ui_update_needed = true;
  pipecat_usb_send_provisioning_status("system", "ready", "Voice keyboard ready for use");

  // Phase 6: System ready - enter normal operation
  ESP_LOGI(LOG_TAG, "Phase 6: System ready for voice keyboard operation");
  current_keyboard_state = KEYBOARD_STATE_IDLE;
  
  printf("\n✅ ESP32 Voice Keyboard Ready!\n");
  printf("   WiFi: %s\n", g_device_config.wifi_ssid);
  printf("   Server: %s\n", g_device_config.server_url);
  printf("   Press button to start voice typing\n\n");
  
  // Check available memory before task creation
  size_t free_heap = esp_get_free_heap_size();
  ESP_LOGI(LOG_TAG, "Free heap before main task: %zu bytes", free_heap);
  
  // Create main task
  ESP_LOGI(LOG_TAG, "Creating main task...");
  BaseType_t task_result = xTaskCreate(main_task, "MainTask", 8192, NULL, 4, NULL);
  if (task_result == pdPASS) {
    ESP_LOGI(LOG_TAG, "Main task created successfully");
  } else {
    ESP_LOGE(LOG_TAG, "Failed to create main task! Result: %d", task_result);
    size_t free_heap_after = esp_get_free_heap_size();
    ESP_LOGE(LOG_TAG, "Free heap after failure: %zu bytes", free_heap_after);
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
        case KEYBOARD_STATE_SERVER_UNAVAILABLE:
          // Return to idle and try again
          current_keyboard_state = KEYBOARD_STATE_IDLE;
          ESP_LOGI(LOG_TAG, "Returning to idle state for retry (Linux build)");
          break;
        default:
          break;
      }
    }
    
    if (current_keyboard_state != KEYBOARD_STATE_IDLE && current_keyboard_state != KEYBOARD_STATE_SERVER_UNAVAILABLE) {
      pipecat_webrtc_loop();
    }
    
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#endif
