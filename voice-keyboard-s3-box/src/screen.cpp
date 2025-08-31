#include <bsp/esp-bsp.h>
#include <lvgl.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "iot_button.h"

#include "main.h"

// Mutex for button press synchronization
static portMUX_TYPE button_mutex = portMUX_INITIALIZER_UNLOCKED;

#define SCREEN_TICK_INTERVAL 50
#define PULSE_ANIMATION_SPEED 500 // milliseconds

static lv_obj_t *screen = NULL;
static lv_obj_t *main_container = NULL;
static lv_obj_t *start_button = NULL;
static lv_obj_t *stop_button = NULL;
static lv_obj_t *wifi_label = NULL;      // WiFi network name display
static lv_obj_t *status_label = NULL;
static lv_obj_t *timer_label = NULL;
static lv_obj_t *transcribing_label = NULL;
static lv_obj_t *debug_label = NULL;

static lv_style_t button_style;
static lv_style_t active_button_style;
static lv_style_t text_style;
static lv_style_t timer_style;

static volatile bool button_pressed = false;
static unsigned long last_button_press_time = 0;
#define BUTTON_DEBOUNCE_MS 500  // 500ms debounce to prevent multiple quick presses
// meeting_start_time removed - not needed for voice keyboard
static button_handle_t physical_button = NULL;

// Function to create/update WiFi status display at top of screen
static void create_wifi_status_display() {
    if (!main_container) return;
    
    if (!wifi_label) {
        wifi_label = lv_label_create(main_container);
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x888888), 0);  // Gray color
        lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14, 0);   // Small font
        lv_obj_align(wifi_label, LV_ALIGN_TOP_MID, 0, 5);                    // Top of screen
    }
    
    // Show WiFi status based on current state and config
    const voice_keyboard_config_t* config = pipecat_get_config();
    if (current_keyboard_state >= KEYBOARD_STATE_CONFIG_MODE && current_keyboard_state <= KEYBOARD_STATE_CONFIG_PROVISIONING) {
        // In configuration phase - show setup status
        lv_label_set_text(wifi_label, "Setup: Configuring...");
    } else if (config && config->wifi_ssid[0] != '\0') {
        // Have valid config - show SSID
        char wifi_text[64];
        snprintf(wifi_text, sizeof(wifi_text), "WiFi: %s", config->wifi_ssid);
        lv_label_set_text(wifi_label, wifi_text);
    } else {
        // No config available
        lv_label_set_text(wifi_label, "WiFi: Not configured");
    }
}

// Physical button callback - safer than touch
static void physical_button_press_cb(void *button_handle, void *usr_data) {
    unsigned long current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    ESP_LOGI(LOG_TAG, "🔘 Physical CONFIG button pressed!");
    pipecat_send_debug_message("CONFIG button pressed");
    
    // Debounce: ignore clicks within 500ms of last press
    if (current_time - last_button_press_time < BUTTON_DEBOUNCE_MS) {
        ESP_LOGI(LOG_TAG, "Physical button debounced (too soon)");
        pipecat_send_debug_message("Button press debounced (too soon)");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "Physical button pressed in state: %d", current_keyboard_state);
    
    if (current_keyboard_state == KEYBOARD_STATE_IDLE || 
        current_keyboard_state == KEYBOARD_STATE_TRANSCRIBING ||
        current_keyboard_state == KEYBOARD_STATE_SERVER_UNAVAILABLE) {
        portENTER_CRITICAL(&button_mutex);
        button_pressed = true;
        portEXIT_CRITICAL(&button_mutex);
        last_button_press_time = current_time;
        ESP_LOGI(LOG_TAG, "Physical button press registered");
        pipecat_send_debug_message("Button press REGISTERED - will trigger action");
    } else {
        ESP_LOGI(LOG_TAG, "Physical button ignored - invalid state (%d)", current_keyboard_state);
        char debug_msg[100];
        snprintf(debug_msg, sizeof(debug_msg), "Button ignored - invalid state %d", current_keyboard_state);
        pipecat_send_debug_message(debug_msg);
    }
}

static void init_styles() {
    // Button style
    lv_style_init(&button_style);
    lv_style_set_radius(&button_style, 20);
    lv_style_set_bg_color(&button_style, lv_color_hex(0x2196F3));
    lv_style_set_bg_opa(&button_style, LV_OPA_COVER);
    lv_style_set_border_width(&button_style, 2);
    lv_style_set_border_color(&button_style, lv_color_hex(0x1976D2));
    lv_style_set_text_color(&button_style, lv_color_white());
    lv_style_set_text_font(&button_style, &lv_font_montserrat_14);

    // Active button style (pulsing)
    lv_style_init(&active_button_style);
    lv_style_set_radius(&active_button_style, 20);
    lv_style_set_bg_color(&active_button_style, lv_color_hex(0xF44336));
    lv_style_set_bg_opa(&active_button_style, LV_OPA_COVER);
    lv_style_set_border_width(&active_button_style, 3);
    lv_style_set_border_color(&active_button_style, lv_color_hex(0xD32F2F));
    lv_style_set_text_color(&active_button_style, lv_color_white());
    lv_style_set_text_font(&active_button_style, &lv_font_montserrat_14);

    // Text style
    lv_style_init(&text_style);
    lv_style_set_text_color(&text_style, lv_color_hex(0x333333));
    lv_style_set_text_font(&text_style, &lv_font_montserrat_14);
    lv_style_set_text_align(&text_style, LV_TEXT_ALIGN_CENTER);

    // Timer style
    lv_style_init(&timer_style);
    lv_style_set_text_color(&timer_style, lv_color_hex(0x4CAF50));
    lv_style_set_text_font(&timer_style, &lv_font_montserrat_14);
    lv_style_set_text_align(&timer_style, LV_TEXT_ALIGN_CENTER);
}

static void start_button_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        unsigned long current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Debounce: ignore clicks within 500ms of last press
        if (current_time - last_button_press_time < BUTTON_DEBOUNCE_MS) {
            ESP_LOGI(LOG_TAG, "Start button debounced (too soon)");
            return;
        }
        
        // Only register click if we're in IDLE state
        if (current_keyboard_state == KEYBOARD_STATE_IDLE) {
            button_pressed = true;
            last_button_press_time = current_time;
            ESP_LOGI(LOG_TAG, "Start voice session button CLICKED (IDLE state)");
        } else {
            ESP_LOGI(LOG_TAG, "Start button ignored - not in IDLE state (%d)", current_keyboard_state);
        }
    }
}

static void stop_button_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        unsigned long current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Debounce: ignore clicks within 500ms of last press
        if (current_time - last_button_press_time < BUTTON_DEBOUNCE_MS) {
            ESP_LOGI(LOG_TAG, "Stop button debounced (too soon)");
            return;
        }
        
        // Only register click if we're in ACTIVE state
        if (current_keyboard_state == KEYBOARD_STATE_TRANSCRIBING) {
            button_pressed = true;
            last_button_press_time = current_time;
            ESP_LOGI(LOG_TAG, "Stop voice session button CLICKED (TRANSCRIBING state)");
        } else {
            ESP_LOGI(LOG_TAG, "Stop button ignored - not in TRANSCRIBING state (%d)", current_keyboard_state);
        }
    }
}

static void create_voice_keyboard_idle_ui() {
    // Clear container and reset all widget pointers
    lv_obj_clean(main_container);
    wifi_label = NULL;  // Reset pointer after clean
    status_label = NULL;
    timer_label = NULL;
    transcribing_label = NULL;
    debug_label = NULL;
    start_button = NULL;
    stop_button = NULL;

    // WiFi status display at top
    create_wifi_status_display();

    // Title
    lv_obj_t *title = lv_label_create(main_container);
    lv_label_set_text(title, "Voice Keyboard");
    lv_obj_add_style(title, &text_style, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // Status
    status_label = lv_label_create(main_container);
    lv_label_set_text(status_label, "Ready for voice typing");
    lv_obj_add_style(status_label, &text_style, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -40);

    // Start button (display only - use physical CONFIG button to activate)
    start_button = lv_btn_create(main_container);
    lv_obj_set_size(start_button, 180, 60);  // Smaller button for 14pt font
    lv_obj_add_style(start_button, &button_style, 0);
    lv_obj_align(start_button, LV_ALIGN_CENTER, 0, 20);
    // No touch event - use physical button instead to avoid crashes

    lv_obj_t *btn_label = lv_label_create(start_button);
    lv_label_set_text(btn_label, "Press CONFIG Button\nto Start Voice Typing");
    
    // Debug label removed
    lv_obj_center(btn_label);
}

static void create_voice_status_ui(const char* status_text, const char* icon, uint32_t color) {
    // Clear container and reset all widget pointers
    lv_obj_clean(main_container);
    wifi_label = NULL;  // Reset pointer after clean
    status_label = NULL;
    timer_label = NULL;
    transcribing_label = NULL;
    debug_label = NULL;
    start_button = NULL;
    stop_button = NULL;

    // WiFi status display at top
    create_wifi_status_display();

    // Title
    lv_obj_t *title = lv_label_create(main_container);
    lv_label_set_text(title, "Voice Keyboard");
    lv_obj_add_style(title, &text_style, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // Status text - large and centered
    status_label = lv_label_create(main_container);
    if (icon && icon[0] != '\0') {
        char status_with_icon[64];
        snprintf(status_with_icon, sizeof(status_with_icon), "%s %s", icon, status_text);
        lv_label_set_text(status_label, status_with_icon);
    } else {
        lv_label_set_text(status_label, status_text);
    }
    lv_obj_add_style(status_label, &text_style, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -10);

    // Stop button (display only - use physical CONFIG button to stop)
    stop_button = lv_btn_create(main_container);
    lv_obj_set_size(stop_button, 180, 60);
    lv_obj_add_style(stop_button, &active_button_style, 0);
    lv_obj_align(stop_button, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *btn_label = lv_label_create(stop_button);
    lv_label_set_text(btn_label, "Press CONFIG Button\nto Stop");
    
    // Debug label removed
    lv_obj_center(btn_label);
}

// Server unavailable UI - shows when server is not running during voice session
static void create_server_unavailable_ui() {
    // Clear container and reset all widget pointers
    lv_obj_clean(main_container);
    wifi_label = NULL;  // Reset pointer after clean
    status_label = NULL;
    timer_label = NULL;
    transcribing_label = NULL;
    debug_label = NULL;
    start_button = NULL;
    stop_button = NULL;

    // WiFi status display at top
    create_wifi_status_display();

    // Title
    lv_obj_t *title = lv_label_create(main_container);
    lv_label_set_text(title, "Voice Keyboard");
    lv_obj_add_style(title, &text_style, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // Error message
    lv_obj_t *error_label = lv_label_create(main_container);
    lv_label_set_text(error_label, "SERVER NOT RUNNING");
    lv_obj_add_style(error_label, &text_style, 0);
    lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
    lv_obj_align(error_label, LV_ALIGN_CENTER, 0, -50);

    // Instruction message
    lv_obj_t *instruction = lv_label_create(main_container);
    lv_label_set_text(instruction, "Please start the server\nfor voice typing to work");
    lv_obj_add_style(instruction, &text_style, 0);
    lv_obj_set_style_text_color(instruction, lv_color_hex(0x666666), 0);
    lv_obj_align(instruction, LV_ALIGN_CENTER, 0, -10);

    // Retry button (display only - use physical CONFIG button to retry)
    lv_obj_t *retry_button = lv_btn_create(main_container);
    lv_obj_set_size(retry_button, 180, 60);
    lv_obj_add_style(retry_button, &button_style, 0);
    lv_obj_align(retry_button, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *btn_label = lv_label_create(retry_button);
    lv_label_set_text(btn_label, "Press CONFIG Button\nto Try Again");
    lv_obj_center(btn_label);
}

// Configuration UI for different phases
static void create_config_status_ui(const char* phase_text, const char* status_text, const char* icon, uint32_t color) {
    // Clear container and reset all widget pointers
    lv_obj_clean(main_container);
    wifi_label = NULL;  // Reset pointer after clean
    status_label = NULL;
    timer_label = NULL;
    transcribing_label = NULL;
    debug_label = NULL;
    start_button = NULL;
    stop_button = NULL;

    // WiFi status display at top
    create_wifi_status_display();

    // Title
    lv_obj_t *title = lv_label_create(main_container);
    lv_label_set_text(title, "Voice Keyboard Setup");
    lv_obj_add_style(title, &text_style, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // Phase indicator
    lv_obj_t *phase_label = lv_label_create(main_container);
    lv_label_set_text(phase_label, phase_text);
    lv_obj_add_style(phase_label, &text_style, 0);
    lv_obj_set_style_text_color(phase_label, lv_color_hex(0x666666), 0);
    lv_obj_align(phase_label, LV_ALIGN_CENTER, 0, -40);

    // Status text - large and centered  
    status_label = lv_label_create(main_container);
    if (icon && icon[0] != '\0') {
        char status_with_icon[128];
        snprintf(status_with_icon, sizeof(status_with_icon), "%s %s", icon, status_text);
        lv_label_set_text(status_label, status_with_icon);
    } else {
        lv_label_set_text(status_label, status_text);
    }
    lv_obj_add_style(status_label, &text_style, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -10);

    // Progress indicator or instruction
    lv_obj_t *instruction = lv_label_create(main_container);
    if (color == 0xFF0000) { // Failed state
        lv_label_set_text(instruction, "Check configuration\nand restart device");
    } else if (color == 0xFFA500) { // In progress
        lv_label_set_text(instruction, "Please wait...");
    } else { // Success state
        lv_label_set_text(instruction, "✓ Success");
    }
    lv_obj_add_style(instruction, &text_style, 0);
    lv_obj_set_style_text_color(instruction, lv_color_hex(0x888888), 0);
    lv_obj_align(instruction, LV_ALIGN_CENTER, 0, 20);
}

// Main keyboard status function - called from screen task
void pipecat_screen_show_keyboard_status(keyboard_state_t state) {
    static bool keyboard_ui_created = false;
    
    switch (state) {
        case KEYBOARD_STATE_IDLE:
            ESP_LOGI(LOG_TAG, "Showing keyboard idle UI");
            create_voice_keyboard_idle_ui();
            keyboard_ui_created = false;
            break;
            
        case KEYBOARD_STATE_STARTING:
            ESP_LOGI(LOG_TAG, "Showing keyboard starting UI");  
            create_voice_status_ui("Connecting...", "", 0xFFA500); // Orange
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_TRANSCRIBING:
            ESP_LOGI(LOG_TAG, "Showing keyboard transcribing UI");
            create_voice_status_ui("Transcribing...", "", 0x4CAF50); // Green
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_TYPING:
            ESP_LOGI(LOG_TAG, "Showing keyboard typing UI");
            create_voice_status_ui("Typing...", "", 0x2196F3); // Blue
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_STOPPING:
            ESP_LOGI(LOG_TAG, "Showing keyboard stopping UI");
            create_voice_status_ui("Stopping...", "", 0xFF5722); // Red
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_SERVER_UNAVAILABLE:
            ESP_LOGI(LOG_TAG, "Showing server unavailable UI");
            create_server_unavailable_ui();
            keyboard_ui_created = true;
            break;
            
        // Configuration phase states
        case KEYBOARD_STATE_CONFIG_MODE:
            ESP_LOGI(LOG_TAG, "Showing configuration mode UI");
            create_config_status_ui("Setup Required", "Configuration Mode", "", 0xFFA500);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_WIFI_CONNECTING:
            ESP_LOGI(LOG_TAG, "Showing WiFi connecting UI");
            create_config_status_ui("Phase 1/3", "Connecting to WiFi...", "", 0xFFA500);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_WIFI_CONNECTED:
            ESP_LOGI(LOG_TAG, "Showing WiFi connected UI");
            create_config_status_ui("Phase 1/3", "WiFi Connected", "", 0x4CAF50);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_WIFI_FAILED:
            ESP_LOGI(LOG_TAG, "Showing WiFi failed UI");
            create_config_status_ui("Phase 1/3", "WiFi Connection Failed", "", 0xFF0000);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_SERVER_CHECKING:
            ESP_LOGI(LOG_TAG, "Showing server checking UI");
            create_config_status_ui("Phase 2/3", "Checking Server...", "", 0xFFA500);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_SERVER_CONNECTED:
            ESP_LOGI(LOG_TAG, "Showing server connected UI");
            create_config_status_ui("Phase 2/3", "Server Connected", "", 0x4CAF50);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_SERVER_FAILED:
            ESP_LOGI(LOG_TAG, "Showing server failed UI");
            create_config_status_ui("Phase 2/3", "Server Connection Failed", "", 0xFF0000);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_HID_READY:
            ESP_LOGI(LOG_TAG, "Showing HID ready UI");
            create_config_status_ui("Phase 3/3", "HID Mode Ready", "", 0x4CAF50);
            keyboard_ui_created = true;
            break;
            
        case KEYBOARD_STATE_CONFIG_PROVISIONING:
            ESP_LOGI(LOG_TAG, "Showing provisioning UI");
            create_config_status_ui("Auto-Setup", "Requesting Configuration...", "", 0xFFA500);
            keyboard_ui_created = true;
            break;
            
        default:
            ESP_LOGW(LOG_TAG, "Unknown keyboard state: %d", state);
            break;
    }
}

// External shared variables from main.cpp
extern volatile keyboard_state_t shared_keyboard_state;
extern volatile bool shared_keyboard_ui_update_needed;
extern keyboard_state_t current_keyboard_state;

static void screen_task(void *pvParameter) {
    while (1) {
        lv_timer_handler();
        
        // Check for keyboard UI updates from main task (thread-safe read)
        static keyboard_state_t last_ui_state = KEYBOARD_STATE_IDLE;
        if (shared_keyboard_ui_update_needed) {
            // Read shared data
            keyboard_state_t current_state = shared_keyboard_state;
            
            // Clear the update flag
            shared_keyboard_ui_update_needed = false;
            
            // Only update UI if state actually changed
            if (current_state != last_ui_state) {
                pipecat_screen_show_keyboard_status(current_state);
                last_ui_state = current_state;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(SCREEN_TICK_INTERVAL));
    }
}

void pipecat_init_screen() {
    bsp_display_start();
    bsp_display_backlight_on();
    
    // Wait for display system to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Keep display locked - follow original assistant pattern  
    bsp_display_lock(0);

    screen = lv_scr_act();
    init_styles();

    // Create main container
    main_container = lv_obj_create(screen);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_align(main_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(main_container, lv_color_hex(0xF5F5F5), 0);

    // Start with the voice keyboard idle UI
    create_voice_keyboard_idle_ui();
    
    // Unlock only for initial render, then lock again to prevent crashes
    bsp_display_unlock();
    lv_timer_handler(); // Force initial render
    bsp_display_lock(0);

    xTaskCreatePinnedToCore(screen_task, "Screen Task", 6144, NULL, 4, NULL, 0);

    // Initialize physical button as backup (CONFIG button - GPIO 0)
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = BSP_BUTTON_CONFIG_IO,
            .active_level = 0,
        },
    };
    physical_button = iot_button_create(&btn_cfg);
    if (physical_button != NULL) {
        iot_button_register_cb(physical_button, BUTTON_SINGLE_CLICK, physical_button_press_cb, NULL);
        ESP_LOGI(LOG_TAG, "Physical button (CONFIG) initialized as backup");
        // Skip HTTP debug during screen init to avoid crashes - just log locally
    } else {
        ESP_LOGE(LOG_TAG, "Failed to initialize physical button");
        // Skip HTTP debug during screen init to avoid crashes - just log locally
    }

    ESP_LOGI(LOG_TAG, "Voice keyboard display initialized");
}

void pipecat_screen_show_start_button() {
    ESP_LOGI(LOG_TAG, "Resetting screen to voice keyboard idle UI");
    if (main_container) {
        // No display lock needed - same core as screen task
        create_voice_keyboard_idle_ui();
        ESP_LOGI(LOG_TAG, "Voice keyboard idle UI displayed");
    }
}


void pipecat_screen_system_log(const char *text) {
    ESP_LOGI(LOG_TAG, "Screen log: %s", text);
    // Debug label removed - function kept for compatibility but does nothing
}

bool pipecat_check_button_pressed() {
    bool pressed = false;
    
    // Use critical section to prevent race condition between button callback and main loop
    portENTER_CRITICAL(&button_mutex);
    if (button_pressed) {
        button_pressed = false;
        pressed = true;
    }
    portEXIT_CRITICAL(&button_mutex);
    
    return pressed;
}

void pipecat_screen_reset_to_idle() {
    bsp_display_lock(0);
    create_voice_keyboard_idle_ui();
    bsp_display_unlock();
}
