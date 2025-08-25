#ifndef LINUX_BUILD

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#include "main.h"

// USB HID Keyboard Report Descriptor
static const uint8_t hid_keyboard_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// Configuration Descriptor
enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID 0x81

static const uint8_t hid_configuration_descriptor[] = {
    // Configuration Descriptor
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    
    // HID Interface Descriptor
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_keyboard_report_descriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10)
};

// String Descriptors
static const char* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Language (English)
    "Espressif",                   // 1: Manufacturer
    "ESP32-S3 HID Keyboard",       // 2: Product
    "123456",                      // 3: Serial Number
    "HID Interface",               // 4: HID Interface
};

// Global variables
static bool usb_hid_initialized = false;
static bool usb_hid_connected = false;

// Text queue for async typing
#define TEXT_QUEUE_SIZE 10
#define MAX_TEXT_LENGTH 512
static QueueHandle_t usb_text_queue = NULL;
static TaskHandle_t usb_task_handle = NULL;

// USB HID keycode mapping - correct USB HID usage IDs
static uint8_t char_to_keycode(char c) {
    // Convert to lowercase for mapping
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    
    // Letters: a=0x04, b=0x05, ..., z=0x1D
    if (c >= 'a' && c <= 'z') return c - 'a' + 0x04;
    
    // Numbers: 1=0x1E, 2=0x1F, ..., 9=0x26, 0=0x27
    if (c >= '1' && c <= '9') return c - '1' + 0x1E;
    if (c == '0') return 0x27;
    
    // Common symbols
    switch (c) {
        case ' ': return 0x2C; // Space
        case '\n': return 0x28; // Enter/Return
        case '\t': return 0x2B; // Tab
        case '.': return 0x37; // Period/Dot
        case ',': return 0x36; // Comma
        case ';': return 0x33; // Semicolon
        case '\'': return 0x34; // Apostrophe/Quote
        case '`': return 0x35; // Grave accent
        case '-': return 0x2D; // Hyphen/Minus
        case '=': return 0x2E; // Equal
        case '[': return 0x2F; // Left bracket
        case ']': return 0x30; // Right bracket
        case '\\': return 0x31; // Backslash
        case '/': return 0x38; // Forward slash
        
        // Characters requiring shift
        case '!': return 0x1E; // 1 + shift
        case '@': return 0x1F; // 2 + shift  
        case '#': return 0x20; // 3 + shift
        case '$': return 0x21; // 4 + shift
        case '%': return 0x22; // 5 + shift
        case '^': return 0x23; // 6 + shift
        case '&': return 0x24; // 7 + shift
        case '*': return 0x25; // 8 + shift
        case '(': return 0x26; // 9 + shift
        case ')': return 0x27; // 0 + shift
        case '_': return 0x2D; // - + shift
        case '+': return 0x2E; // = + shift
        case '{': return 0x2F; // [ + shift
        case '}': return 0x30; // ] + shift
        case '|': return 0x31; // \ + shift
        case ':': return 0x33; // ; + shift
        case '"': return 0x34; // ' + shift
        case '<': return 0x36; // , + shift
        case '>': return 0x37; // . + shift
        case '?': return 0x38; // / + shift
        case '~': return 0x35; // ` + shift
    }
    
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
    if (!usb_hid_connected || !tud_mounted()) {
        pipecat_send_debug_message("HID not connected/mounted");
        return;
    }
    
    // Wait for HID to be ready (up to 500ms)
    int wait_count = 0;
    while (!tud_hid_ready() && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }
    
    if (!tud_hid_ready()) {
        pipecat_send_debug_message("HID timeout - not ready after wait");
        return;
    }
    
    // Create proper HID keyboard report - 8 bytes total
    uint8_t report[8] = {0};
    report[0] = modifier;  // Modifier byte
    report[2] = keycode;   // First keycode (report[1] is reserved)
    
    bool ret = tud_hid_n_report(0, 0, report, 8);
    if (!ret) {
        pipecat_send_debug_message("HID report send FAILED");
    } else {
        // Only debug first few reports to avoid spam
        static int report_count = 0;
        if (report_count < 3) {
            char debug_msg[100];
            snprintf(debug_msg, sizeof(debug_msg), "HID report OK: mod=0x%02x key=0x%02x", modifier, keycode);
            pipecat_send_debug_message(debug_msg);
            report_count++;
        }
    }
}

static void type_character(char c) {
    uint8_t keycode = char_to_keycode(c);
    uint8_t modifier = char_needs_shift(c) ? 0x02 : 0x00; // Left shift
    
    if (keycode != 0x00) {
        // Press key
        send_keyboard_report(modifier, keycode);
        vTaskDelay(pdMS_TO_TICKS(20)); // Longer delay for HID processing
        
        // Release key (send empty report)
        send_keyboard_report(0x00, 0x00);
        vTaskDelay(pdMS_TO_TICKS(30)); // Even longer delay between characters
    } else {
        // Send debug for unknown characters
        char debug_msg[50];
        snprintf(debug_msg, sizeof(debug_msg), "Unknown char: '%c' (0x%02x)", c, c);
        pipecat_send_debug_message(debug_msg);
    }
}

static void usb_hid_task(void *pvParameter) {
    ESP_LOGI(LOG_TAG, "⌨️  USB HID task started on Core %d", xPortGetCoreID());
    
    char text_buffer[MAX_TEXT_LENGTH];
    
    while (1) {
        // Wait for text to type (blocks until text is available)
        if (xQueueReceive(usb_text_queue, text_buffer, portMAX_DELAY)) {
            ESP_LOGI(LOG_TAG, "📝 Received text to type: %s", text_buffer);
            pipecat_send_debug_message("Text received for typing");
            
            if (usb_hid_connected && tud_mounted()) {
                pipecat_send_debug_message("USB HID ready - starting to type");
                ESP_LOGI(LOG_TAG, "⌨️  Typing text via USB HID: %s", text_buffer);
                
                // Update state to typing (no UI updates)
                current_keyboard_state = KEYBOARD_STATE_TYPING;
                
                // Type each character
                pipecat_send_debug_message("Starting character typing loop");
                int char_count = 0;
                for (int i = 0; text_buffer[i] != '\0'; i++) {
                    char_count++;
                    
                    // Debug first character
                    if (i == 0) {
                        char first_char_msg[50];
                        snprintf(first_char_msg, sizeof(first_char_msg), "Typing first char: '%c'", text_buffer[i]);
                        pipecat_send_debug_message(first_char_msg);
                    }
                    
                    type_character(text_buffer[i]);
                    
                    // Debug every 10 characters to reduce HTTP overhead
                    if (char_count % 10 == 0 || text_buffer[i+1] == '\0') {
                        char progress_msg[100];
                        snprintf(progress_msg, sizeof(progress_msg), "Typed %d chars", char_count);
                        pipecat_send_debug_message(progress_msg);
                    }
                }
                
                ESP_LOGI(LOG_TAG, "✅ Finished typing text via USB HID");
                pipecat_send_debug_message("USB HID typing COMPLETED");
                
                // Return to transcribing state (ready for more text)
                current_keyboard_state = KEYBOARD_STATE_TRANSCRIBING;
            } else {
                char debug_status[200];
                snprintf(debug_status, sizeof(debug_status), "USB HID NOT READY: connected=%d mounted=%d", 
                         usb_hid_connected, tud_mounted());
                pipecat_send_debug_message(debug_status);
                ESP_LOGW(LOG_TAG, "❌ USB HID not ready, dropping text: %s", text_buffer);
            }
        }
    }
}

// Required TinyUSB callbacks
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return hid_keyboard_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) bufsize;
}

// TinyUSB mount/unmount callbacks
void tud_mount_cb(void) {
    ESP_LOGI(LOG_TAG, "🔌 USB HID mounted - device connected");
    usb_hid_connected = true;
    pipecat_send_debug_message("USB HID MOUNTED - macOS recognized device!");
}

void tud_unmount_cb(void) {
    ESP_LOGI(LOG_TAG, "🔌 USB HID unmounted - device disconnected");
    usb_hid_connected = false;
    pipecat_send_debug_message("USB HID UNMOUNTED - device disconnected");
}

void pipecat_init_bluetooth_hid() {
    if (usb_hid_initialized) {
        return;
    }
    
    ESP_LOGI(LOG_TAG, "⌨️  Initializing simple USB HID keyboard...");
    pipecat_send_debug_message("USB HID initialization started");
    
    // Initialize TinyUSB with proper HID keyboard descriptors
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,                    // Use TinyUSB default device descriptor
        .string_descriptor = string_desc_arr,        // Use our string descriptors
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,                       // Use internal USB PHY
        .configuration_descriptor = hid_configuration_descriptor, // Use our HID configuration
    };
    
    ESP_LOGI(LOG_TAG, "TinyUSB config prepared, installing driver...");
    pipecat_send_debug_message("Installing TinyUSB driver with default HID config");
    
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        char error_msg[200];
        snprintf(error_msg, sizeof(error_msg), "TinyUSB install FAILED: %s (0x%x)", esp_err_to_name(ret), ret);
        ESP_LOGE(LOG_TAG, "❌ Failed to install TinyUSB driver: %s", esp_err_to_name(ret));
        pipecat_send_debug_message(error_msg);
        return;
    }
    
    ESP_LOGI(LOG_TAG, "✅ TinyUSB driver installed");
    pipecat_send_debug_message("TinyUSB driver installed successfully");
    
    // Create text queue for async typing
    usb_text_queue = xQueueCreate(TEXT_QUEUE_SIZE, MAX_TEXT_LENGTH);
    if (usb_text_queue == NULL) {
        ESP_LOGE(LOG_TAG, "❌ Failed to create USB text queue");
        pipecat_send_debug_message("USB text queue creation FAILED");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "✅ USB text queue created");
    pipecat_send_debug_message("USB text queue created");
    
    // Create USB HID task on Core 0 with reasonable stack size
    BaseType_t task_result = xTaskCreatePinnedToCore(usb_hid_task, "USB HID", 6144, NULL, 3, &usb_task_handle, 0);
    if (task_result != pdPASS) {
        ESP_LOGE(LOG_TAG, "❌ Failed to create USB HID task");
        pipecat_send_debug_message("USB HID task creation FAILED");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "✅ USB HID task created");
    pipecat_send_debug_message("USB HID task created on Core 0");
    
    usb_hid_initialized = true;
    ESP_LOGI(LOG_TAG, "✅ Simple USB HID keyboard initialized successfully");
    pipecat_send_debug_message("USB HID keyboard initialization COMPLETE");
}

void pipecat_queue_text_for_typing(const char* text) {
    // Critical safety checks
    if (!text) {
        ESP_LOGE(LOG_TAG, "❌ NULL text pointer");
        return;
    }
    
    if (!usb_hid_initialized || usb_text_queue == NULL) {
        ESP_LOGW(LOG_TAG, "⚠️ USB HID not initialized, cannot queue text");
        return;
    }
    
    size_t text_len = strlen(text);
    if (text_len >= MAX_TEXT_LENGTH) {
        ESP_LOGW(LOG_TAG, "Text too long (%d chars), truncating", text_len);
    }
    
    ESP_LOGI(LOG_TAG, "📥 Queueing text for USB HID typing: %.50s%s", text, text_len > 50 ? "..." : "");
    
    // Create local copy to ensure safety
    char safe_buffer[MAX_TEXT_LENGTH];
    strncpy(safe_buffer, text, MAX_TEXT_LENGTH - 1);
    safe_buffer[MAX_TEXT_LENGTH - 1] = '\0';
    
    // Queue text for typing with timeout
    if (xQueueSend(usb_text_queue, safe_buffer, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "⚠️ USB HID text queue full or error");
        pipecat_send_debug_message("Queue failed - timeout or full");
    } else {
        ESP_LOGI(LOG_TAG, "✅ Text successfully queued for USB HID");
        pipecat_send_debug_message("Text queued OK");
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