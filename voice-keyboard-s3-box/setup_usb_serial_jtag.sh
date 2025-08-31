#!/bin/bash
# Setup script to enable USB Serial/JTAG for ESP32 Voice Keyboard

echo "🔧 Setting up ESP32 Voice Keyboard with USB Serial/JTAG"
echo "=================================================="

# Copy USB Serial/JTAG config
echo "📝 Enabling USB Serial/JTAG configuration..."
if [ -f "sdkconfig.usb_serial_jtag" ]; then
    cat sdkconfig.usb_serial_jtag >> sdkconfig
    echo "✅ USB Serial/JTAG configuration added to sdkconfig"
else
    echo "❌ sdkconfig.usb_serial_jtag not found"
    exit 1
fi

echo ""
echo "🔨 Building project..."
idf.py build

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Build successful!"
    echo ""
    echo "📋 Next steps:"
    echo "1. Flash to ESP32-S3: idf.py flash"
    echo "2. Configure device: python esp32_voice_keyboard_config.py"
    echo ""
    echo "💡 The ESP32-S3 will now appear as USB Serial/JTAG device (VID:PID 303A:1001)"
else
    echo "❌ Build failed - check errors above"
    exit 1
fi