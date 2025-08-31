#!/usr/bin/env python3
"""
ESP32 Voice Keyboard Configuration Tool

This tool configures WiFi credentials and server settings for the ESP32 voice keyboard
without requiring firmware recompilation. It communicates with the ESP32 via USB serial
to store settings in NVS (Non-Volatile Storage).
"""

import argparse
import json
import socket
import sys
import time
from typing import Optional, Dict, List, Tuple

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial is required. Install with: pip install pyserial")
    sys.exit(1)


class ESP32VoiceKeyboardConfig:
    """ESP32 Voice Keyboard Configuration Tool"""
    
    # ESP32-S3 USB Serial/JTAG and bridge combinations
    ESP32_USB_IDS = [
        (0x303A, 0x1001),  # ESP32-S3 USB Serial/JTAG (primary)
        (0x303A, 0x1002),  # ESP32-S3 USB Serial/JTAG (alternate)
        (0x10C4, 0xEA60),  # CP2102 USB-UART bridge (fallback)
        (0x1A86, 0x7523),  # CH340 USB-UART bridge (fallback)
        (0x0403, 0x6001),  # FTDI USB-UART bridge (fallback)
    ]
    
    def __init__(self, port: Optional[str] = None, baudrate: int = 115200):
        """Initialize the configuration tool"""
        self.port = port
        self.baudrate = baudrate
        self.serial_connection = None
        self.host_ip = self.get_host_ip()
        
    def get_host_ip(self) -> str:
        """Get the IP address of the system running this script"""
        try:
            # Connect to a remote address to determine local IP
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(("8.8.8.8", 80))
                return s.getsockname()[0]
        except Exception:
            return "127.0.0.1"
    
    def find_esp32_devices(self) -> List[Tuple[str, str]]:
        """Find potential ESP32 devices by USB VID/PID"""
        esp32_ports = []
        
        for port in serial.tools.list_ports.comports():
            # Check by VID/PID
            if (port.vid, port.pid) in self.ESP32_USB_IDS:
                if (port.vid, port.pid) == (0x303A, 0x1001) or (port.vid, port.pid) == (0x303A, 0x1002):
                    esp32_ports.append((port.device, f"ESP32-S3 USB Serial/JTAG - {port.description}"))
                else:
                    esp32_ports.append((port.device, f"ESP32 USB Bridge - {port.description}"))
                continue
                
            # Check by description keywords
            description = port.description.lower()
            if any(keyword in description for keyword in 
                   ['esp32', 's3', 'serial jtag', 'usb jtag', 'silicon labs', 'ch340', 'cp210', 'ftdi']):
                esp32_ports.append((port.device, f"Possible ESP32 - {port.description}"))
        
        return esp32_ports
    
    def select_serial_port(self) -> Optional[str]:
        """Auto-detect or let user select ESP32 serial port"""
        if self.port:
            return self.port
            
        esp32_devices = self.find_esp32_devices()
        all_ports = list(serial.tools.list_ports.comports())
        
        if not all_ports:
            print("❌ No serial ports found!")
            return None
            
        print("\n🔍 Searching for ESP32 devices...")
        
        if len(esp32_devices) == 1:
            port, description = esp32_devices[0]
            print(f"✅ Found ESP32 device: {port} - {description}")
            response = input("Use this device? [Y/n]: ").strip().lower()
            if response in ['', 'y', 'yes']:
                return port
        elif len(esp32_devices) > 1:
            print(f"✅ Found {len(esp32_devices)} potential ESP32 devices:")
            for i, (port, description) in enumerate(esp32_devices):
                print(f"  [{i+1}] {port} - {description}")
        
        # Show all available ports if no clear ESP32 match or user wants to choose
        print(f"\n📋 All available serial ports:")
        for i, port in enumerate(all_ports):
            print(f"  [{i+1}] {port.device} - {port.description}")
        
        try:
            choice = input(f"\nSelect port (1-{len(all_ports)}): ").strip()
            index = int(choice) - 1
            if 0 <= index < len(all_ports):
                return all_ports[index].device
            else:
                print("❌ Invalid selection")
                return None
        except ValueError:
            print("❌ Invalid input")
            return None
    
    def connect_serial(self, port: str) -> bool:
        """Connect to ESP32 via USB Serial/JTAG or serial bridge"""
        try:
            # Optimized settings for USB Serial/JTAG
            self.serial_connection = serial.Serial(
                port, 
                self.baudrate, 
                timeout=5,
                rtscts=False,  # Disable hardware flow control
                dsrdtr=False   # Disable DSR/DTR
            )
            print(f"🔌 Connected to {port} at {self.baudrate} baud")
            time.sleep(3)  # Longer wait for USB Serial/JTAG to be ready
            return True
        except Exception as e:
            print(f"❌ Failed to connect to {port}: {e}")
            return False
    
    def send_command(self, command: str) -> Optional[Dict]:
        """Send command to ESP32 and get JSON response"""
        if not self.serial_connection:
            return None
            
        try:
            print(f"   Sending: {command}")
            
            # Clear any pending input first
            self.serial_connection.reset_input_buffer()
            time.sleep(0.1)
            
            # Send command
            cmd_line = f"{command}\n"
            self.serial_connection.write(cmd_line.encode())
            self.serial_connection.flush()
            
            # Wait for response with better debugging
            response_lines = []
            start_time = time.time()
            json_found = False
            
            while time.time() - start_time < 10 and not json_found:
                if self.serial_connection.in_waiting > 0:
                    line = self.serial_connection.readline().decode(errors='ignore').strip()
                    if line:
                        print(f"   Received line: {line}")
                        response_lines.append(line)
                        
                        # Look for JSON response with prefix
                        if line.startswith('JSON_RESPONSE: '):
                            json_data = line[15:]  # Remove "JSON_RESPONSE: " prefix
                            try:
                                json_response = json.loads(json_data)
                                print(f"   ✅ JSON parsed successfully")
                                return json_response
                            except json.JSONDecodeError as e:
                                print(f"   ⚠️  JSON decode error: {e}")
                                continue
                        # Fallback: Look for JSON response without prefix
                        elif line.startswith('{') and line.endswith('}'):
                            try:
                                json_response = json.loads(line)
                                print(f"   ✅ JSON parsed successfully")
                                return json_response
                            except json.JSONDecodeError as e:
                                print(f"   ⚠️  JSON decode error: {e}")
                                continue
                        elif "DEBUG:" in line:
                            print(f"   📋 Debug message: {line}")
                        elif "CONFIG_MODE_READY" in line:
                            print(f"   📡 Ready signal: {line}")
                else:
                    time.sleep(0.1)
            
            # If no JSON response found
            print(f"   ❌ No valid JSON response after {time.time() - start_time:.1f}s")
            if response_lines:
                print(f"   📝 Raw responses received: {response_lines}")
            
            return {"raw_response": response_lines}
            
        except Exception as e:
            print(f"❌ Error sending command '{command}': {e}")
            return None
    
    def test_esp32_communication(self) -> bool:
        """Test if we can communicate with ESP32 configuration system"""
        print("📡 Testing ESP32 communication...")
        
        # First check if ESP32 is already in config mode by looking for ready signal
        print("   Checking for configuration mode...")
        
        # Clear any pending input and look for CONFIG_MODE_READY
        if self.serial_connection:
            self.serial_connection.reset_input_buffer()
            
            # Wait a bit and check for any incoming data
            time.sleep(1)
            
            # Read any available data
            available_data = []
            start_time = time.time()
            while time.time() - start_time < 3:  # Wait up to 3 seconds
                if self.serial_connection.in_waiting > 0:
                    data = self.serial_connection.readline().decode(errors='ignore').strip()
                    if data:
                        available_data.append(data)
                        print(f"   Received: {data}")
                        if "CONFIG_MODE_READY" in data:
                            print("✅ ESP32 is in configuration mode!")
                            break
                time.sleep(0.1)
        
        # Send a simple test first
        print("   Sending test command...")
        self.serial_connection.write(b"CONFIG_IDENTIFY\n")
        # self.serial_connection.write(b"CONFIG_IDENTIFY\n")
        self.serial_connection.flush()
        
        # Wait for response
        time.sleep(1)
        test_response = []
        start_time = time.time()
        while time.time() - start_time < 5:
            if self.serial_connection.in_waiting > 0:
                data = self.serial_connection.readline().decode(errors='ignore').strip()
                if data:
                    test_response.append(data)
                    print(f"   Test response: {data}")
            else:
                time.sleep(0.1)
        
        # Now try to send identify command properly
        response = self.send_command("CONFIG_IDENTIFY")
        print(f"   Test response: {response}")
        if response and response.get("device") == "ESP32_VOICE_KEYBOARD":
            print("✅ ESP32 Voice Keyboard detected!")
            return True
        
        print("⚠️  ESP32 may not be in configuration mode.")
        print("   Debug info:")
        if available_data:
            print(f"   Raw data received: {available_data}")
        else:
            print("   No data received from ESP32")
        print("   Try:")
        print("   1. Reset the ESP32 device")
        print("   2. Check if it's running the updated firmware")
        print("   3. Verify serial connection")
        return False
    
    def get_current_config(self) -> Optional[Dict]:
        """Get current configuration from ESP32"""
        print("📋 Reading current ESP32 configuration...")
        
        response = self.send_command("CONFIG_GET_ALL")
        if response and "config" in response:
            return response["config"]
        
        print("⚠️  Could not read current configuration")
        return None
    
    def scan_wifi_networks(self) -> Optional[List[Dict]]:
        """Scan for available WiFi networks"""
        print("📶 Scanning for WiFi networks...")
        
        response = self.send_command("CONFIG_WIFI_SCAN")
        if response and "networks" in response:
            return response["networks"]
        
        print("⚠️  WiFi scan failed")
        return None
    
    def configure_wifi(self, current_config: Dict) -> Tuple[str, str]:
        """Configure WiFi settings"""
        current_ssid = current_config.get("wifi_ssid", "")
        
        print(f"\n🔧 WiFi Configuration")
        if current_ssid:
            print(f"Current WiFi: {current_ssid}")
            keep_current = input("Keep current WiFi settings? [Y/n]: ").strip().lower()
            if keep_current in ['', 'y', 'yes']:
                return current_ssid, current_config.get("wifi_password", "")
        
        # Scan for networks
        networks = self.scan_wifi_networks()
        if networks:
            print(f"\nAvailable 2.4GHz networks:")
            for i, network in enumerate(networks):
                security = "🔒" if network.get("auth_mode", 0) > 0 else "🔓"
                print(f"  [{i+1}] {security} {network['ssid']} (Signal: {network.get('rssi', 'Unknown')})")
            
            print(f"  [{len(networks)+1}] Enter custom SSID")
            
            try:
                choice = input(f"\nSelect network (1-{len(networks)+1}): ").strip()
                choice_idx = int(choice) - 1
                
                if 0 <= choice_idx < len(networks):
                    ssid = networks[choice_idx]["ssid"]
                else:
                    ssid = input("Enter WiFi SSID: ").strip()
            except ValueError:
                ssid = input("Enter WiFi SSID: ").strip()
        else:
            ssid = input("Enter WiFi SSID: ").strip()
        
        password = input(f"Enter password for '{ssid}': ").strip()
        
        return ssid, password
    
    def configure_server(self, current_config: Dict) -> Tuple[str, int]:
        """Configure server settings"""
        current_url = current_config.get("server_url", "")
        current_port = current_config.get("server_port", 8765)
        
        print(f"\n🖥️  Server Configuration")
        print(f"Host system IP: {self.host_ip}")
        
        # Always use host system IP, only ask about port
        suggested_url = f"http://{self.host_ip}:{current_port}/api/offer"
        print(f"Using server URL: {suggested_url}")
        
        # Only ask about port if user wants to change it
        if current_url and current_port != 8765:
            print(f"Current port: {current_port}")
            change_port = input("Change server port? [y/N]: ").strip().lower()
            if change_port not in ['y', 'yes']:
                return self.host_ip, current_port
        
        # Ask for port (with default)
        try:
            port_input = input(f"Enter server port [{current_port}]: ").strip()
            server_port = int(port_input) if port_input else current_port
        except ValueError:
            print("Invalid port, using default")
            server_port = current_port
        
        return self.host_ip, server_port
    
    def save_config_to_esp32(self, wifi_ssid: str, wifi_password: str, 
                           server_ip: str, server_port: int) -> bool:
        """Save configuration to ESP32 NVS"""
        print("\n💾 Saving configuration to ESP32...")
        
        config_data = {
            "wifi_ssid": wifi_ssid,
            "wifi_password": wifi_password,
            "server_ip": server_ip,
            "server_port": server_port,
            "server_url": f"http://{server_ip}:{server_port}/api/offer"
        }
        
        command = f"CONFIG_SAVE {json.dumps(config_data)}"
        response = self.send_command(command)
        
        if response and response.get("status") == "success":
            print("✅ Configuration saved successfully!")
            return True
        else:
            print("❌ Failed to save configuration")
            return False
    
    def restart_esp32(self) -> None:
        """Restart ESP32 to apply new configuration"""
        print("\n🔄 Restarting ESP32...")
        response = self.send_command("CONFIG_RESTART")
        if response and response.get("status") == "restarting":
            print("✅ ESP32 will restart and connect with new settings")
        else:
            print("⚠️  Manual reset may be required")
    
    def run_configuration(self, force_config: bool = False) -> None:
        """Run the complete configuration process"""
        print("🎹 ESP32 Voice Keyboard Configuration Tool")
        print("=" * 50)
        
        # Find and connect to ESP32
        port = self.select_serial_port()
        if not port:
            print("❌ No serial port selected")
            return
        
        if not self.connect_serial(port):
            return
        
        # Test communication (skip if forced)
        if not force_config and not self.test_esp32_communication():
            print("\n💡 Make sure ESP32 is running the updated firmware with configuration support")
            print("   Or use --force-config to skip communication test")
            return
        elif force_config:
            print("\n⚠️  Skipping communication test (forced configuration mode)")
        
        # Get current configuration
        current_config = self.get_current_config() or {}
        
        # Configure WiFi
        wifi_ssid, wifi_password = self.configure_wifi(current_config)
        if not wifi_ssid:
            print("❌ WiFi SSID is required")
            return
        
        # Configure server
        server_ip, server_port = self.configure_server(current_config)
        
        # Confirmation
        print(f"\n📋 Configuration Summary:")
        print(f"   WiFi SSID: {wifi_ssid}")
        print(f"   WiFi Password: {'*' * len(wifi_password)}")
        print(f"   Server: http://{server_ip}:{server_port}/api/offer")
        
        confirm = input("\nSave configuration? [Y/n]: ").strip().lower()
        if confirm not in ['', 'y', 'yes']:
            print("❌ Configuration cancelled")
            return
        
        # Save and restart
        if self.save_config_to_esp32(wifi_ssid, wifi_password, server_ip, server_port):
            self.restart_esp32()
        
        # Close connection
        if self.serial_connection:
            self.serial_connection.close()
        
        print("\n🎉 Configuration complete!")
        print("   The ESP32 should now connect automatically when powered on.")
        print("\n💡 To reconfigure later:")
        print("   1. Power on ESP32 and wait for it to attempt connection")
        print("   2. If no WiFi/server connection, it will enter config mode automatically")
        print("   3. Or reset ESP32 while it's trying to connect")
        print("   4. Look for 'CONFIG_MODE_READY' messages")
        print("   5. Run: python esp32_voice_keyboard_config.py")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="ESP32 Voice Keyboard Configuration Tool")
    parser.add_argument("--port", help="Serial port (auto-detect if not specified)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports")
    parser.add_argument("--force-config", action="store_true", help="Force configuration mode (skip connection test)")
    
    args = parser.parse_args()
    
    if args.list_ports:
        print("Available serial ports:")
        for port in serial.tools.list_ports.comports():
            print(f"  {port.device} - {port.description}")
        return
    
    config_tool = ESP32VoiceKeyboardConfig(port=args.port, baudrate=args.baudrate)
    config_tool.run_configuration(force_config=args.force_config)


if __name__ == "__main__":
    main()