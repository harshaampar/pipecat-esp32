#ifndef LINUX_BUILD

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#include "main.h"

// TinyUSB HID descriptors and configuration
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

// HID report descriptor for keyboard only
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

// String descriptor
const char* hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "ESP32-S3",            // 1: Manufacturer
    "Voice Keyboard",      // 2: Product
    "123456",              // 3: Serials
    "Voice Keyboard HID",  // 4: HID
};

// Configuration descriptor
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// Global variables
static bool usb_hid_initialized = false;
static bool usb_hid_connected = false;

// Text queue for async typing
#define TEXT_QUEUE_SIZE 10
#define MAX_TEXT_LENGTH 512
static QueueHandle_t usb_text_queue = NULL;
static TaskHandle_t usb_task_handle = NULL;

// USB HID keycode mapping for common characters
static uint8_t char_to_keycode(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 0x04;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 0x04;
    if (c >= '1' && c <= '9') return c - '1' + 0x1E;
    if (c == '0') return 0x27;
    if (c == ' ') return 0x2C;
    if (c == '\n') return 0x28; // Enter
    if (c == '.') return 0x37;
    if (c == ',') return 0x36;
    if (c == '?') return 0x38;
    if (c == '!') return 0x1E;
    if (c == ':') return 0x33;
    if (c == ';') return 0x33;
    if (c == '\'') return 0x34;
    if (c == '"') return 0x34;
    if (c == '-') return 0x2D;
    if (c == '_') return 0x2D;
    if (c == '(') return 0x26;
    if (c == ')') return 0x27;
    return 0x00; // Unknown character
}

static uint8_t char_needs_shift(char c) {
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '!' || c == '@' || c == '#' || c == '$' || c == '%') return 1;
    if (c == '^' || c == '&' || c == '*' || c == '(' || c == ')') return 1;
    if (c == '_' || c == '+' || c == '{' || c == '}' || c == '|') return 1;
    if (c == ':' || c == '"' || c == '<' || c == '>' || c == '?') return 1;
    return 0;
}

static void send_keyboard_report(uint8_t modifier, uint8_t keycode) {
    if (usb_hid_connected && tud_mounted() && tud_hid_ready()) {
        uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
        bool ret = tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycodes);
        if (!ret) {
            ESP_LOGW(LOG_TAG, "❌ Failed to send USB HID keyboard report");
        }
    } else {
        ESP_LOGW(LOG_TAG, "⚠️  Cannot send report: connected=%d mounted=%d ready=%d", 
                 usb_hid_connected, tud_mounted(), tud_hid_ready());
    }
}

static void type_character(char c) {
    uint8_t keycode = char_to_keycode(c);
    uint8_t modifier = char_needs_shift(c) ? 0x02 : 0x00; // Left shift
    
    if (keycode != 0x00) {
        // Press key
        send_keyboard_report(modifier, keycode);
        vTaskDelay(pdMS_TO_TICKS(1)); // Minimal delay for HID protocol
        
        // Release key
        send_keyboard_report(0x00, 0x00);
        vTaskDelay(pdMS_TO_TICKS(1)); // Minimal delay between characters
    }
}

static void usb_hid_task(void *pvParameter) {
    ESP_LOGI(LOG_TAG, "USB HID task started on Core %d", xPortGetCoreID());
    
    char text_buffer[MAX_TEXT_LENGTH];
    
    while (1) {
        // Wait for text to type (blocks until text is available)
        if (xQueueReceive(usb_text_queue, text_buffer, portMAX_DELAY)) {
            ESP_LOGI(LOG_TAG, "📥 Received text to type: %s", text_buffer);
            
            if (usb_hid_connected) {
                ESP_LOGI(LOG_TAG, "⌨️  Typing text via USB HID: %s", text_buffer);
                pipecat_screen_system_log("Typing via USB HID");
                
                // Update state to typing
                current_keyboard_state = KEYBOARD_STATE_TYPING;
                shared_keyboard_state = KEYBOARD_STATE_TYPING;
                shared_keyboard_ui_update_needed = true;
                
                // Type each character at maximum speed
                for (int i = 0; text_buffer[i] != '\0'; i++) {
                    type_character(text_buffer[i]);
                }
                
                ESP_LOGI(LOG_TAG, "Finished typing text via USB HID");
                
                // Return to transcribing state (ready for more text)
                current_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
                shared_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
                shared_keyboard_ui_update_needed = true;
            } else {
                ESP_LOGW(LOG_TAG, "❌ USB HID not connected, dropping text: %s", text_buffer);
                pipecat_screen_system_log("USB HID not connected!");
            }
        }
    }
}

// TinyUSB HID callbacks
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

// TinyUSB mount/unmount callbacks
void tud_mount_cb(void) {
    ESP_LOGI(LOG_TAG, "🔌 USB HID mounted - device connected");
    usb_hid_connected = true;
    pipecat_screen_system_log("USB HID Connected");
}

void tud_unmount_cb(void) {
    ESP_LOGI(LOG_TAG, "🔌 USB HID unmounted - device disconnected");
    usb_hid_connected = false;
    pipecat_screen_system_log("USB HID Disconnected");
}


void pipecat_init_bluetooth_hid() {
    if (usb_hid_initialized) {
        return;
    }
    
    ESP_LOGI(LOG_TAG, "⌨️  Initializing USB HID keyboard...");
    pipecat_screen_system_log("Initializing USB HID");
    
    // Initialize TinyUSB stack with HID support
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };
    
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(LOG_TAG, "✅ TinyUSB HID keyboard driver installed");
    pipecat_screen_system_log("USB HID driver ready");
    
    // Create text queue for async typing
    usb_text_queue = xQueueCreate(TEXT_QUEUE_SIZE, MAX_TEXT_LENGTH);
    if (usb_text_queue == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to create USB text queue");
        return;
    }
    
    // Create high-priority USB HID task on Core 0
    xTaskCreatePinnedToCore(usb_hid_task, "USB HID", 4096, NULL, 6, &usb_task_handle, 0);
    
    usb_hid_initialized = true;
    ESP_LOGI(LOG_TAG, "USB HID keyboard initialized successfully");
}

void pipecat_queue_text_for_typing(const char* text) {
    if (!usb_hid_initialized || usb_text_queue == NULL) {
        ESP_LOGW(LOG_TAG, "USB HID not initialized, cannot queue text");
        return;
    }
    
    if (strlen(text) >= MAX_TEXT_LENGTH) {
        ESP_LOGW(LOG_TAG, "Text too long for queue, truncating");
    }
    
    // Queue text for typing (non-blocking)
    if (xQueueSend(usb_text_queue, text, 0) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "USB HID text queue full, dropping text");
    } else {
        ESP_LOGI(LOG_TAG, "Queued text for USB HID typing: %.50s%s", text, strlen(text) > 50 ? "..." : "");
    }
}

bool pipecat_is_bluetooth_connected() {
    return usb_hid_connected;
}

#else
// Linux build stubs
void pipecat_init_bluetooth_hid() {
    ESP_LOGI(LOG_TAG, "USB HID not available in Linux build");
}

void pipecat_queue_text_for_typing(const char* text) {
    ESP_LOGI(LOG_TAG, "Would type: %s", text);
}

bool pipecat_is_bluetooth_connected() {
    return true; // Simulate connected state for testing
}

#endif