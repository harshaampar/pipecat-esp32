# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the Pipecat ESP32 Client SDK - a WebRTC client implementation for ESP32 microcontrollers that enables real-time audio communication with Pipecat AI bots. The project supports both ESP32-S3 hardware and Linux (for development/testing).

## Key Development Commands

### Environment Setup
```bash
# Load ESP-IDF environment (required before any idf.py commands)
source PATH_TO_ESP_IDF/export.sh

# Set required environment variables
export WIFI_SSID=your_wifi_ssid
export WIFI_PASSWORD=your_wifi_password
export PIPECAT_SMALLWEBRTC_URL=http://your-bot-url/api/offer

# Optional: Enable data channel message logging
export LOG_DATACHANNEL_MESSAGES=1
```

### Building
```bash
cd esp32-s3-box-3

# Set target platform
idf.py --preview set-target esp32s3  # For ESP32 hardware
# or
idf.py --preview set-target linux    # For Linux testing

# Build the project
idf.py build

# Clean build
idf.py fullclean
```

### Flashing and Running
```bash
# Flash to ESP32 (Linux)
idf.py -p /dev/ttyACM0 flash

# Flash to ESP32 (macOS)
idf.py flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor

# Run Linux build directly
./build/src.elf
```

### Code Formatting
```bash
# Format all source files (enforced by CI)
clang-format -i src/*.cpp src/*.h

# Check formatting without modifying files
clang-format --dry-run --Werror src/*.cpp src/*.h
```

### Debugging and Monitoring
```bash
# Monitor ESP32 serial output with logs
idf.py -p /dev/ttyACM0 monitor

# For Linux builds, run directly and see console output
./build/src.elf

# Enable WebRTC data channel message logging
export LOG_DATACHANNEL_MESSAGES=1
```

## Architecture Overview

### Core Components

1. **WebRTC Implementation** (`src/webrtc.cpp`)
   - Manages peer connections with Pipecat bots
   - Handles offer/answer exchange via HTTP signaling
   - Implements ICE candidate gathering and STUN/TURN support

2. **Audio Pipeline** (`src/media.cpp` - ESP32 only)
   - Captures audio from microphone (16kHz, mono)
   - Encodes/decodes using Opus codec
   - Plays back received audio through speaker

3. **Network Layer**
   - **WiFi** (`src/wifi.cpp` - ESP32 only): Manages WiFi connectivity
   - **HTTP** (`src/http.cpp`): Handles WebRTC signaling with Pipecat endpoint

4. **Display Support** (`src/screen.cpp` - ESP32 only)
   - Uses LVGL graphics library
   - Shows connection status and audio waveforms
   - Supports ESP32-S3-BOX-3 display

5. **RTVI Support** (`src/rtvi.cpp`, `src/rtvi_callbacks.cpp`)
   - Real-Time Voice Interaction protocol for Pipecat bots
   - Handles WebRTC data channel messaging
   - Manages bot state (speaking/listening) and TTS messages
   - Sends client-ready signals when connection established

### External Components

Located in `esp32-s3-box-3/components/`:
- **esp-libopus**: Opus audio codec optimized for ESP32
- **peer**: WebRTC peer connection implementation
- **srtp**: Secure RTP for encrypted media transport
- **esp-protocols**: ESP32 network protocol implementations

### Platform Differences

**ESP32 Build**:
- Full hardware support (WiFi, audio I/O, display)
- Requires physical device for testing
- Uses ESP-IDF's FreeRTOS tasks

**Linux Build**:
- No WiFi/audio/display support
- Used for testing WebRTC logic without hardware
- Useful for development and debugging

### Configuration

Key configuration files:
- `CMakeLists.txt`: Build configuration, component dependencies
- `sdkconfig.defaults`: ESP-IDF settings (CPU speed, memory, features)
- `partitions.csv`: Flash memory layout
- `.clang-format`: Google-style C++ formatting with 2-space indents

### Important Notes

1. Always run `clang-format` before committing C++ code changes
2. The project uses recursive submodules - clone with `--recursive`
3. No test framework is currently implemented
4. CI/CD builds for both esp32s3 and linux targets on every push/PR
5. Environment variables must be set before building (WIFI credentials and Pipecat URL)
6. The project requires ESP-IDF to be installed and sourced before using idf.py commands
7. The `--preview` flag is required for `idf.py set-target` commands
8. Audio is configured for 16kHz mono with Opus codec
9. Code style uses 80-character line limit and right-aligned pointers (`char *ptr`)