#!/usr/bin/env python3
"""
Test build script for the ESP32 Voice Keyboard with dynamic configuration

This script verifies that the new configuration system builds correctly
without requiring environment variables.
"""

import subprocess
import sys
import os
from pathlib import Path

def run_command(cmd, cwd=None, capture_output=True):
    """Run a command and return the result"""
    try:
        result = subprocess.run(
            cmd, 
            shell=True, 
            cwd=cwd, 
            capture_output=capture_output,
            text=True,
            timeout=300  # 5 minute timeout
        )
        return result.returncode == 0, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return False, "", "Command timed out"
    except Exception as e:
        return False, "", str(e)

def check_esp_idf():
    """Check if ESP-IDF is available"""
    success, stdout, stderr = run_command("idf.py --version")
    if success:
        print(f"✅ ESP-IDF found: {stdout.strip()}")
        return True
    else:
        print("❌ ESP-IDF not found. Please run:")
        print("   source /path/to/esp-idf/export.sh")
        return False

def test_build():
    """Test building the ESP32 firmware"""
    project_dir = Path(__file__).parent
    
    print("🔧 Testing ESP32 Voice Keyboard build...")
    print(f"   Project directory: {project_dir}")
    
    # Check ESP-IDF availability
    if not check_esp_idf():
        return False
    
    # Clean previous build
    print("\n🧹 Cleaning previous build...")
    success, stdout, stderr = run_command("idf.py clean", cwd=project_dir)
    if not success:
        print(f"⚠️  Clean failed (this is usually OK): {stderr}")
    
    # Configure build
    print("\n⚙️  Configuring build...")
    success, stdout, stderr = run_command("idf.py set-target esp32s3", cwd=project_dir)
    if not success:
        print(f"❌ Configure failed: {stderr}")
        return False
    
    # Build the project
    print("\n🔨 Building project...")
    print("   This may take several minutes...")
    
    success, stdout, stderr = run_command("idf.py build", cwd=project_dir, capture_output=False)
    if success:
        print("\n✅ Build completed successfully!")
        print("\n🎉 Configuration system is ready to use!")
        print("\nNext steps:")
        print("1. Flash to ESP32: idf.py flash")
        print("2. Configure device: python esp32_voice_keyboard_config.py")
        return True
    else:
        print(f"\n❌ Build failed!")
        print("Check the output above for errors.")
        return False

def show_usage():
    """Show usage information"""
    print("ESP32 Voice Keyboard - Build Test")
    print("=" * 40)
    print()
    print("This script tests that the new dynamic configuration system")
    print("builds correctly without requiring environment variables.")
    print()
    print("Prerequisites:")
    print("1. ESP-IDF must be installed and sourced")
    print("2. ESP32-S3 target must be supported")
    print()
    print("Usage:")
    print("  python test_build.py")
    print()

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ["-h", "--help"]:
        show_usage()
        sys.exit(0)
    
    print("ESP32 Voice Keyboard - Dynamic Configuration Build Test")
    print("=" * 60)
    print()
    print("Testing the new system that eliminates compile-time")
    print("environment variables for WiFi and server settings.")
    print()
    
    success = test_build()
    
    if success:
        print("\n🎊 SUCCESS: Dynamic configuration system is working!")
        sys.exit(0)
    else:
        print("\n💥 FAILED: Build encountered errors")
        sys.exit(1)