#include <bsp/esp-bsp.h>
#include <lvgl.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "iot_button.h"

#include "main.h"

#define SCREEN_TICK_INTERVAL 50
#define PULSE_ANIMATION_SPEED 500 // milliseconds

static lv_obj_t *screen = NULL;
static lv_obj_t *main_container = NULL;
static lv_obj_t *start_button = NULL;
static lv_obj_t *stop_button = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *timer_label = NULL;
static lv_obj_t *transcribing_label = NULL;

static lv_style_t button_style;
static lv_style_t active_button_style;
static lv_style_t text_style;
static lv_style_t timer_style;

static bool button_pressed = false;
static unsigned long last_button_press_time = 0;
#define BUTTON_DEBOUNCE_MS 500  // 500ms debounce to prevent multiple quick presses
static unsigned long meeting_start_time = 0;
static bool meeting_ui_created = false;
static button_handle_t physical_button = NULL;

// Physical button callback - safer than touch
static void physical_button_press_cb(void *button_handle, void *usr_data) {
    unsigned long current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Debounce: ignore clicks within 500ms of last press
    if (current_time - last_button_press_time < BUTTON_DEBOUNCE_MS) {
        ESP_LOGI(LOG_TAG, "Physical button debounced (too soon)");
        return;
    }
    
    ESP_LOGI(LOG_TAG, "Physical button pressed in state: %d", current_meeting_state);
    
    if (current_meeting_state == MEETING_STATE_IDLE || current_meeting_state == MEETING_STATE_ACTIVE) {
        button_pressed = true;
        last_button_press_time = current_time;
        ESP_LOGI(LOG_TAG, "Physical button press registered");
    } else {
        ESP_LOGI(LOG_TAG, "Physical button ignored - invalid state (%d)", current_meeting_state);
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
        if (current_meeting_state == MEETING_STATE_IDLE) {
            button_pressed = true;
            last_button_press_time = current_time;
            ESP_LOGI(LOG_TAG, "Start meeting button CLICKED (IDLE state)");
        } else {
            ESP_LOGI(LOG_TAG, "Start button ignored - not in IDLE state (%d)", current_meeting_state);
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
        if (current_meeting_state == MEETING_STATE_ACTIVE) {
            button_pressed = true;
            last_button_press_time = current_time;
            ESP_LOGI(LOG_TAG, "Stop meeting button CLICKED (ACTIVE state)");
        } else {
            ESP_LOGI(LOG_TAG, "Stop button ignored - not in ACTIVE state (%d)", current_meeting_state);
        }
    }
}

static void create_start_meeting_ui() {
    // Clear container
    lv_obj_clean(main_container);

    // Title
    lv_obj_t *title = lv_label_create(main_container);
    lv_label_set_text(title, "Meeting Assistant");
    lv_obj_add_style(title, &text_style, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Status
    status_label = lv_label_create(main_container);
    lv_label_set_text(status_label, "Ready to start meeting");
    lv_obj_add_style(status_label, &text_style, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -40);

    // Start button (display only - use physical CONFIG button to activate)
    start_button = lv_btn_create(main_container);
    lv_obj_set_size(start_button, 180, 60);  // Smaller button for 14pt font
    lv_obj_add_style(start_button, &button_style, 0);
    lv_obj_align(start_button, LV_ALIGN_CENTER, 0, 20);
    // No touch event - use physical button instead to avoid crashes

    lv_obj_t *btn_label = lv_label_create(start_button);
    lv_label_set_text(btn_label, "Press CONFIG Button\nto Start Meeting");
    lv_obj_center(btn_label);
}

static void create_meeting_active_ui() {
    ESP_LOGI(LOG_TAG, "create_meeting_active_ui: Starting UI creation");
    // Clear container
    lv_obj_clean(main_container);
    ESP_LOGI(LOG_TAG, "create_meeting_active_ui: Container cleaned");

    // Title
    lv_obj_t *title = lv_label_create(main_container);
    lv_label_set_text(title, "Meeting in Progress");
    lv_obj_add_style(title, &text_style, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Timer
    timer_label = lv_label_create(main_container);
    lv_label_set_text(timer_label, "00:00");
    lv_obj_add_style(timer_label, &timer_style, 0);
    lv_obj_align(timer_label, LV_ALIGN_TOP_MID, 0, 60);

    // Transcribing indicator
    transcribing_label = lv_label_create(main_container);
    lv_label_set_text(transcribing_label, "🎙️ Transcribing...");
    lv_obj_add_style(transcribing_label, &text_style, 0);
    lv_obj_set_style_text_color(transcribing_label, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(transcribing_label, LV_ALIGN_CENTER, 0, -20);

    // Stop button (display only - use physical CONFIG button to activate)
    stop_button = lv_btn_create(main_container);
    lv_obj_set_size(stop_button, 180, 60);  // Smaller button for 14pt font
    lv_obj_add_style(stop_button, &active_button_style, 0);
    lv_obj_align(stop_button, LV_ALIGN_CENTER, 0, 40);
    // No touch event - use physical button instead to avoid crashes

    lv_obj_t *btn_label = lv_label_create(stop_button);
    lv_label_set_text(btn_label, "Press CONFIG Button\nto Stop Meeting");
    lv_obj_center(btn_label);

    // Start pulsing animation
    static lv_anim_t pulse_anim;
    lv_anim_init(&pulse_anim);
    lv_anim_set_var(&pulse_anim, stop_button);
    lv_anim_set_values(&pulse_anim, 180, 160);  // Adjusted for smaller button
    lv_anim_set_time(&pulse_anim, PULSE_ANIMATION_SPEED);
    lv_anim_set_exec_cb(&pulse_anim, (lv_anim_exec_xcb_t)lv_obj_set_width);
    lv_anim_set_path_cb(&pulse_anim, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_time(&pulse_anim, PULSE_ANIMATION_SPEED);
    lv_anim_start(&pulse_anim);
    
    ESP_LOGI(LOG_TAG, "create_meeting_active_ui: UI creation completed");
}

static void update_meeting_timer(unsigned long duration_seconds) {
    if (timer_label) {
        unsigned long minutes = duration_seconds / 60;
        unsigned long seconds = duration_seconds % 60;
        
        char timer_text[16];
        snprintf(timer_text, sizeof(timer_text), "%02lu:%02lu", minutes, seconds);
        lv_label_set_text(timer_label, timer_text);
    }
}

// External shared variables from main.cpp
extern volatile unsigned long shared_meeting_duration;
extern volatile bool shared_meeting_ui_update_needed;
extern meeting_state_t current_meeting_state;

static void screen_task(void *pvParameter) {
    static int debug_counter = 0;
    while (1) {
        lv_timer_handler();
        
        debug_counter++;
        if (debug_counter % 200 == 0) { // Every 10 seconds (200 * 50ms)
            ESP_LOGI(LOG_TAG, "Screen task: State=%d, Update needed=%s", 
                     current_meeting_state, shared_meeting_ui_update_needed ? "true" : "false");
        }
        
        // Check for meeting UI updates from main task (thread-safe read)
        if (shared_meeting_ui_update_needed && current_meeting_state == MEETING_STATE_ACTIVE) {
            // Read shared data
            unsigned long duration = shared_meeting_duration;
            
            // Clear the update flag
            shared_meeting_ui_update_needed = false;
            
            ESP_LOGI(LOG_TAG, "Screen task: Updating UI to meeting active, duration=%lu", duration);
            // Safely update UI from screen task context
            pipecat_screen_show_meeting_active(duration);
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

    // Start with the start meeting UI
    create_start_meeting_ui();
    
    // Unlock only for initial render, then lock again to prevent crashes
    bsp_display_unlock();
    lv_timer_handler(); // Force initial render
    bsp_display_lock(0);

    xTaskCreatePinnedToCore(screen_task, "Screen Task", 8192, NULL, 1, NULL, 0);

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
        iot_button_register_cb(physical_button, BUTTON_PRESS_DOWN, physical_button_press_cb, NULL);
        ESP_LOGI(LOG_TAG, "Physical button (CONFIG) initialized as backup");
    } else {
        ESP_LOGE(LOG_TAG, "Failed to initialize physical button");
    }

    ESP_LOGI(LOG_TAG, "Meeting assistant display initialized");
}

void pipecat_screen_show_start_button() {
    ESP_LOGI(LOG_TAG, "Resetting screen to start meeting UI");
    if (main_container) {
        // Reset meeting UI flag so next meeting can create fresh UI
        meeting_ui_created = false;
        
        // No display lock needed - same core as screen task
        create_start_meeting_ui();
        ESP_LOGI(LOG_TAG, "Start meeting UI displayed");
    }
}

void pipecat_screen_show_meeting_active(unsigned long duration_seconds) {    
    if (!meeting_ui_created) {
        ESP_LOGI(LOG_TAG, "Creating meeting active UI (no display lock - same core as lv_timer_handler)");
        create_meeting_active_ui();
        meeting_ui_created = true;
        meeting_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    }
    
    // Update timer without display lock since both screen task and this run on same core
    update_meeting_timer(duration_seconds);
}

void pipecat_screen_system_log(const char *text) {
    ESP_LOGI(LOG_TAG, "Screen log: %s", text);
    
    // Update status label if available
    if (status_label && current_meeting_state == MEETING_STATE_IDLE) {
        lv_label_set_text(status_label, text);
    }
}

bool pipecat_check_button_pressed() {
    bool pressed = false;
    
    // No display lock needed - just checking a boolean flag
    // Both main task and screen task are on Core 0, so no race condition
    if (button_pressed) {
        button_pressed = false;
        pressed = true;
    }
    
    return pressed;
}

void pipecat_screen_reset_to_idle() {
    bsp_display_lock(0);
    meeting_ui_created = false;  // Reset the meeting UI flag
    create_start_meeting_ui();
    bsp_display_unlock();
}
