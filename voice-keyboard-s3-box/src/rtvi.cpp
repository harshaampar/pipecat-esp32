#include <esp_log.h>
#include <peer.h>

#include "main.h"

// Meeting Assistant minimal RTVI implementation for compatibility

void pipecat_init_rtvi(PeerConnection *connection, rtvi_callbacks_t *callbacks) {
    // Minimal RTVI init for compatibility - store references but don't create queue/task
    ESP_LOGI(LOG_TAG, "RTVI initialized (minimal compatibility mode)");
}

void pipecat_rtvi_handle_message(const char *msg) {
    // Meeting assistant doesn't process RTVI messages
    ESP_LOGD(LOG_TAG, "RTVI message received but ignored: %s", msg);
}
