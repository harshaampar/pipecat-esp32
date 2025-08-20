#include <esp_log.h>

#include "main.h"

// Meeting Assistant minimal RTVI callbacks for compatibility
static void on_bot_started_speaking() {
  // Meeting assistant doesn't need TTS callbacks, but required for RTVI init
}

static void on_bot_stopped_speaking() {
  // Meeting assistant doesn't need TTS callbacks, but required for RTVI init
}

static void on_bot_tts_text(const char *text) {
  // Meeting assistant doesn't need TTS callbacks, but required for RTVI init
}

rtvi_callbacks_t pipecat_rtvi_callbacks = {
    .on_bot_started_speaking = on_bot_started_speaking,
    .on_bot_stopped_speaking = on_bot_stopped_speaking,
    .on_bot_tts_text = on_bot_tts_text,
};
