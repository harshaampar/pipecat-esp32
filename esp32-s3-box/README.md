# ESP32-S3-Box Pipecat Project

This project is specifically configured for the **original ESP32-S3-Box** development board (not the ESP32-S3-Box-3). It uses the correct Board Support Package (BSP) for your hardware.

## 🔧 Prerequisites

- ESP-IDF v5.4 or later
- Original ESP32-S3-Box development board
- USB cable for programming

## 📋 Quick Start

### 1. Set Environment Variables

```bash
export WIFI_SSID="your_wifi_name"
export WIFI_PASSWORD="your_wifi_password"
export PIPECAT_SMALLWEBRTC_URL="wss://your-server-url"
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

## 🔄 Hardware Differences

This project is configured for:
- **ESP32-S3-Box** (original) ✅
- Uses `espressif/esp-box` BSP v3.1.0
- Compatible with TT21100 touch controller
- Correct pin mappings for original hardware

**Not compatible with:**
- ESP32-S3-Box-3 (newer version) ❌
- Generic ESP32-S3 chips ❌

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