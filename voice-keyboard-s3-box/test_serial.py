#!/usr/bin/env python3
"""
Simple serial communication test for ESP32 Voice Keyboard

This script tests basic serial communication without the complex configuration logic.
"""

import serial
import serial.tools.list_ports
import time
import sys

def find_esp32_port():
    """Find potential ESP32 port"""
    esp32_ports = []
    for port in serial.tools.list_ports.comports():
        if (port.vid, port.pid) in [(0x303A, 0x1001), (0x10C4, 0xEA60), (0x1A86, 0x7523)]:
            esp32_ports.append(port.device)
        elif any(keyword in port.description.lower() for keyword in ['esp32', 's3', 'usb jtag']):
            esp32_ports.append(port.device)
    
    return esp32_ports

def test_serial_communication():
    """Test basic serial communication with ESP32"""
    print("🔍 ESP32 Serial Communication Test")
    print("=" * 40)
    
    # Find ESP32 ports
    esp32_ports = find_esp32_port()
    if not esp32_ports:
        print("❌ No ESP32 devices found")
        return False
    
    port = esp32_ports[0]
    print(f"📱 Using port: {port}")
    
    try:
        # Connect to serial port
        print("🔌 Connecting...")
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)  # Wait for connection to stabilize
        
        print("📡 Listening for data from ESP32...")
        print("   (Make sure ESP32 is in configuration mode)")
        print("   Press Ctrl+C to stop\n")
        
        # Clear any pending data
        ser.reset_input_buffer()
        
        # Listen for data
        start_time = time.time()
        data_received = []
        
        try:
            while time.time() - start_time < 30:  # Listen for 30 seconds
                if ser.in_waiting > 0:
                    try:
                        data = ser.readline().decode('utf-8', errors='ignore').strip()
                        if data:
                            print(f"📥 Received: {data}")
                            data_received.append(data)
                            
                            # Test sending a command if we see CONFIG_MODE_READY
                            if "CONFIG_MODE_READY" in data:
                                print("✅ ESP32 is ready! Sending test command...")
                                ser.write(b"CONFIG_IDENTIFY\n")
                                ser.flush()
                    except UnicodeDecodeError:
                        print("📥 Received: [binary data]")
                else:
                    time.sleep(0.1)
                    
        except KeyboardInterrupt:
            print("\n⚠️  Interrupted by user")
        
        ser.close()
        
        # Summary
        print(f"\n📊 Test Results:")
        print(f"   Data received: {len(data_received)} lines")
        if data_received:
            print("✅ Communication working!")
            print("   Sample data:")
            for line in data_received[:5]:  # Show first 5 lines
                print(f"     {line}")
            if len(data_received) > 5:
                print(f"     ... and {len(data_received) - 5} more lines")
            return True
        else:
            print("❌ No data received from ESP32")
            print("   Possible issues:")
            print("   1. ESP32 not in configuration mode")
            print("   2. Wrong serial port")
            print("   3. ESP32 not running updated firmware")
            return False
            
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    print("ESP32 Serial Communication Test")
    print("Make sure ESP32 is powered and in configuration mode")
    print("(Should show 'CONFIG_MODE_READY' message)\n")
    
    success = test_serial_communication()
    
    if success:
        print("\n🎉 Serial communication is working!")
        print("You can now use the full configuration tool:")
        print("python esp32_voice_keyboard_config.py")
    else:
        print("\n💥 Serial communication failed")
        print("Check ESP32 connection and firmware")
        
    sys.exit(0 if success else 1)