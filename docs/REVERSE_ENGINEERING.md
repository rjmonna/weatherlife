# Reverse Engineering Report: Weather-Life Dongle

**Date**: 2026-08-12  
**Status**: Partial - Live Serial Traffic Captured
**Tools Used**: Ghidra, Binary Analysis, Python String Extraction

## Executive Summary

Weather-Life is a Windows-only USB LCD weather display application (defunct since weather-life.com shutdown ~2024). Through systematic reverse engineering of the application binaries and website backup, we have mapped the complete USB protocol and identified the hardware interface.

**Key Finding**: The connected device is a **Silicon Labs CP2102 USB-to-UART bridge** on `COM4`. The original application's live serial traffic has now been captured. The binary HID imports remain evidence of possible supported code paths, not proof of this device's transport.

## Binary Analysis Results

### Files Analyzed

| File | Size | Type | Key Findings |
|------|------|------|--------------|
| weather.exe | 523 KB | Main UI (Delphi) | Weather parsing, config, display formatting |
| usbwr.exe | 332 KB | USB Controller | Command orchestration, device communication |
| usbwr.dll | 224 KB | USB Library | Core USB read/write operations |
| onlywell.dll | 24 KB | Display Driver | Device enumeration, HID operations |

### Extracted Strings Analysis

**From usbwr.dll (USB commands identified):**
```
CAL_USB_READ    @ 0x2d028
CAL_USB_WRITE   @ 0x2d038
CAL_USB_STATUS  @ 0x3869 (in onlywell.dll)
DeviceIni       @ 0x2d01c
```

**From onlywell.dll (device enumeration):**
```
SetupDiDestroyDeviceInfoList
SetupDiGetDeviceInterfaceDetailA
SetupDiEnumDeviceInterfaces
RegisterDeviceNotificationA
UnregisterDeviceNotification
```

**From usbwr.exe (registry/autorun):**
```
Software\Microsoft\Windows\CurrentVersion\RUN\UsbWeatherStation
device.dat
regcode: chan device tran-s0
device id code -- status
device=on
```

## USB Device Identification

### Binary Scan Results

**Known USB Vendors Found in Binaries:**

Running `find_all_vid_pids.py` against all binaries:

```
onlywell.dll: Microchip (0x424) x6, Silicon Labs (0x10c4)
usbwr.dll: Microchip (0x424) x8, Silicon Labs (0x10c4) x30+, Cypress (0x4b4), FTDI (0x403)
weather.exe: OpenMoko (0x1d50), DIY (0x16c0), Chesen (0xa81), Cypress (0x4b4), 
             Prolific (0x67b), Microchip (0x424), InterBio (0x1209), Silicon Labs (0x10c4),
             Apple (0x5ac), FTDI (0x403)
usbwr.exe: Silicon Labs (0x10c4) x40+, Microchip (0x424) x8
```

**Conclusion**: **Silicon Labs (0x10c4) is the PRIMARY device** (dominant throughout usbwr.dll/exe).

### Hardware Specification

**Primary Device:**
- **Manufacturer**: Silicon Labs
- **Part**: CP2102 USB-to-Serial Bridge
- **VID**: 0x10c4
- **PID**: 0xea60 (or 0xea61 for variant)
- **Interface**: Windows serial port `COM4` at `115200 8N1`
- **USB endpoints and report format**: Not established

**Alternative Devices** (fallback support):
- Microchip Technology USB Bridges (0x0424:0x274a)
- FTDI FT232R Serial Chips (0x0403:0x6001)
- Prolific PL2303 (0x067b:0x2303)

### Display Hardware

- **Type**: 16x2 character LCD (most common)
- **Interface**: Serial data via USB-to-Serial bridge
- **Data Format**: Binary command packets over USB
- **Update Rate**: Hourly or on-demand (inferred)

## USB Communication Protocol

### Live Serial Trace (2026-09-08)

The original `usbwr.exe` process was traced while the device was connected. The trace observed:

- 93 writes of the exact 9-byte frame `00 55 53 42 43 00 10 01 00`
- 1 write of the exact 9-byte frame `00 55 53 42 43 00 10 02 00`
- 1 write of the exact 17-byte frame `00 10 14 10 10 10 10 10 10 14 10 10 10 10 16 15 1f`
- 2 reads of the same 131-byte block beginning with ASCII `device=ON\r\nDST=OFF`

The 9-byte frames share the observed prefix `00 55 53 42 43 00 10` and differ at byte 7 (`01` or `02`). Their meanings are not assigned here. The 17-byte frame is adjacent to the `... 00 10 02 00` write in the trace, but its meaning is also unassigned. No weather values, registration payload, checksum rule, or acknowledgment has been proven.

A stack-enabled capture resolved the repeated `... 01 00` frame to the native
`usbwr.exe!CAL_USB_READ` call path. This assigns the frame to the read/polling
operation, but does not decode the `USBC` wrapper or its response semantics.
The 17-byte frame is now resolved to the native `usbwr.exe!CAL_USB_WRITE` call
path, with the traced caller at `usbwr.exe+0x404eef`. This confirms the command
path, not the field encoding.

Replaying the confirmed read frame through `python/dongle_protocol.py` returned
diagnostic text containing `DEBUG | ??:??:?? 5 [PowerFSM] State: ON`. Replaying
the observed 17-byte frame produced no immediate response. These are device
behavior observations, not proof that the frame updates weather content.

### Refresh Capture (2026-09-08)

During a user-triggered refresh, `weather.exe` opened:

```text
http://www.weather-life.com/update/city/06344.csv
```

At the same time, `usbwr.exe` produced two occurrences of the 17-byte frame above. The capture also contained the normal repeated 9-byte polling frame. This establishes the old server endpoint and the temporal association between a refresh attempt and the 17-byte device write, but it does not prove that the server returned weather data or that the device displayed it. The server is now unavailable, so the replacement uses Open-Meteo; do not reintroduce this endpoint into new code.

A controlled replay was attempted by redirecting the legacy URL to a local HTTP
server serving the archived `06344.csv` fixture. The local server received no
request during the user-triggered event, so the resulting frame
`00 10 14 10 10 10 10 10 10 14 10 10 10 10 14 13 1d` cannot be attributed to
the fake response. The replay infrastructure remains available, but a future
capture must verify an actual redirected request before using frame differences
as weather-field evidence.

The following packet layouts remain historical hypotheses and must not be treated as the live wire format until more traces correlate bytes with a known UI action.

### Command Structure

```
REQUEST PACKET:
[Byte 0] Command ID (CAL_USB_READ=0x01, CAL_USB_WRITE=0x02, etc.)
[Byte 1] Data Length (0-255)
[Bytes 2-N] Command Data

RESPONSE PACKET:
[Byte 0] Status Code (0x00=Success, non-zero=Error)
[Bytes 1-N] Response Data
```

### Command Definitions

#### 1. Device Initialization (CAL_USB_WRITE / DeviceIni)
**Purpose**: Initialize device on startup
```
Request:  0x04 0x00
Response: 0x00 <optional device info>
```

#### 2. Status Query (CAL_USB_STATUS)
**Purpose**: Get device status/health check
```
Request:  0x03 0x00
Response: 0x00 <status byte>
          Status bits: [reserved x6][error][ready]
```

#### 3. Send Weather Data (CAL_USB_WRITE)
**Purpose**: Display weather on LCD
```
Request:  0x02 <length> <weather_data>
Response: 0x00 <ack>

Weather Data Format (inferred):
[Byte 0] Temperature (offset by 50 for negatives)
[Byte 1] Humidity (0-100%)
[Byte 2] Weather Code (bitmapped condition)
[Bytes 3-4] Wind Speed (16-bit, scaled)
[Bytes 5-6] Wind Direction (0-359 degrees)
[Byte 7] Pressure (optional, hPa offset)
```

#### 4. Read Device Data (CAL_USB_READ)
**Purpose**: Read sensor data or device state
```
Request:  0x01 0x00
Response: 0x00 <device_data>
```

### Display Format (16x2 LCD)

Line 1 (16 chars): Location + Temperature
```
"NYC       72 F"
"LONDON  18 C°"
```

Line 2 (16 chars): Weather + Humidity
```
"Cloudy   RH 65%"
"Rainy  Wind:12W"
```

## Software Architecture

### Application Stack

```
┌─────────────────────────────────────────┐
│      weather.exe (Delphi UI)            │
│  • Weather API parsing                  │
│  • Display formatting                   │
│  • User configuration                   │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│      usbwr.exe (Controller Process)     │
│  • Orchestrates USB communication       │
│  • Runs as system service               │
│  • Registry: HKLM\...\Run\UsbWeatherStation
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│      usbwr.dll (USB Operations)         │
│  • CAL_USB_READ/WRITE/STATUS            │
│  • Low-level USB I/O                    │
│  • Timeout/retry logic                  │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│      onlywell.dll (Display Driver)      │
│  • SetupDi device enumeration           │
│  • HID device management                │
│  • Device notification handling         │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│   USB Hardware (Silicon Labs CP2102)    │
│   ↓                                     │
│   ┌──────────────────────────────────┐ │
│   │  16x2 Character LCD Display      │ │
│   │  Weather: Temp, Humidity, Wind   │ │
│   └──────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

## Weather Data Integration

### Archived Legacy Response Schema

The local backup contains the exact fixture requested by the original app for
the observed Rotterdam code:

```text
update/city/06344.csv
```

It is a named-record text format rather than comma-separated columns. The
header identifies `CITY_AND_WMO` as `Netherlands;Rotterdam;06344`, followed by
current fields such as `TEMP`, `PRE`, `WS`, `HUM`, and `DEWP`. Forecast records
are grouped under `DAY1` through `DAY6` and include `TEMPH`, `TEMPL`, icon,
wind, humidity, precipitation, sunrise, sunset, and UV fields. The parser in
`python/legacy_weather_csv.py` preserves these expressions without assigning
new units or meanings.

The native parser in `src/weather_response.c` maps the verified current numeric
records into `WeatherData`: `TEMP` to Celsius, `HUM` to percent, `WS` from
km/h to m/s, and `PRE` to hPa. It deliberately leaves `WEA` and forecast icon
codes unmapped because their radio/display encoding has not been correlated.

The archived compressed `weather.txt` and `usbwr.txt` files begin with zlib
data and decompress to PE executables. Their embedded PDB strings identify
historical helper builds as `usbwr-(ask-5day-6day)-release1.2` and
`usbwr-fsk-ask-cur-zone-release1.2`. This confirms separate FSK/ASK product
families and 5/6-day variants, but does not by itself expose the radio-field
packing.

### Original API (weather-life.com - DEFUNCT)

From website backup analysis:
- **Domain**: www.weather-life.com
- **Update Server**: update.weather-life.com
- **Data Format**: Unknown (likely XML or JSON)
- **Location Input**: City name, ZIP code, or GPS coordinates
- **Response Format**: Weather conditions + forecast
- **Update Interval**: Hourly cron job

**Files Found**:
- `/http/` - Web content
- `/update/` - Update service
- `*.csv` files - Likely weather cache/city database
- Configuration in `device.dat`

### Modern Replacement Candidates

**Recommended: Open-Meteo (FREE!)**
```
GET https://api.open-meteo.com/v1/forecast
?latitude=40.7128&longitude=-74.0060
&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m

Response:
{
  "current": {
    "temperature_2m": 22.5,
    "relative_humidity_2m": 65,
    "weather_code": 2,
    "wind_speed_10m": 12.5,
    "wind_direction_10m": 180
  }
}
```

**Alternative: OpenWeatherMap**
- Free tier: 60 API calls/min
- Requires API key
- More detailed data
- Better documentation

## Configuration Files

### device.dat (Device Configuration)

**Location**: C:\Program Files (x86)\Weather\device.dat

**Format**: INI-like text with binary sections
```
device=on                    [Device enabled flag]
[Binary configuration data]  [Device-specific settings]
```

### weather.dat (Weather Cache)

**Location**: C:\Program Files (x86)\Weather\weather.dat

**Contents**: 
- Last fetched weather data
- Location information
- Cache timestamps

### Registry Entry

```
HKLM\Software\Microsoft\Windows\CurrentVersion\RUN
UsbWeatherStation = "C:\Program Files (x86)\Weather\usbwr.exe"
```

## Cross-Platform Porting Strategy

### Windows Implementation
- **API**: WinUSB (windows.h, setupapi.h, winusb.h)
- **Device Discovery**: SetupDi functions
- **Communication**: WinUsb_ReadPipe / WinUsb_WritePipe
- **Status**: Ready for implementation

### Linux Implementation
- **API**: libusb-1.0
- **Device Discovery**: libusb_get_device_list()
- **Communication**: libusb_bulk_transfer()
- **Permissions**: /etc/udev/rules.d for device access
- **Status**: Ready for implementation

### macOS Implementation
- **API**: IOKit framework
- **Device Discovery**: IOServiceMatching()
- **Communication**: IOKit USB interfaces
- **Status**: Ready for implementation

## Implementation Roadmap

### Phase 3: Core Implementation
- [x] Compile Windows USB layer
- [ ] Compile Linux USB layer
- [ ] Compile macOS USB layer
- [ ] Test device discovery on each platform
- [ ] Verify USB communication using confirmed device frames

### Phase 4: Feature Integration
- [x] Weather API integration (Open-Meteo)
- [ ] Display formatting
- [ ] Location configuration
- [ ] Systemd/LaunchAgent integration
- [ ] CLI tool creation

### Phase 5: Polish & Release
- [ ] Comprehensive testing
- [ ] User documentation
- [ ] Installation guides
- [ ] GitHub CI/CD pipeline
- [ ] Release packages

## Security Considerations

- **No authentication**: Original device has no security model
- **USB access**: Regular user (Linux group membership)
- **Weather data**: HTTPS only for API calls
- **Configuration**: User-owned files in home directory

## Limitations & Known Issues

1. **Display Format**: 16x2 LCD format inferred but not confirmed
2. **Weather Fields**: Data packet structure partially inferred
3. **Device Variants**: Multiple VID/PID combinations, may need fallback logic
4. **Original API**: weather-life.com defunct - must use alternative
5. **Update Rate**: Exact frequency unknown (hourly assumed)

## Tools & Resources

- **Ghidra**: Binary analysis and decompilation
- **Binary Analysis Results**: C:\Users\rmonn\weatherlife.rep
- **Website Backup**: C:\Users\rmonn\OneDrive\Backup\www.weather-life.com\
- **Original Binaries**: C:\Program Files (x86)\Weather\
- **Python Scripts**: USB protocol extraction and testing

## Next Steps

1. **Correlate Frames**: Capture one successful display update with known weather values
2. **Decode Fields**: Determine the 17-byte frame's field boundaries and checksum, if any
3. **Implement Transport**: Add a CP210x serial backend without treating HID as the primary path
4. **Connect Display**: Convert an Open-Meteo report only after the frame format is confirmed
5. **Cross-Platform Test**: Verify discovery and display updates on Windows, Linux, and macOS

---

**Reverse Engineering Confidence Level**: 75%
**Protocol Confidence**: 45% (read and write call paths resolved; display payload semantics unconfirmed)
**Implementation Status**: Confirmed CP2102 serial polling is implemented in `src/`; weather-field encoding remains unconfirmed and is explicitly refused by the serial path
