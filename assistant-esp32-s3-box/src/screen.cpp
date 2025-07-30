#include <bsp/esp-bsp.h>
#include <lvgl.h>
#include <string.h>
#include "esp_log.h"

#include "main.h"

#define SCREEN_TICK_INTERVAL 50
#define MAX_LOG_LINES 8

static lv_obj_t *screen = NULL;
static lv_obj_t *log_container = NULL;
static lv_obj_t *log_label = NULL;
static lv_obj_t *log_labels[MAX_LOG_LINES];
static lv_obj_t *status_label = NULL;
static lv_obj_t *connect_button = NULL;
static int log_line_count = 0;

static lv_style_t STYLE_BLUE;
static lv_style_t STYLE_GREEN;
static lv_style_t STYLE_RED;
static lv_style_t STYLE_DEFAULT;

static void init_styles() {
  lv_style_init(&STYLE_BLUE);
  lv_style_set_text_color(&STYLE_BLUE, lv_color_hex(0x0000FF));

  lv_style_init(&STYLE_GREEN);
  lv_style_set_text_color(&STYLE_GREEN, lv_color_hex(0x00AA00));

  lv_style_init(&STYLE_RED);
  lv_style_set_text_color(&STYLE_RED, lv_color_hex(0xFF0000));

  lv_style_init(&STYLE_DEFAULT);
  lv_style_set_text_color(&STYLE_DEFAULT, lv_color_hex(0x000000));
}

static void connect_button_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  
  if (code == LV_EVENT_CLICKED) {
    connection_state_t state = pipecat_get_connection_state();
    
    if (state == CONNECTION_STATE_DISCONNECTED) {
      ESP_LOGI(LOG_TAG, "Connect button pressed - Connecting to server");
      pipecat_webrtc_connect();
    } else if (state == CONNECTION_STATE_CONNECTED || state == CONNECTION_STATE_CONNECTING) {
      ESP_LOGI(LOG_TAG, "Disconnect button pressed - Disconnecting from server");
      pipecat_webrtc_disconnect();
    }
  }
}

static lv_obj_t *create_connect_button(lv_obj_t *parent) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 150, 40);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
  
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, "CONNECT");
  lv_obj_center(label);
  
  lv_obj_add_event_cb(btn, connect_button_event_cb, LV_EVENT_CLICKED, NULL);
  
  return btn;
}

static lv_obj_t *create_scrollable_log(lv_obj_t *parent) {
  lv_obj_t *container = lv_obj_create(parent);
  lv_obj_set_size(container, LV_PCT(95), 140);
  lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);

  // Enable vertical scrolling
  lv_obj_set_scroll_dir(container, LV_DIR_VER);
  // Enable flex layout
  lv_obj_set_layout(container, LV_LAYOUT_FLEX);
  // Vertical stacking
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  // Space between rows
  lv_obj_set_style_pad_row(container, 2, 0);
  return container;
}

static lv_obj_t *create_status_label(lv_obj_t *parent) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_size(label, LV_PCT(95), LV_SIZE_CONTENT);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_add_style(label, &STYLE_BLUE, 0);
  lv_label_set_text(label, "Initializing...");
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

static lv_obj_t *create_label(lv_obj_t *container, lv_style_t *style) {
  lv_obj_t *label = lv_label_create(container);
  lv_obj_add_style(label, style, 0);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, "");
  return label;
}

static void create_current_label(lv_obj_t *container, lv_style_t *style) {
  // Delete oldest labels.
  if (log_line_count >= MAX_LOG_LINES) {
    lv_obj_del(log_labels[0]);

    // Shift remaining labels
    for (int i = 1; i < MAX_LOG_LINES; ++i) {
      log_labels[i - 1] = log_labels[i];
    }
    log_line_count--;
  }

  log_label = create_label(container, style);
  log_labels[log_line_count++] = log_label;
}

static void screen_task(void *pvParameter) {
  while (1) {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(SCREEN_TICK_INTERVAL));
  }
}

void pipecat_init_screen() {
  bsp_display_start();

  bsp_display_backlight_on();

  bsp_display_lock(0);

  screen = lv_scr_act();

  init_styles();

  status_label = create_status_label(screen);
  log_container = create_scrollable_log(screen);
  connect_button = create_connect_button(screen);

  bsp_display_unlock();

  xTaskCreatePinnedToCore(screen_task, "Screen Task", 8192, NULL, 1, NULL, 1);

  ESP_LOGI(LOG_TAG, "Display initialized");
}

void pipecat_screen_system_log(const char *text) {
  if (!log_container) {
    return;
  }

  bsp_display_lock(0);
  create_current_label(log_container, &STYLE_DEFAULT);
  pipecat_screen_log(text);
  bsp_display_unlock();
}

void pipecat_screen_new_log() {
  if (!log_container) {
    return;
  }

  bsp_display_lock(0);
  create_current_label(log_container, &STYLE_BLUE);
  bsp_display_unlock();
}

void pipecat_screen_log(const char *text) {
  if (!log_container) {
    return;
  }

  const char *current_text = lv_label_get_text(log_label);
  size_t current_len = strlen(current_text);
  size_t new_len = current_len + strlen(text) + 1;

  char *combined = (char *)malloc(new_len);
  if (!combined) {
    ESP_LOGW(LOG_TAG, "Failed to allocate memory for log text.");
    return;
  }

  strcpy(combined, current_text);
  strcat(combined, text);

  lv_label_set_text(log_label, combined);
  free(combined);

  lv_obj_scroll_to_view_recursive(log_label, LV_ANIM_OFF);
}

void pipecat_screen_update_status(const char *status) {
  if (!status_label || !connect_button) {
    return;
  }
  
  bsp_display_lock(0);
  
  // Update status label
  lv_label_set_text(status_label, status);
  
  // Update button text based on connection state
  connection_state_t state = pipecat_get_connection_state();
  lv_obj_t *btn_label = lv_obj_get_child(connect_button, 0);
  
  switch (state) {
    case CONNECTION_STATE_DISCONNECTED:
      lv_label_set_text(btn_label, "CONNECT");
      lv_obj_add_style(status_label, &STYLE_RED, 0);
      break;
      
    case CONNECTION_STATE_CONNECTING:
      lv_label_set_text(btn_label, "CANCEL");
      lv_obj_add_style(status_label, &STYLE_BLUE, 0);
      break;
      
    case CONNECTION_STATE_CONNECTED:
      lv_label_set_text(btn_label, "DISCONNECT");
      lv_obj_add_style(status_label, &STYLE_GREEN, 0);
      break;
  }
  
  bsp_display_unlock();
}
