# Voice Keyboard ESP32-S3-BOX-3

A voice-to-keyboard system that converts speech to text and types it via Bluetooth HID on your macOS device.

## Features

- 🎤 **Real-time speech recognition** using Deepgram STT
- ⌨️ **Bluetooth HID keyboard emulation** - appears as a standard keyboard
- 🔄 **WebRTC audio streaming** to Python server
- 📱 **ESP32-S3-BOX-3 hardware** with display and physical button control
- 🚀 **High-speed typing** optimized for voice input speed
- 🎯 **Async text processing** with queuing system

## System Architecture

### ESP32 Core Usage
- **Core 0**: Main task + Bluetooth HID task (high priority) + Screen task (low priority)
- **Core 1**: Audio task (highest priority - preserves timing calibration)

### Workflow
1. Press CONFIG button → Start voice session
2. Speak → ESP32 streams audio via WebRTC to server
3. Server processes speech with Deepgram STT
4. Server sends transcribed text back to ESP32 via data channel
5. ESP32 types text immediately via Bluetooth HID

## Hardware Requirements

- ESP32-S3-BOX-3 development board
- macOS device with Bluetooth
- WiFi network connection

## Software Requirements

### ESP32 Side
- ESP-IDF (latest version)
- Required environment variables:
  - `WIFI_SSID` - Your WiFi network name
  - `WIFI_PASSWORD` - Your WiFi password  
  - `PIPECAT_SMALLWEBRTC_URL` - Server endpoint (e.g., `http://192.168.1.100:8765/api/offer`)

### Server Side
- Python 3.8+
- Pipecat framework
- Deepgram API key
- Required packages: `uvicorn`, `fastapi`, `websockets`, `loguru`

## Quick Start

### 1. Set up Python Server

```bash
cd pipecat/demo/voice-keyboard
pip install -r ../../requirements.txt
export DEEPGRAM_API_KEY="your_deepgram_api_key"
python voice_keyboard_server.py --host 0.0.0.0 --port 8765
```

### 2. Build and Flash ESP32

```bash
cd pipecat-esp32/voice-keyboard-s3-box

# Set environment variables
export WIFI_SSID="YourWiFiSSID"
export WIFI_PASSWORD="YourWiFiPassword"
export PIPECAT_SMALLWEBRTC_URL="http://YOUR_SERVER_IP:8765/api/offer"

# Test build (optional)
./test_build.sh

# Or build and flash directly
idf.py --preview set-target esp32s3
idf.py build
idf.py flash
```

### 3. Pair with macOS

1. **ESP32**: Device will advertise as "Voice Keyboard"
2. **macOS**: Go to System Preferences → Bluetooth → Add Device
3. **Pair**: Select "Voice Keyboard" and complete pairing
4. **Test**: Device should appear in Bluetooth keyboards list

### 4. Use Voice Keyboard

1. **Start**: Press CONFIG button on ESP32-S3-BOX-3
2. **Speak**: Talk clearly into the device microphone
3. **Type**: Watch as your speech appears as typed text on macOS
4. **Stop**: Press CONFIG button again to end session

## Display States

The ESP32 screen shows current status:

- 🟢 **"Ready for voice typing"** - Idle, press button to start
- 🔗 **"Connecting..."** - Establishing WebRTC connection
- 🎙️ **"Transcribing..."** - Listening and processing speech
- ⌨️ **"Typing..."** - Sending keystrokes via Bluetooth
- 🛑 **"Stopping..."** - Ending session

## Web Interface

Access the server web interface at `http://SERVER_IP:8765` to:
- Monitor live transcription sessions  
- View real-time typing status
- Debug connection issues

## Technical Details

### Bluetooth HID Implementation
- **Protocol**: BLE HID (Bluetooth Low Energy Human Interface Device)
- **Typing Speed**: ~1-2ms per keystroke (no artificial delays)
- **Queue System**: Async text processing prevents blocking
- **Character Support**: Letters, numbers, punctuation, spaces

### Audio Processing
- **Codec**: Opus compression for WebRTC streaming
- **VAD**: Silero Voice Activity Detection
- **STT**: Deepgram Nova-3 model with smart formatting
- **Latency**: Optimized for minimal voice-to-keystroke delay

### Task Priorities
1. **Audio Task** (Core 1, Priority 7) - Audio capture/streaming
2. **Bluetooth Task** (Core 0, Priority 6) - Fast keystroke transmission  
3. **Main Task** (Core 0, Priority 5) - WebRTC and text reception
4. **Screen Task** (Core 0, Priority 1) - Static UI updates

## Troubleshooting

### Common Issues

**ESP32 won't connect to WiFi:**
- Check SSID/password environment variables
- Verify WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)

**Bluetooth pairing fails:**
- Reset ESP32 and try pairing again
- Remove old "Voice Keyboard" entries from macOS Bluetooth settings
- Ensure ESP32 is in pairing mode (restart device)

**No audio/transcription:**
- Check microphone is not muted on ESP32-S3-BOX-3
- Verify server is running and accessible
- Check WebRTC connection status in server logs

**Typing is slow/missing characters:**
- Bluetooth connection may be poor - move devices closer
- Check server logs for text transmission errors
- Restart Bluetooth service on macOS if needed

### Debug Logs

**ESP32 logs:**
```bash
idf.py monitor
```

**Server logs:**
- Server outputs detailed logs to console
- Look for "Sent text to ESP32" messages
- WebRTC connection status is logged

## Development Notes

This implementation preserves the original meeting-assistant's audio processing calibration by keeping all audio operations on Core 1. The new Bluetooth HID functionality runs on Core 0 with higher priority than UI updates, ensuring fast voice-to-keystroke response times.

The system uses async queuing to prevent any task from blocking others, and includes comprehensive error handling for robust operation.

## License

This project is part of the Pipecat framework. See the main project for license details.