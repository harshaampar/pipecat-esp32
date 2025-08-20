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
#ifdef LOG_DATACHANNEL_MESSAGES
  ESP_LOGI(LOG_TAG, "DataChannel Message: %s", msg);
#endif
  pipecat_rtvi_handle_message(msg);
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
    
    // Set meeting state to ACTIVE from WebRTC callback context
    extern meeting_state_t current_meeting_state;
    extern volatile bool shared_meeting_ui_update_needed;
    extern volatile unsigned long shared_meeting_duration;
    
    current_meeting_state = MEETING_STATE_ACTIVE;
    shared_meeting_duration = 0; // Start from 0 seconds
    shared_meeting_ui_update_needed = true; // Trigger initial UI creation
    
    ESP_LOGI(LOG_TAG, "WebRTC callback: State set to ACTIVE, UI update requested");
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
    const char* disconnect_msg = "{\"type\":\"meeting.disconnect\",\"message\":\"ESP32 meeting ended\"}";
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