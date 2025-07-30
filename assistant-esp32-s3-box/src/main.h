#include <peer.h>
#include <esp_err.h>

#define LOG_TAG "pipecat"
#define MAX_HTTP_OUTPUT_BUFFER 4096
#define HTTP_TIMEOUT_MS 10000
#define TICK_INTERVAL 15

// Connection state
typedef enum {
    CONNECTION_STATE_DISCONNECTED,
    CONNECTION_STATE_CONNECTING,
    CONNECTION_STATE_CONNECTED
} connection_state_t;

// Wifi
extern void pipecat_init_wifi();

// WebRTC / Media
extern void pipecat_init_audio_capture();
extern void pipecat_init_audio_decoder();
extern void pipecat_init_audio_encoder();
extern void pipecat_send_audio(PeerConnection *peer_connection);
extern void pipecat_audio_decode(uint8_t *data, size_t size);

// WebRTC / Signalling
extern void pipecat_init_webrtc();
extern void pipecat_webrtc_loop();
extern void pipecat_webrtc_connect();
extern void pipecat_webrtc_disconnect();
extern esp_err_t pipecat_http_request(char *offer, char *answer);

// Connection management
extern connection_state_t pipecat_get_connection_state();
extern void pipecat_set_connection_state(connection_state_t state);

// RTVI
typedef struct {
  void (*on_bot_started_speaking)();
  void (*on_bot_stopped_speaking)();
  void (*on_bot_tts_text)(const char *text);
} rtvi_callbacks_t;

extern rtvi_callbacks_t pipecat_rtvi_callbacks;

extern void pipecat_init_rtvi(PeerConnection *peer_connection, rtvi_callbacks_t *callbacks);
extern void pipecat_rtvi_send_client_ready();
extern void pipecat_rtvi_handle_message(const char* msg);

// Screen
extern void pipecat_init_screen();
extern void pipecat_screen_system_log(const char *text);
extern void pipecat_screen_new_log();
extern void pipecat_screen_log(const char *text);
extern void pipecat_screen_update_status(const char *status);
