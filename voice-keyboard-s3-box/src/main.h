#include <peer.h>
#include "config.h"

#define LOG_TAG "voice-keyboard"
#define MAX_HTTP_OUTPUT_BUFFER 4096
#define HTTP_TIMEOUT_MS 10000
#define TICK_INTERVAL 15

// Voice-keyboard states
typedef enum {
    KEYBOARD_STATE_IDLE,
    KEYBOARD_STATE_STARTING,
    KEYBOARD_STATE_TRANSCRIBING,
    KEYBOARD_STATE_TYPING,
    KEYBOARD_STATE_STOPPING,
    KEYBOARD_STATE_SERVER_UNAVAILABLE,  // Server not running during voice session
    KEYBOARD_STATE_CONFIG_MODE,
    // Configuration phases
    KEYBOARD_STATE_CONFIG_WIFI_CONNECTING,
    KEYBOARD_STATE_CONFIG_WIFI_CONNECTED,
    KEYBOARD_STATE_CONFIG_WIFI_FAILED,
    KEYBOARD_STATE_CONFIG_SERVER_CHECKING,
    KEYBOARD_STATE_CONFIG_SERVER_CONNECTED,
    KEYBOARD_STATE_CONFIG_SERVER_FAILED,
    KEYBOARD_STATE_CONFIG_HID_READY,
    KEYBOARD_STATE_CONFIG_PROVISIONING
} keyboard_state_t;

// Wifi
extern void pipecat_init_wifi();
extern bool pipecat_connect_wifi_with_config(const voice_keyboard_config_t *config);
extern bool pipecat_is_wifi_connected();

// WebRTC / Media (audio input and output for WebRTC stability)
extern void pipecat_init_audio_capture();
extern void pipecat_init_audio_decoder();
extern void pipecat_init_audio_encoder();
extern void pipecat_send_audio(PeerConnection *peer_connection);
extern void pipecat_audio_decode(uint8_t *data, size_t size);

// WebRTC / Signalling
extern void pipecat_init_webrtc();
extern void pipecat_webrtc_loop();
extern void pipecat_http_request(char *offer, char *answer);
extern void pipecat_http_debug_message(const char* message);
extern void pipecat_send_disconnect_message();
extern void pipecat_stop_webrtc();
extern bool pipecat_is_webrtc_connected();
extern void pipecat_send_debug_message(const char* message);

// Health Check and USB JTAG Provisioning
extern bool pipecat_http_health_check(const char* server_ip, const char* server_port);
extern bool pipecat_usb_request_provisioning(const char* request_type, voice_keyboard_config_t* config);
extern void pipecat_usb_send_provisioning_status(const char* phase, const char* status, const char* message);
extern bool pipecat_usb_check_provisioning_response(voice_keyboard_config_t* config);
extern bool pipecat_usb_parse_provisioning_response(voice_keyboard_config_t* config, const char* response_line);

// Voice-keyboard control
extern keyboard_state_t current_keyboard_state;
extern void pipecat_start_voice_session();
extern void pipecat_stop_voice_session();
extern bool pipecat_is_voice_active();

// Shared variables for cross-core communication
extern volatile keyboard_state_t shared_keyboard_state;
extern volatile bool shared_keyboard_ui_update_needed;

// Screen
extern void pipecat_init_screen();
extern void pipecat_screen_show_start_button();
extern void pipecat_screen_show_keyboard_status(keyboard_state_t state);
extern void pipecat_screen_system_log(const char *text);
extern void pipecat_screen_reset_to_idle();

// Touch/Button handling
extern bool pipecat_check_button_pressed();

// RTVI (minimal implementation for compatibility)
typedef struct {
  void (*on_bot_started_speaking)();
  void (*on_bot_stopped_speaking)();
  void (*on_bot_tts_text)(const char *text);
} rtvi_callbacks_t;

extern rtvi_callbacks_t pipecat_rtvi_callbacks;
extern void pipecat_init_rtvi(PeerConnection *peer_connection, rtvi_callbacks_t *callbacks);
extern void pipecat_rtvi_handle_message(const char* msg);

// Configuration system
extern void pipecat_init_config();
extern bool pipecat_load_config();
extern bool pipecat_is_config_valid();
extern const voice_keyboard_config_t* pipecat_get_config();

// USB HID Keyboard (function names kept for compatibility)
extern void pipecat_init_bluetooth_hid();
extern void pipecat_queue_text_for_typing(const char* text);
extern bool pipecat_is_bluetooth_connected();
