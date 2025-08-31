# ESP32 Voice Keyboard - Dynamic Configuration System

This guide explains how to use the new dynamic configuration system that eliminates the need for firmware recompilation when changing WiFi networks or server settings.

## Overview

The ESP32 Voice Keyboard now supports runtime configuration through a USB serial interface. WiFi credentials and server settings are stored in the ESP32's NVS (Non-Volatile Storage) and persist across power cycles.

## Key Benefits

- ✅ **No Recompilation Required**: Change networks without rebuilding firmware
- ✅ **Persistent Storage**: Settings survive power cycles and firmware updates  
- ✅ **Cross-Network Support**: ESP32 (2.4GHz) works with servers on 5GHz networks
- ✅ **Auto-Discovery**: Automatic server IP detection and WiFi network scanning
- ✅ **Plug-and-Play**: Professional configuration experience

## Quick Start

### 1. Build and Flash Firmware (One-time)

```bash
cd pipecat-esp32/voice-keyboard-s3-box
source path/to/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash  # Linux
# or
idf.py flash  # macOS
```

**Note**: No environment variables required! The old `WIFI_SSID`, `WIFI_PASSWORD`, and `PIPECAT_SMALLWEBRTC_URL` are no longer needed.

### 2. Configure the Device

When first powered on (or after configuration is cleared), the ESP32 will enter configuration mode:

```
🔧 ESP32 Voice Keyboard - Configuration Required
Connect via USB serial and use the configuration tool
Run: python esp32_voice_keyboard_config.py
```

### 3. Run Configuration Tool

```bash
# Install requirements
pip install pyserial

# Run configuration tool
python esp32_voice_keyboard_config.py
```

The tool will:
- Auto-detect your ESP32 device
- Show current configuration (if any)
- Scan for available WiFi networks
- Auto-detect your server IP address
- Guide you through setup

### 4. Ready to Use!

Once configured, the ESP32 will display:
```
✅ ESP32 Voice Keyboard Ready!
   WiFi: YourNetwork_2.4G
   Server: http://192.168.1.100:8765/api/offer
   Press button to start voice typing
```

## Configuration Tool Usage

### Basic Usage

```bash
python esp32_voice_keyboard_config.py
```

### Advanced Options

```bash
# List available serial ports
python esp32_voice_keyboard_config.py --list-ports

# Use specific serial port
python esp32_voice_keyboard_config.py --port /dev/ttyUSB0

# Custom baudrate
python esp32_voice_keyboard_config.py --baudrate 230400
```

## Configuration Process

### Step-by-Step Walkthrough

1. **Device Detection**
   ```
   🔍 Searching for ESP32 devices...
   ✅ Found ESP32 device: /dev/ttyACM0 - ESP32-S3 CDC
   Use this device? [Y/n]: 
   ```

2. **Current Configuration**
   ```
   📋 Reading current ESP32 configuration...
   Current WiFi: MyNetwork_5G
   Keep current WiFi settings? [Y/n]: n
   ```

3. **WiFi Network Selection**
   ```
   📶 Scanning for WiFi networks...
   Available 2.4GHz networks:
     [1] 🔒 MyNetwork_2.4G (Signal: -45)
     [2] 🔒 Neighbor_WiFi (Signal: -67)
     [3] Enter custom SSID
   Select network (1-3): 1
   ```

4. **Password Entry**
   ```
   Enter password for 'MyNetwork_2.4G': [hidden input]
   ```

5. **Server Configuration**
   ```
   🖥️  Server Configuration
   Host system IP: 192.168.1.100
   Suggested server URL: http://192.168.1.100:8765/api/offer
   Use suggested settings? [Y/n]: 
   ```

6. **Confirmation and Save**
   ```
   📋 Configuration Summary:
      WiFi SSID: MyNetwork_2.4G
      WiFi Password: ********
      Server: http://192.168.1.100:8765/api/offer
   
   Save configuration? [Y/n]: y
   ```

7. **Device Restart**
   ```
   💾 Saving configuration to ESP32...
   ✅ Configuration saved successfully!
   🔄 Restarting ESP32...
   
   🎉 Configuration complete!
   ```

## Network Requirements

### WiFi Frequency Support
- **ESP32-S3**: Only supports 2.4GHz WiFi networks
- **5GHz Networks**: ESP32 cannot connect directly to 5GHz-only networks
- **Dual-Band Routers**: Most routers broadcast both 2.4GHz and 5GHz simultaneously

### Cross-Band Communication
✅ **ESP32 (2.4GHz) can communicate with servers on 5GHz networks**

This works because:
- Both devices connect to the same router
- Router bridges traffic between 2.4GHz and 5GHz bands
- They share the same IP subnet (e.g., 192.168.1.x)

Example setup:
- **Your Computer**: Connected to 5GHz WiFi (192.168.1.100)
- **ESP32**: Connected to 2.4GHz WiFi (192.168.1.150)
- **Communication**: Works perfectly through the router

## Troubleshooting

### ESP32 Not Detected

```bash
# List all serial ports
python esp32_voice_keyboard_config.py --list-ports

# Try different ports manually
python esp32_voice_keyboard_config.py --port /dev/ttyUSB0
```

### Configuration Mode Not Starting

1. **Power cycle** the ESP32
2. Check serial connection
3. Verify firmware is flashed correctly

### WiFi Connection Fails

1. **Check network frequency**: Ensure you're connecting to a 2.4GHz network
2. **Verify password**: Re-run configuration tool
3. **Check router settings**: Ensure 2.4GHz is enabled
4. **Signal strength**: Move closer to router

### Server Connection Fails

1. **Verify server is running**: Start your voice keyboard server
   ```bash
   python voice_keyboard_server.py
   ```
2. **Check IP address**: Ensure server IP is correct
3. **Firewall**: Check that port 8765 is not blocked
4. **Network connectivity**: Test ping between devices

## Advanced Configuration

### Manual Configuration Commands

For advanced users, you can send commands directly via serial:

```bash
# Connect to ESP32 serial terminal
screen /dev/ttyACM0 115200

# Available commands:
CONFIG_IDENTIFY          # Get device info
CONFIG_GET_ALL          # Show current config
CONFIG_WIFI_SCAN        # Scan WiFi networks
CONFIG_SAVE {...}       # Save new configuration
CONFIG_RESTART          # Restart device
```

### Clearing Configuration

To reset configuration and return to setup mode:

```python
# In configuration tool, use:
CONFIG_SAVE {"wifi_ssid": "", "server_ip": "", "server_port": 0}
```

### Backup and Restore

Configuration is stored in ESP32's NVS flash and automatically persists across:
- Power cycles
- Firmware updates (if NVS partition is preserved)
- Network changes

## Integration with Voice Keyboard Server

### Server Setup

1. **Start the server** on your host machine:
   ```bash
   cd pipecat/demo/voice-keyboard/
   python voice_keyboard_server.py --host 0.0.0.0
   ```

2. **Configuration tool** will automatically detect your host IP
3. **ESP32** connects to: `http://YOUR_HOST_IP:8765/api/offer`

### Multiple Devices

Each ESP32 can have different configurations:
- Different WiFi networks
- Different server endpoints
- Settings stored independently

## File Structure

```
pipecat-esp32/voice-keyboard-s3-box/
├── esp32_voice_keyboard_config.py    # Configuration tool
├── CONFIGURATION.md                   # This guide
├── src/
│   ├── config.h                      # Configuration system header
│   ├── config.cpp                    # NVS storage implementation
│   ├── config_serial.h              # Serial interface header  
│   ├── config_serial.cpp            # Serial protocol implementation
│   ├── main.cpp                     # Updated boot sequence
│   └── ...                          # Other source files
└── ...
```

## Migration from Old System

### Before (Required Environment Variables)
```bash
export WIFI_SSID="YourWiFiSSID"
export WIFI_PASSWORD="YourWiFiPassword" 
export PIPECAT_SMALLWEBRTC_URL="http://192.168.1.100:8765/api/offer"
idf.py build  # Required for every network change
```

### After (Dynamic Configuration)
```bash
# Build once
idf.py build

# Configure dynamically  
python esp32_voice_keyboard_config.py

# Ready to use on any network!
```

The new system eliminates the need for recompilation and makes the device truly portable across different network environments.