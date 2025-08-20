#!/bin/bash

# Test build script for Meeting Assistant ESP32

echo "🔧 Testing Meeting Assistant ESP32 Build"
echo "========================================"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ Error: CMakeLists.txt not found!"
    echo "   Make sure you're in the meeting-assistant-esp32-s3-box directory"
    exit 1
fi

# Check if ESP-IDF is sourced
if [ -z "$IDF_PATH" ]; then
    echo "❌ ESP-IDF not found!"
    echo "   Please source ESP-IDF first:"
    echo "   source /path/to/esp-idf/export.sh"
    exit 1
fi

echo "✓ ESP-IDF found at: $IDF_PATH"

# Check environment variables
echo ""
echo "🔍 Checking environment variables..."
if [ -z "$WIFI_SSID" ]; then
    echo "⚠️  WIFI_SSID not set"
    echo "   export WIFI_SSID=\"your_wifi_name\""
fi

if [ -z "$WIFI_PASSWORD" ]; then
    echo "⚠️  WIFI_PASSWORD not set"
    echo "   export WIFI_PASSWORD=\"your_wifi_password\""
fi

if [ -z "$PIPECAT_SMALLWEBRTC_URL" ]; then
    echo "⚠️  PIPECAT_SMALLWEBRTC_URL not set"
    echo "   export PIPECAT_SMALLWEBRTC_URL=\"http://server_ip:8765/api/offer\""
fi

# Set target
echo ""
echo "🎯 Setting ESP32-S3 target..."
idf.py --preview set-target esp32s3

# Clean build
echo ""
echo "🧹 Cleaning previous build..."
idf.py fullclean

# Build
echo ""
echo "🔨 Building Meeting Assistant ESP32..."
idf.py build

if [ $? -eq 0 ]; then
    echo ""
    echo "🎉 Build successful!"
    echo ""
    echo "To flash to your ESP32-S3-Box:"
    echo "  idf.py flash"
    echo ""
    echo "To flash and monitor:"
    echo "  idf.py flash monitor"
else
    echo ""
    echo "❌ Build failed! Check the errors above."
    exit 1
fi