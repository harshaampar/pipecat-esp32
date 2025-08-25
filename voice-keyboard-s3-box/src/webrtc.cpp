#ifndef LINUX_BUILD
#include <driver/i2s_std.h>
#include <opus.h>
#endif

#include <esp_event.h>
#include <esp_log.h>
#include <string.h>

#include "main.h"

static PeerConnection *peer_connection = NULL;
static volatile bool audio_task_should_stop = false;
static TaskHandle_t audio_task_handle = NULL;

#ifndef LINUX_BUILD
StaticTask_t task_buffer;
void pipecat_send_audio_task(void *user_data) {
  pipecat_init_audio_encoder();
  ESP_LOGI(LOG_TAG, "Audio task started on Core %d", xPortGetCoreID());

  while (!audio_task_should_stop && peer_connection != NULL) {
    pipecat_send_audio(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
  
  ESP_LOGI(LOG_TAG, "Audio task stopping gracefully");
  audio_task_handle = NULL; // Clear handle before task ends
  vTaskDelete(NULL); // Delete self
}
#endif

static void pipecat_ondatachannel_onmessage_task(char *msg, size_t len,
                                                 void *userdata, uint16_t sid) {
  ESP_LOGI(LOG_TAG, "DataChannel Message received (len=%d): %s", len, msg);
  
  // Debug: Show what message format we received
  char debug_msg[200];
  snprintf(debug_msg, sizeof(debug_msg), "DataChannel msg: '%.100s'", msg);
  pipecat_send_debug_message(debug_msg);
  
  // Check if this is a text message for keyboard typing (handle spaces in JSON)
  if (strstr(msg, "transcribed_text") != NULL) {
    pipecat_send_debug_message("Received transcribed_text from server");
    // Parse JSON to extract the text content (handle spaces)
    char *text_start = strstr(msg, "\"text\":");
    if (text_start != NULL) {
      // Skip past "text": and any spaces, then find opening quote
      text_start += 7; // Skip past "text":
      while (*text_start == ' ') text_start++; // Skip spaces
      if (*text_start == '"') text_start++; // Skip opening quote
      char *text_end = strchr(text_start, '"');
      if (text_end != NULL) {
        size_t text_len = text_end - text_start;
        if (text_len > 0 && text_len < 512) {
          char text_buffer[512];
          strncpy(text_buffer, text_start, text_len);
          text_buffer[text_len] = '\0';
          
          char debug_msg[300];
          snprintf(debug_msg, sizeof(debug_msg), "Parsed text to type: '%.100s'", text_buffer);
          pipecat_send_debug_message(debug_msg);
          
          pipecat_queue_text_for_typing(text_buffer);
        }
      }
    }
  } else {
    // Handle other RTVI messages
    pipecat_rtvi_handle_message(msg);
  }
}

static void pipecat_ondatachannel_onopen_task(void *userdata) {
  ESP_LOGI(LOG_TAG, "DataChannel open callback triggered");
  int dc_result = peer_connection_create_datachannel(peer_connection, DATA_CHANNEL_RELIABLE,
                                                     0, 0, (char *)"rtvi-ai",
                                                     (char *)"");
  if (dc_result != -1) {
    ESP_LOGI(LOG_TAG, "DataChannel created successfully, ID: %d", dc_result);
  } else {
    ESP_LOGE(LOG_TAG, "Failed to create DataChannel");
  }
}

static void pipecat_onconnectionstatechange_task(PeerConnectionState state,
                                                 void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));

  if (state == PEER_CONNECTION_DISCONNECTED ||
      state == PEER_CONNECTION_CLOSED) {
#ifndef LINUX_BUILD
    esp_restart();
#endif
  } else if (state == PEER_CONNECTION_CONNECTED) {
    ESP_LOGI(LOG_TAG, "WebRTC connected - starting audio task");
#ifndef LINUX_BUILD
    // Reset stop flag before creating new audio task
    audio_task_should_stop = false;
    
    StackType_t *stack_memory = (StackType_t *)heap_caps_malloc(
        30000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    audio_task_handle = xTaskCreateStaticPinnedToCore(pipecat_send_audio_task, "audio_publisher",
                                  30000, NULL, 7, stack_memory, &task_buffer,
                                  1);
    ESP_LOGI(LOG_TAG, "Audio task created with SPIRAM stack on Core 1");
    pipecat_init_rtvi(peer_connection, &pipecat_rtvi_callbacks);
    ESP_LOGI(LOG_TAG, "RTVI initialized");
    
    // Set keyboard state to TRANSCRIBING from WebRTC callback context
    extern keyboard_state_t current_keyboard_state;
    extern volatile keyboard_state_t shared_keyboard_state;
    extern volatile bool shared_keyboard_ui_update_needed;
    
    current_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
    shared_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
    shared_keyboard_ui_update_needed = true; // Trigger initial UI creation
    
    ESP_LOGI(LOG_TAG, "WebRTC callback: State set to TRANSCRIBING, UI update requested");
#endif
  }
}

static void pipecat_on_icecandidate_task(char *description, void *user_data) {
  char *local_buffer = (char *)malloc(MAX_HTTP_OUTPUT_BUFFER + 1);
  memset(local_buffer, 0, MAX_HTTP_OUTPUT_BUFFER + 1);
  pipecat_http_request(description, local_buffer);
  peer_connection_set_remote_description(peer_connection, local_buffer,
                                         SDP_TYPE_ANSWER);
  free(local_buffer);
}

void pipecat_init_webrtc() {
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_STRING,
      .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {
      },
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_connection = peer_connection_create(&peer_connection_config);
  if (peer_connection == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create peer connection");
#ifndef LINUX_BUILD
    esp_restart();
#endif
  }

  peer_connection_oniceconnectionstatechange(
      peer_connection, pipecat_onconnectionstatechange_task);
  peer_connection_onicecandidate(peer_connection, pipecat_on_icecandidate_task);
  peer_connection_ondatachannel(peer_connection,
                                pipecat_ondatachannel_onmessage_task,
                                pipecat_ondatachannel_onopen_task, NULL);

  peer_connection_create_offer(peer_connection);
}

void pipecat_webrtc_loop() {
  peer_connection_loop(peer_connection);
}

void pipecat_send_disconnect_message() {
  if (peer_connection && pipecat_is_webrtc_connected()) {
    const char* disconnect_msg = "{\"type\":\"session.disconnect\",\"message\":\"ESP32 voice session ended\"}";
    int result = peer_connection_datachannel_send(peer_connection, (char*)disconnect_msg, strlen(disconnect_msg));
    if (result == 0) {
      ESP_LOGI(LOG_TAG, "Disconnect message sent to server");
    } else {
      ESP_LOGE(LOG_TAG, "Failed to send disconnect message: %d", result);
    }
    
    // Give time for message to be sent
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void pipecat_stop_webrtc() {
  ESP_LOGI(LOG_TAG, "Stopping WebRTC connection");
  
  // Send explicit disconnect message to server before closing
  pipecat_send_disconnect_message();
  
  // First, signal audio task to stop gracefully
  if (audio_task_handle != NULL) {
    ESP_LOGI(LOG_TAG, "Signaling audio task to stop");
    audio_task_should_stop = true;
    
    // Wait for audio task to finish (max 1 second)
    int wait_count = 0;
    while (audio_task_handle != NULL && wait_count < 100) {
      vTaskDelay(pdMS_TO_TICKS(10));
      wait_count++;
    }
    
    if (audio_task_handle == NULL) {
      ESP_LOGI(LOG_TAG, "Audio task stopped gracefully");
    } else {
      ESP_LOGE(LOG_TAG, "Audio task did not stop, forcing cleanup");
      audio_task_handle = NULL;
    }
  }
  
  if (peer_connection) {
    // Check current connection state
    PeerConnectionState state = peer_connection_get_state(peer_connection);
    ESP_LOGI(LOG_TAG, "WebRTC connection state before close: %s", peer_connection_state_to_string(state));
    
    // Properly close the connection (this should trigger server disconnect event)
    peer_connection_close(peer_connection);
    ESP_LOGI(LOG_TAG, "WebRTC connection closed, cleaning up resources");
    
    // Small delay to ensure close signal is sent
    vTaskDelay(pdMS_TO_TICKS(50));
    
    peer_connection_destroy(peer_connection);
    peer_connection = NULL;
    ESP_LOGI(LOG_TAG, "WebRTC connection destroyed and cleaned up");
  } else {
    ESP_LOGI(LOG_TAG, "No WebRTC connection to stop");
  }
  
  // Reset flags for next connection
  audio_task_should_stop = false;
}

bool pipecat_is_webrtc_connected() {
  if (peer_connection == NULL) {
    return false;
  }
  PeerConnectionState state = peer_connection_get_state(peer_connection);
  return (state == PEER_CONNECTION_CONNECTED || state == PEER_CONNECTION_COMPLETED);
}

void pipecat_send_debug_message(const char* message) {
  // Use the HTTP debug function
  pipecat_http_debug_message(message);
}