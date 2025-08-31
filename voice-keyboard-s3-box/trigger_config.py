#!/usr/bin/env python3
"""
Quick tool to trigger ESP32 configuration mode manually

This sends 'C' during boot to force configuration mode even if valid config exists.
"""

import serial
import serial.tools.list_ports
import time
import sys

def find_esp32_port():
    """Find ESP32 USB Serial/JTAG port"""
    for port in serial.tools.list_ports.comports():
        if (port.vid, port.pid) in [(0x303A, 0x1001), (0x303A, 0x1002)]:
            return port.device
        elif any(keyword in port.description.lower() for keyword in ['esp32', 'serial jtag']):
            return port.device
    return None

def trigger_config_mode():
    """Trigger configuration mode on ESP32"""
    port = find_esp32_port()
    if not port:
        print("❌ ESP32 USB Serial/JTAG not found")
        print("Available ports:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device} - {p.description}")
        return False
    
    print(f"🔌 Found ESP32 at {port}")
    print("📡 Triggering configuration mode...")
    print("⚠️  Reset ESP32 NOW, then press Enter...")
    input()
    
    try:
        ser = serial.Serial(port, 115200, timeout=1, rtscts=False, dsrdtr=False)
        time.sleep(0.5)  # Brief wait
        
        # Send 'C' multiple times to ensure it's caught during boot
        for i in range(10):
            ser.write(b'C')
            ser.flush()
            time.sleep(0.1)
        
        # Read responses
        print("📥 ESP32 response:")
        start_time = time.time()
        while time.time() - start_time < 5:
            if ser.in_waiting > 0:
                data = ser.readline().decode('utf-8', errors='ignore').strip()
                if data:
                    print(f"   {data}")
                    if "Manual configuration mode" in data or "CONFIG_MODE_READY" in data:
                        print("✅ Configuration mode activated!")
                        ser.close()
                        return True
        
        ser.close()
        print("⚠️  Configuration mode trigger may not have worked")
        print("   Try running: python esp32_voice_keyboard_config.py --force-config")
        return False
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def main():
    print("🔧 ESP32 Voice Keyboard - Configuration Mode Trigger")
    print("=" * 50)
    print("This tool triggers configuration mode even when valid config exists.")
    print()
    
    if trigger_config_mode():
        print()
        print("🎉 Success! Now run the configuration tool:")
        print("   python esp32_voice_keyboard_config.py")
    else:
        print()
        print("💡 Alternative methods:")
        print("   1. python esp32_voice_keyboard_config.py --force-config")
        print("   2. Reset ESP32 multiple times to trigger auto-config")

if __name__ == "__main__":
    main()