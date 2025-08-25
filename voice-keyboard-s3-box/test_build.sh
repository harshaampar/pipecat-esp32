#!/bin/bash

# Test build script for voice-keyboard ESP32 project
# This tests both Linux and ESP32 builds

echo "🔧 Testing Voice Keyboard ESP32 Build..."

# Set required environment variables for testing
export WIFI_SSID="TestSSID"
export WIFI_PASSWORD="TestPassword"
export PIPECAT_SMALLWEBRTC_URL="http://localhost:8765/api/offer"

# Test Linux build first (faster)
echo "📦 Testing Linux build..."
idf.py --preview set-target linux
if idf.py build; then
    echo "✅ Linux build successful"
else
    echo "❌ Linux build failed"
    exit 1
fi

# Test ESP32-S3 build (actual target)
echo "📦 Testing ESP32-S3 build..." 
idf.py --preview set-target esp32s3
if idf.py build; then
    echo "✅ ESP32-S3 build successful"
    echo "🎉 All builds completed successfully!"
    echo ""
    echo "Next steps:"
    echo "1. Set your WiFi credentials: export WIFI_SSID=your_ssid WIFI_PASSWORD=your_password"
    echo "2. Set server URL: export PIPECAT_SMALLWEBRTC_URL=http://YOUR_SERVER_IP:8765/api/offer"
    echo "3. Flash to device: idf.py -p /dev/ttyACM0 flash (Linux) or idf.py flash (macOS)"
else
    echo "❌ ESP32-S3 build failed"
    exit 1
fi