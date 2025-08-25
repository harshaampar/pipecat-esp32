#ifndef LINUX_BUILD

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>

#include "main.h"

// Global variables
static bool hid_initialized = false;
static bool hid_connected = true; // Always simulate connected for testing

// Text queue for async typing
#define TEXT_QUEUE_SIZE 10
#define MAX_TEXT_LENGTH 512
static QueueHandle_t hid_text_queue = NULL;
static TaskHandle_t hid_task_handle = NULL;

static void hid_stub_task(void *pvParameter) {
    ESP_LOGI(LOG_TAG, "🔧 HID stub task started on Core %d", xPortGetCoreID());
    pipecat_screen_system_log("HID stub task started");
    
    char text_buffer[MAX_TEXT_LENGTH];
    
    while (1) {
        // Wait for text to type (blocks until text is available)
        if (xQueueReceive(hid_text_queue, text_buffer, portMAX_DELAY)) {
            ESP_LOGI(LOG_TAG, "📝 STUB: Would type via HID: %s", text_buffer);
            pipecat_screen_system_log("STUB: Got text to type");
            
            // Update state to typing
            current_keyboard_state = KEYBOARD_STATE_TYPING;
            shared_keyboard_state = KEYBOARD_STATE_TYPING;
            shared_keyboard_ui_update_needed = true;
            
            // Simulate typing delay
            vTaskDelay(pdMS_TO_TICKS(strlen(text_buffer) * 20)); // 20ms per character
            
            ESP_LOGI(LOG_TAG, "✅ STUB: Finished simulated typing");
            pipecat_screen_system_log("STUB: Finished typing");
            
            // Return to transcribing state (ready for more text)
            current_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
            shared_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
            shared_keyboard_ui_update_needed = true;
        }
    }
}

void pipecat_init_bluetooth_hid() {
    if (hid_initialized) {
        return;
    }
    
    ESP_LOGI(LOG_TAG, "🔧 Initializing HID stub (debugging mode)...");
    pipecat_screen_system_log("Initializing HID stub");
    
    // Create text queue for async typing
    hid_text_queue = xQueueCreate(TEXT_QUEUE_SIZE, MAX_TEXT_LENGTH);
    if (hid_text_queue == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to create HID text queue");
        pipecat_screen_system_log("ERROR: Queue creation failed");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "✅ HID text queue created");
    pipecat_screen_system_log("HID queue created");
    
    // Create high-priority HID task on Core 0
    BaseType_t result = xTaskCreatePinnedToCore(hid_stub_task, "HID Stub", 4096, NULL, 6, &hid_task_handle, 0);
    if (result != pdPASS) {
        ESP_LOGE(LOG_TAG, "Failed to create HID stub task");
        pipecat_screen_system_log("ERROR: Task creation failed");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "✅ HID stub task created");
    pipecat_screen_system_log("HID task created");
    
    hid_initialized = true;
    ESP_LOGI(LOG_TAG, "✅ HID stub initialized successfully");
    pipecat_screen_system_log("HID stub ready!");
}

void pipecat_queue_text_for_typing(const char* text) {
    if (!hid_initialized || hid_text_queue == NULL) {
        ESP_LOGW(LOG_TAG, "⚠️ HID stub not initialized, cannot queue text");
        pipecat_screen_system_log("ERROR: HID not initialized");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "📥 Queueing text for stub typing: %.50s%s", text, strlen(text) > 50 ? "..." : "");
    pipecat_screen_system_log("Text queued for typing");
    
    if (strlen(text) >= MAX_TEXT_LENGTH) {
        ESP_LOGW(LOG_TAG, "Text too long for queue, truncating");
    }
    
    // Queue text for typing (non-blocking)
    if (xQueueSend(hid_text_queue, text, 0) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "⚠️ HID text queue full, dropping text");
        pipecat_screen_system_log("ERROR: Queue full");
    } else {
        ESP_LOGI(LOG_TAG, "✅ Text successfully queued");
    }
}

bool pipecat_is_bluetooth_connected() {
    return hid_connected;
}

#else
// Linux build stubs
void pipecat_init_bluetooth_hid() {
    ESP_LOGI(LOG_TAG, "HID stub not available in Linux build");
}

void pipecat_queue_text_for_typing(const char* text) {
    ESP_LOGI(LOG_TAG, "Would type: %s", text);
}

bool pipecat_is_bluetooth_connected() {
    return true; // Simulate connected state for testing
}

#endif