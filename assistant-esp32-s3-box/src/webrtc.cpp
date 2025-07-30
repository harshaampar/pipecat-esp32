#ifndef LINUX_BUILD
#include <driver/i2s_std.h>
#include <opus.h>
#endif

#include <esp_event.h>
#include <esp_log.h>
#include <string.h>

#include "main.h"

static PeerConnection *peer_connection = NULL;
static connection_state_t current_connection_state = CONNECTION_STATE_DISCONNECTED;
static uint32_t connection_start_time = 0;
static const uint32_t CONNECTION_TIMEOUT_MS = 15000; // 15 seconds timeout

#ifndef LINUX_BUILD
static TaskHandle_t audio_task_handle = NULL;

void pipecat_send_audio_task(void *user_data) {
  pipecat_init_audio_encoder();

  while (1) {
    if (current_connection_state == CONNECTION_STATE_CONNECTED && peer_connection != NULL) {
      pipecat_send_audio(peer_connection);
    }
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
    
    // Check if we should exit the task
    if (current_connection_state == CONNECTION_STATE_DISCONNECTED) {
      ESP_LOGI(LOG_TAG, "Audio task exiting");
      audio_task_handle = NULL;
      vTaskDelete(NULL);
    }
  }
}

static void stop_audio_task() {
  if (audio_task_handle != NULL) {
    ESP_LOGI(LOG_TAG, "Stopping audio task");
    vTaskDelete(audio_task_handle);
    audio_task_handle = NULL;
  }
}

static void start_audio_task() {
  if (audio_task_handle == NULL) {
    ESP_LOGI(LOG_TAG, "Starting audio task");
    xTaskCreatePinnedToCore(
        pipecat_send_audio_task,
        "audio_publisher", 
        8192,  // Reduced stack size
        NULL, 
        5,     // Lower priority
        &audio_task_handle, 
        0      // Core 0
    );
  }
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
  if (peer_connection_create_datachannel(peer_connection, DATA_CHANNEL_RELIABLE,
                                         0, 0, (char *)"rtvi-ai",
                                         (char *)"") != -1) {
    ESP_LOGI(LOG_TAG, "DataChannel created");
  } else {
    ESP_LOGE(LOG_TAG, "Failed to create DataChannel");
  }
}

static void pipecat_onconnectionstatechange_task(PeerConnectionState state,
                                                 void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));

  if (state == PEER_CONNECTION_DISCONNECTED ||
      state == PEER_CONNECTION_CLOSED ||
      state == PEER_CONNECTION_FAILED) {
    pipecat_set_connection_state(CONNECTION_STATE_DISCONNECTED);
    pipecat_screen_update_status("Disconnected - Tap Connect to reconnect");
#ifndef LINUX_BUILD
    stop_audio_task();
#endif
  } else if (state == PEER_CONNECTION_CONNECTED) {
    pipecat_set_connection_state(CONNECTION_STATE_CONNECTED);
    pipecat_screen_update_status("Connected - Voice assistant ready!");
#ifndef LINUX_BUILD
    start_audio_task();
    pipecat_init_rtvi(peer_connection, &pipecat_rtvi_callbacks);
#endif
  }
}

static void pipecat_on_icecandidate_task(char *description, void *user_data) {
  char *local_buffer = (char *)malloc(MAX_HTTP_OUTPUT_BUFFER + 1);
  memset(local_buffer, 0, MAX_HTTP_OUTPUT_BUFFER + 1);
  
  esp_err_t result = pipecat_http_request(description, local_buffer);
  if (result != ESP_OK) {
    ESP_LOGE(LOG_TAG, "HTTP request failed");
    free(local_buffer);
    // Connection failed, reset state
    pipecat_set_connection_state(CONNECTION_STATE_DISCONNECTED);
    pipecat_screen_update_status("Connection failed - Tap Connect to retry");
    return;
  }
  
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
#ifndef LINUX_BUILD
        pipecat_audio_decode(data, size);
#endif
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

  // Don't create offer automatically - wait for button press
}

void pipecat_webrtc_connect() {
  if (current_connection_state == CONNECTION_STATE_DISCONNECTED) {
    ESP_LOGI(LOG_TAG, "Connecting to server...");
    pipecat_set_connection_state(CONNECTION_STATE_CONNECTING);
    connection_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    pipecat_screen_update_status("Connecting to voice assistant...");
    peer_connection_create_offer(peer_connection);
  }
}

void pipecat_webrtc_disconnect() {
  if (current_connection_state != CONNECTION_STATE_DISCONNECTED) {
    ESP_LOGI(LOG_TAG, "Disconnecting from server...");
    pipecat_set_connection_state(CONNECTION_STATE_DISCONNECTED);
    pipecat_screen_update_status("Disconnecting...");
    
#ifndef LINUX_BUILD
    // Stop audio task first
    stop_audio_task();
#endif

    // Close the peer connection
    if (peer_connection) {
      peer_connection_destroy(peer_connection);
      // Recreate the peer connection for future use
      pipecat_init_webrtc();
    }
    pipecat_screen_update_status("Disconnected - Tap Connect button");
  }
}

connection_state_t pipecat_get_connection_state() {
  return current_connection_state;
}

void pipecat_set_connection_state(connection_state_t state) {
  current_connection_state = state;
}

void pipecat_webrtc_loop() {
  if (peer_connection) {
    peer_connection_loop(peer_connection);
  }
  
  // Check for connection timeout
  if (current_connection_state == CONNECTION_STATE_CONNECTING) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (current_time - connection_start_time > CONNECTION_TIMEOUT_MS) {
      ESP_LOGW(LOG_TAG, "Connection timeout");
      pipecat_webrtc_disconnect();
      pipecat_screen_update_status("Connection timeout - Tap Connect to retry");
    }
  }
}
