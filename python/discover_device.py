#!/usr/bin/env python3
"""
Weather-Life USB Device Discovery Tool
Finds and identifies the weather dongle on the system
"""

import sys
import os

# Add platform-specific USB libraries
try:
    import usb.core
    import usb.util
    HAS_PYUSB = True
except ImportError:
    HAS_PYUSB = False
    print("Warning: pyusb not installed. Install with: pip install pyusb")

class WeatherDeviceDiscovery:
    """Discover and test weather USB devices."""
    
    # Known device identifiers from binary analysis
    KNOWN_DEVICES = [
        # Silicon Labs - MOST LIKELY (dominant in binaries)
        {
            'vid': 0x10c4,
            'pid': 0xea60,
            'name': 'Silicon Labs CP2102',
            'description': 'USB-to-Serial (most common weather device)'
        },
        {
            'vid': 0x10c4,
            'pid': 0xea61,
            'name': 'Silicon Labs CP2103',
            'description': 'USB-to-Serial variant'
        },
        
        # Microchip Technology
        {
            'vid': 0x0424,
            'pid': 0x274a,
            'name': 'Microchip USB Bridge',
            'description': 'USB Bridge device'
        },
        
        # FTDI
        {
            'vid': 0x0403,
            'pid': 0x6001,
            'name': 'FTDI FT232R',
            'description': 'USB Serial adapter'
        },
        
        # Generic/DIY
        {
            'vid': 0x1209,
            'pid': 0x0001,
            'name': 'InterBiometrics Generic',
            'description': 'Open vendor ID'
        },
        {
            'vid': 0x16c0,
            'pid': 0x0483,
            'name': 'Van Ooijen DIY',
            'description': 'DIY USB device'
        },
    ]
    
    def __init__(self):
        self.found_devices = []
        self.context = None
    
    def discover_windows(self):
        """Discover devices on Windows using COM ports and registry."""
        print("\n[Windows] Discovering USB devices...")
        
        try:
            import winreg
            import serial
            from serial.tools import list_ports
            
            # Check all COM ports
            print("  Checking COM ports...")
            for port_info in list_ports.comports():
                port = port_info.device
                vid_pid = port_info.hwid
                
                if 'VID' in vid_pid:
                    print(f"    Found: {port} - {vid_pid}")
                    self.found_devices.append({
                        'port': port,
                        'hwid': vid_pid,
                        'type': 'COM'
                    })
            
            # Check USB devices in registry
            print("  Checking USB registry...")
            reg_path = r'SYSTEM\CurrentControlSet\Enum\USB'
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, reg_path) as key:
                    for i in range(winreg.QueryInfoKey(key)[0]):
                        subkey_name = winreg.EnumKeyEx(key, i)
                        if 'VID' in subkey_name:
                            print(f"    Device: {subkey_name}")
                            self.found_devices.append({
                                'device_id': subkey_name,
                                'type': 'USB Registry'
                            })
            except Exception as e:
                print(f"    Registry access error: {e}")
                
        except ImportError:
            print("  Note: serial library not installed (pip install pyserial)")
    
    def discover_linux(self):
        """Discover devices on Linux using /dev/ttyUSB* and dmesg."""
        print("\n[Linux] Discovering USB devices...")
        
        import subprocess
        import glob
        
        # Check /dev/ttyUSB* devices
        print("  Checking /dev/ttyUSB* devices...")
        for device in glob.glob('/dev/ttyUSB*'):
            print(f"    Found: {device}")
            self.found_devices.append({
                'device': device,
                'type': 'UART'
            })
        
        # Check dmesg for USB device info
        print("  Checking dmesg for USB devices...")
        try:
            output = subprocess.check_output(['dmesg'], stderr=subprocess.DEVNULL).decode()
            for line in output.split('\n'):
                if 'USB' in line and ('VID' in line or 'Device' in line):
                    print(f"    {line.strip()[:80]}")
        except:
            pass
    
    def discover_macos(self):
        """Discover devices on macOS using ioreg."""
        print("\n[macOS] Discovering USB devices...")
        
        import subprocess
        
        try:
            output = subprocess.check_output(['ioreg', '-p', 'IOUSB'], 
                                            stderr=subprocess.DEVNULL).decode()
            print("  USB Devices found (via ioreg):")
            for line in output.split('\n'):
                if '@' in line and 'USB' in line:
                    print(f"    {line.strip()[:80]}")
                    self.found_devices.append({
                        'device': line.strip(),
                        'type': 'IOKit'
                    })
        except:
            print("  ioreg not available (requires admin)")
    
    def discover_with_pyusb(self):
        """Use pyusb to find known devices."""
        if not HAS_PYUSB:
            return
        
        print("\n[pyusb] Scanning for known devices...")
        
        for known_dev in self.KNOWN_DEVICES:
            vid = known_dev['vid']
            pid = known_dev['pid']
            
            try:
                dev = usb.core.find(idVendor=vid, idProduct=pid)
                if dev:
                    print(f"  FOUND: {known_dev['name']} (VID:{vid:04x} PID:{pid:04x})")
                    self.found_devices.append({
                        'name': known_dev['name'],
                        'vid': vid,
                        'pid': pid,
                        'type': 'pyusb',
                        'usb_device': dev,
                        'description': known_dev['description']
                    })
            except:
                pass
    
    def discover(self):
        """Run full device discovery."""
        print("\n" + "="*70)
        print("WEATHER-LIFE USB DEVICE DISCOVERY")
        print("="*70)
        
        platform = sys.platform
        
        if platform == 'win32':
            self.discover_windows()
        elif platform.startswith('linux'):
            self.discover_linux()
        elif platform == 'darwin':
            self.discover_macos()
        else:
            print(f"Unknown platform: {platform}")
        
        # Always try pyusb
        self.discover_with_pyusb()
        
        return self.found_devices
    
    def report(self):
        """Print discovery results."""
        print("\n" + "="*70)
        print("DISCOVERY RESULTS")
        print("="*70)
        
        if not self.found_devices:
            print("\n[!] No devices found!")
            print("\nTroubleshooting:")
            print("  1. Plug in the weather dongle")
            print("  2. On Windows: Check Device Manager for unknown devices")
            print("  3. On Linux: Run 'lsusb' to see connected devices")
            print("  4. On macOS: Run 'system_profiler SPUSBDataType'")
            print("\nKnown device IDs to watch for:")
            for dev in self.KNOWN_DEVICES[:3]:
                print(f"  - {dev['name']} (VID:{dev['vid']:04x} PID:{dev['pid']:04x})")
            return
        
        print(f"\nFound {len(self.found_devices)} device(s):\n")
        
        for i, dev in enumerate(self.found_devices, 1):
            print(f"{i}. {dev.get('name', dev.get('device', dev.get('port', 'Unknown')))} ({dev.get('type')})")
            
            if 'description' in dev:
                print(f"   Description: {dev['description']}")
            
            if 'vid' in dev:
                print(f"   VID: 0x{dev['vid']:04x}, PID: 0x{dev['pid']:04x}")
            
            if 'hwid' in dev:
                print(f"   Hardware ID: {dev['hwid']}")
            
            if 'port' in dev:
                print(f"   Port: {dev['port']}")
            
            print()
    
    def test_device(self, device):
        """Test communication with a discovered device."""
        if device.get('type') != 'pyusb':
            print(f"Cannot test {device.get('name')} - not accessible via pyusb")
            return False
        
        usb_dev = device['usb_device']
        
        try:
            # Try to read device descriptor
            print(f"Testing {device['name']}...")
            print(f"  Manufacturer: {usb.util.get_string(usb_dev, usb_dev.iManufacturer)}")
            print(f"  Product: {usb.util.get_string(usb_dev, usb_dev.iProduct)}")
            print(f"  Serial: {usb.util.get_string(usb_dev, usb_dev.iSerialNumber)}")
            
            # Try to set configuration (may fail if already set)
            try:
                usb_dev.set_configuration()
                print(f"  Device configuration set successfully")
            except:
                print(f"  Device already configured")
            
            return True
        except Exception as e:
            print(f"  Error testing device: {e}")
            return False

def main():
    discovery = WeatherDeviceDiscovery()
    discovery.discover()
    discovery.report()
    
    # Test first device found
    if discovery.found_devices:
        pyusb_devs = [d for d in discovery.found_devices if d.get('type') == 'pyusb']
        if pyusb_devs:
            print("\nTesting device communication...")
            discovery.test_device(pyusb_devs[0])
    
    print("\n" + "="*70)
    print("Next steps:")
    print("  1. Plug in weather dongle if not already connected")
    print("  2. Run this script again to identify the device")
    print("  3. Use the VID:PID from the output to configure the software")
    print("="*70 + "\n")

if __name__ == '__main__':
    main()
