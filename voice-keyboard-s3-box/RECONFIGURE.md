# ESP32 Voice Keyboard - Reconfiguration Guide

## How to Reconfigure Your ESP32 Voice Keyboard

### Automatic Configuration Mode

The ESP32 automatically enters configuration mode when:
1. **First boot** (no saved configuration)
2. **WiFi connection fails** after multiple attempts
3. **Server connection fails** after WiFi connects

**Signs ESP32 is in configuration mode:**
- Serial output shows `CONFIG_MODE_READY` messages
- ESP32 appears as USB Serial/JTAG device (VID:PID 303A:1001)

### Manual Reconfiguration

**Method 1: Manual trigger during boot (RECOMMENDED)**
```bash
# 1. Run the trigger script:
python trigger_config.py
# 2. When prompted, reset ESP32
# 3. Script will send 'C' during boot to force config mode
# 4. Then run: python esp32_voice_keyboard_config.py
```

**Method 2: Manual trigger (advanced)**
```bash
# 1. Reset ESP32
# 2. Quickly send 'C' within 3 seconds of boot
# 3. Look for "Manual configuration mode triggered!"
# 4. Run: python esp32_voice_keyboard_config.py
```

**Method 3: Force configuration mode**
```bash
# Skip communication test and force configuration:
python esp32_voice_keyboard_config.py --force-config
```

**Method 4: List available ports first**
```bash
# See all serial ports:
python esp32_voice_keyboard_config.py --list-ports

# Then configure specific port:
python esp32_voice_keyboard_config.py --port /dev/cu.usbmodem101
```

### Configuration Behavior

**Server Settings:**
- Always uses your computer's current IP address
- Only asks about port number (default: 8765)
- No more confusing IP address prompts!

**WiFi Settings:**
- Scans for available networks
- Remembers previous SSID if configured
- Shows signal strength for network selection

### Troubleshooting

**ESP32 not detected:**
1. Check USB cable (must be data cable, not power-only)
2. Look for VID:PID 303A:1001 in device manager
3. Try different USB port
4. Reset ESP32 and retry

**Configuration mode not activating:**
1. Reset ESP32 multiple times
2. Wait for it to fail WiFi connection attempts
3. Use `--force-config` option
4. Check serial output for CONFIG_MODE_READY messages

**Connection issues:**
- Ensure ESP32-S3-BOX supports USB Serial/JTAG
- Verify firmware is built with USB Serial/JTAG enabled
- Try lower baud rate: `--baudrate 9600`