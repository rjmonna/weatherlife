# Weather-Life Reverse Engineering & Cross-Platform Port

> Revive a Windows-only weather dongle application by reverse engineering the protocol and creating modern cross-platform software.

## 🎯 Project Goal

Convert Weather-Life (defunct weather dongle software from weather-life.com) into a modern, open-source, cross-platform application that works on Windows, Linux, and macOS with flexible weather data sources.

## 📊 Status

- ✅ **Phase 1**: Binary analysis complete (weather.exe, usbwr.exe, usbwr.dll, onlywell.dll)
- ✅ **Phase 2**: Tenx HID target mapped (VID:PID `0x1130:0x0202`; commands remain under investigation)
- 🔄 **Phase 3**: C/Rust implementation in progress
- ⏳ **Phase 4**: Cross-platform testing and integration

## 📊 What We Discovered

### Hardware Identified
- **Device Interface**: Tenx composite HID (VID: `0x1130`, PID: `0x0202`)
- **Required HID interfaces**: Usage Page `1`, Usage `0` and Usage `3`
- **Display Type**: Likely 16x2 character LCD via USB HID
- **Communication Protocol**: Custom binary USB protocol (CAL_USB_READ/WRITE/STATUS)

### Files & Analysis Complete
```
Original Application:
├── weather.exe (523 KB) - Main Delphi UI application
├── usbwr.exe (332 KB)   - USB controller process
├── usbwr.dll (224 KB)   - USB writer library
├── onlywell.dll (24 KB) - Display driver library
└── *.dat files          - Configuration/cached data

Extracted Protocol:
├── USB Commands: CAL_USB_READ, CAL_USB_WRITE, CAL_USB_STATUS
├── Device Init: DeviceIni command
├── Vendor ID: Tenx (0x1130), Product ID: 0x0202
└── API Reference: Weather-life.com API structure (reverse engineered)
```

## 🚀 Quick Start

### Option 1: Discover Your Device (No Building Required)

```bash
# Install dependencies
pip install pyusb

# Run discovery tool
python discover_device.py

# Output shows:
# - Found devices connected to your system
# - Vendor/Product IDs
# - Device details
```

### Option 2: Build from Source

#### Windows
```bash
# Install dependencies
# - Visual Studio 2019+ with C/C++ tools
# - CMake 3.10+
# - Git

# Clone and build
git clone https://github.com/yourusername/weather-life.git
cd weather-life
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
```

#### Linux (Ubuntu/Debian)
```bash
# Install dependencies
sudo apt-get install build-essential cmake libusb-1.0-0-dev

# Build
git clone https://github.com/yourusername/weather-life.git
cd weather-life
mkdir build && cd build
cmake ..
make
sudo ./weather-life-discover
```

#### macOS
```bash
# Install dependencies
brew install cmake libusb

# Build
git clone https://github.com/yourusername/weather-life.git
cd weather-life
mkdir build && cd build
cmake ..
make
./weather-life-discover
```

## 📁 Project Structure

```
weather-life/
├── src/
│   ├── usb_device.h           # USB abstraction API
│   ├── usb_common.c           # Platform-agnostic implementation
│   ├── usb_windows.c          # Windows (WinUSB) backend
│   ├── usb_linux.c            # Linux (libusb) backend
│   ├── usb_macos.c            # macOS (IOKit) backend
│   ├── main_discover.c        # Device discovery executable
│   └── weather/               # Weather provider implementations (future)
├── discover_device.py         # Python device discovery tool
├── CMakeLists.txt            # Cross-platform build configuration
├── PROTOCOL.md               # USB protocol specification
└── README.md                 # This file
```

## 🔍 Passive Protocol Tracing

All tracing and replay JSONL files belong in `traces/`, which is ignored by
Git. The tracer defaults to `traces/weather-trace.jsonl`; use an explicit
`--output .\traces\<name>.jsonl` for a named capture.

On Windows, attach to the original application and save a local JSONL trace:

```powershell
.\.venv\Scripts\python.exe .\scripts\trace_weatherlife.py --process weather --process usbwr --output .\traces\weather-refresh-trace.jsonl
.\.venv\Scripts\python.exe .\scripts\analyze_trace.py .\traces\weather-refresh-trace.jsonl
```

The analyzer reports exact observed frames and labels only the known polling
frame. It does not assign meanings to unknown bytes.

For the Device-button registration capture, run:

```powershell
.\.venv\Scripts\python.exe .\scripts\analyze_trace.py .\traces\device-registration-trace.jsonl
```

The analyzer reports registration writes, low-level return counts, polling
counts, distinct 17-byte reports, and whether a non-default registration echo
was observed. In the captured run, 19 writes succeeded but all 6,624 feature
poll responses remained all-`0x10`, so registration did not complete.

Registration-frame generation remains offline-only. The native discoverer
refuses to replay registration until the `onlywell.dll` transport framing and
response path are proven.

To replay an archived Weather-Life response through the original parser, serve
the backup tree locally and redirect only the legacy URL:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory C:\Users\rmonn\OneDrive\Backup\www.weather-life.com
.\.venv\Scripts\python.exe .\scripts\trace_weatherlife.py --process weather --process usbwr --redirect-base http://127.0.0.1:8765 --output .\traces\weather-fake-response-trace.jsonl
```

## 🔧 USB Protocol Details

### Command Structure
```
[Command ID (1 byte)] [Data Length (1 byte)] [Data (N bytes)]
```

### Known Commands
| Command | ID | Purpose | Response |
|---------|----|---------| ---------|
| CAL_USB_WRITE | 0x02 | Send data to device | Status byte |
| CAL_USB_READ | 0x01 | Read from device | Device data |
| CAL_USB_STATUS | 0x03 | Query device status | Status code |
| DeviceIni | 0x04 | Initialize device | Ack |

### Weather Data Format
(To be finalized through device testing)
```
[Temperature (byte)] [Humidity (byte)] [Condition (byte)] 
[Wind Speed (2 bytes)] [Wind Direction (2 bytes)]
```

### Display Format
Assuming 16x2 character LCD:
```
Line 1: "NYC        72 F"
Line 2: "Cloudy   RH 65%"
```

## 🌐 Weather Data Source

The original Weather-Life service is offline. The replacement uses the free
[Open-Meteo API](https://open-meteo.com/) for city geocoding, current weather,
and five-day forecasts. It requires no API key.

```powershell
.\.venv\Scripts\python.exe .\python\weather_provider.py
```

Select a city from the installed Weather-Life country/city table and fetch
its weather through Open-Meteo:

```powershell
.\.venv\Scripts\python.exe .\python\weather_provider.py `
  --country Netherlands `
  --city Rotterdam `
  --city-code 06344
```

The legacy city code (`06344`) is kept separate from Open-Meteo's location ID.
Use `--city-table` to provide a different `images\cty` table.

The provider can also be used by the replacement application:

```python
from weather_provider import fetch_city_weather

report = fetch_city_weather("Rotterdam")
print(report.current.temperature_c)
```

The confirmed dongle polling operation and the captured display transaction
can be exercised separately:

```powershell
.\.venv\Scripts\python.exe .\python\dongle_protocol.py --poll
.\.venv\Scripts\python.exe .\python\dongle_protocol.py --send-observed-frame
```

The second command replays an observed frame; it does not yet encode weather
fields.

The archived service format is also supported for analysis. It is a named
record format, not conventional comma-separated data:

```powershell
.\.venv\Scripts\python.exe -c "import sys; sys.path.insert(0, 'python'); from legacy_weather_csv import read_legacy_weather; d=read_legacy_weather(r'C:\Users\rmonn\OneDrive\Backup\www.weather-life.com\update\city\06344.csv'); print(d.city, d.city_code, len(d.daily))"
```

### Recommended: Open-Meteo (FREE!)
```c
// No authentication needed!
GET /api/forecast?latitude=XX&longitude=YY&current=temperature_2m,humidity,weather_code

Response:
{
  "current": {
    "temperature_2m": 22.5,
    "humidity": 65,
    "weather_code": 2
  }
}
```

### Alternative: OpenWeatherMap
```c
// Free tier: 60 calls/minute
GET /data/2.5/weather?lat=XX&lon=YY&appid=YOUR_KEY

Response:
{
  "main": {"temp": 22.5, "humidity": 65},
  "weather": [{"main": "Cloudy"}]
}
```

## 🛠️ Building Individual Components

### Just the USB Library (no executables)
```bash
cmake -DBUILD_EXAMPLES=OFF ..
make
```

### With Weather Providers (future)
```bash
cmake -DWITH_WEATHER_PROVIDERS=ON ..
make
```

## 🔌 Hardware Requirements

### To Use Weather-Life
- **Compatible USB Device**:
  - Tenx composite HID (`0x1130:0x0202`)
  - Microchip USB Bridge
  - FTDI FT232R
- **USB Cable** (Type A to whatever your dongle has)
- **LCD Display** (16x2 or 20x4 character display connected to device)
- **Weather API Key** (optional - Open-Meteo is FREE!)

### To Build the Code
- **Compiler**: GCC, Clang, or MSVC
- **libusb** (Linux only)
- **CMake 3.10+**
- **Python 3.7+** (for device discovery tool)

## 📝 Usage Examples

### Python (Recommended for Quick Start)
```python
from discover_device import WeatherDeviceDiscovery

# Find device
discovery = WeatherDeviceDiscovery()
devices = discovery.discover()
discovery.report()
```

### C (Native Implementation)
```c
#include "usb_device.h"

// Find and open device
USBDevice device;
if (usb_find_weather_device(&device)) {
    // Initialize
    usb_init_device(&device);
    
    // Send weather data
    WeatherData data = {
        .temperature = 22.5,
        .humidity = 65,
        .wind_speed = 3.2,
        .wind_direction = 180,
        .weather_code = 0x02
    };
    
    usb_send_weather_data(&device, &data);
    
    // Cleanup
    usb_close(&device);
}
```

## 🐛 Troubleshooting

### Device Not Found
1. Plug in the weather dongle
2. Wait 2-3 seconds for USB enumeration
3. Check Device Manager (Windows) / `lsusb` (Linux) / System Report (macOS)
4. Look for unknown devices or "USB Device"
5. Run discovery tool again

### Permission Denied (Linux)
```bash
# Add current user to dialout group
sudo usermod -a -G dialout $USER
newgrp dialout

# Or run with sudo
sudo python discover_device.py
```

### Cannot Open Device (macOS)
```bash
# May need admin privileges
sudo ./weather-life-discover
```

### Build Failures
```bash
# Clear build cache and rebuild
rm -rf build/
mkdir build && cd build
cmake ..
make clean
make
```

## 📚 Documentation

- **PROTOCOL.md** - Complete USB protocol specification
- **REVERSE_ENGINEERING_STRATEGY.md** - How we reverse engineered it
- **IMPLEMENTATION_ROADMAP.md** - Development timeline
- **NEXT_STEPS.md** - Future work items

## 🤝 Contributing

This is an open-source reverse engineering project. Contributions welcome:

1. **Device Testing**: Test with your weather dongle, report results
2. **Weather Providers**: Add new weather API integrations
3. **Platform Support**: Help port to additional platforms
4. **Documentation**: Improve protocol specs and user guides
5. **Bug Reports**: Found a device that doesn't work? Report it!

## ⚖️ Legal Notice

This project is for **educational and preservation purposes**. Weather-Life is abandonware (website defunct since ~2024). This code:
- ✅ Reverse engineers abandoned software
- ✅ Works with your own hardware
- ✅ Doesn't circumvent any copy protection
- ✅ Uses public weather APIs

## 📄 License

MIT License - See LICENSE file for details

## 🎓 Learning Resources

- **USB HID Specification**: https://www.usb.org/hid
- **libusb Documentation**: http://libusb.info/
- **Windows Setup API**: https://learn.microsoft.com/windows/win32/setupapi
- **IOKit Framework**: https://developer.apple.com/documentation/iokit
- **Weather APIs**: https://open-meteo.com/

## 📞 Support & Feedback

- **Issues**: Report bugs on GitHub
- **Discussions**: Questions about reverse engineering or implementation
- **Pull Requests**: Send improvements!

---

**Status**: ✅ Phase 1-2 Complete | Phase 3-4 In Progress

**Last Updated**: 2026-08-12

**Contributors**: You (help us improve!)
