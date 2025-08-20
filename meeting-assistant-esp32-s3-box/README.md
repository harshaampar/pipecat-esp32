# Meeting Assistant - ESP32-S3-Box Client

This is the ESP32-S3-Box hardware client for the Meeting Assistant demo. It provides one-button meeting transcription with real-time audio capture and a touch interface.

This project is specifically configured for the **original ESP32-S3-Box** development board. It streams audio to a meeting server for live transcription using Deepgram Nova-3.

## 🔧 Prerequisites

- ESP-IDF v5.4 or later
- Original ESP32-S3-Box development board
- USB cable for programming

## 📋 Quick Start

### 1. Set Environment Variables

```bash
export WIFI_SSID="your_wifi_name"
export WIFI_PASSWORD="your_wifi_password"
export PIPECAT_SMALLWEBRTC_URL="http://your-server-ip:8765/api/offer"
```

### 2. Build and Flash

**Option A: Using the build script (recommended)**
```bash
./build.sh              # Build only
./build.sh flash        # Build and flash
```

**Option B: Manual commands**
```bash
# Source ESP-IDF environment
source ../../esp/esp-idf/export.sh

# Build project
idf.py build

# Flash and monitor
idf.py flash monitor
```

## 🎙️ Meeting Features

- **One-Touch Meeting Control**: Start/stop meetings with screen buttons
- **Real-Time Audio Streaming**: WebRTC audio input to meeting server
- **Visual Meeting Status**: Pulsing stop button and meeting timer
- **Live Transcription**: Audio transcribed using Deepgram Nova-3
- **Web Dashboard**: View live transcripts on computer/phone browser

## 🔄 Meeting Flow

1. **Idle State**: Shows "Start Meeting" button
2. **Press Start**: Connects to server via WebRTC
3. **Active Meeting**: Shows timer, "Transcribing..." text, pulsing "Stop Meeting" button
4. **Press Stop**: Ends meeting, returns to idle state

## 🐛 Troubleshooting

### Build Issues
- Ensure ESP-IDF v5.4+ is installed and sourced
- Check that all environment variables are set
- Try cleaning: `idf.py fullclean`

### Touch Issues
- This project uses the correct touch controller for ESP32-S3-Box
- If you have an ESP32-S3-Box-3, use the `esp32-s3-box-3` folder instead

### Flash Issues
- Make sure your ESP32-S3-Box is connected via USB
- Check that the correct port is detected: `idf.py flash --list-ports`
- Try holding the BOOT button while connecting if flashing fails

## 📁 Project Structure

```
esp32-s3-box/
├── src/                     # Main source code
│   ├── main.cpp            # Application entry point
│   ├── screen.cpp          # Display and UI handling
│   └── media.cpp           # Audio capture/playback
├── components/             # Custom components
├── managed_components/     # ESP-IDF managed components
├── dependencies.lock       # Dependency versions
├── build.sh               # Build script
└── README.md              # This file
```

## 🔍 Key Changes from ESP32-S3-Box-3

- **BSP**: Changed from `esp-box-3` to `esp-box`
- **Touch Controller**: Uses TT21100 instead of GT911
- **Pin Configurations**: Optimized for original ESP32-S3-Box layout
- **Dependencies**: All managed components updated for original hardware

## 📖 Additional Resources

- [ESP32-S3-Box Documentation](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-box/index.html)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [Pipecat Documentation](https://docs.pipecat.ai/) 