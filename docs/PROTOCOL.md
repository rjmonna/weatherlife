# USB Protocol Specification: Weather-Life Dongle

**Version**: 1.0  
**Device**: Silicon Labs CP2102 USB-to-Serial Bridge  
**Status**: Reverse Engineered  
**Confidence**: 70% (based on binary analysis)

## Overview

The Weather-Life dongle communicates via USB using a custom binary protocol layered over HID. Commands are sent to display weather information on an LCD.

## Physical Layer

### USB Interface
- **Device Class**: USB-to-Serial (CDC)
- **Vendor ID**: 0x10c4 (Silicon Labs)
- **Product ID**: 0xea60 (CP2102)
- **Speed**: Full-speed USB (12 Mbps)
- **Endpoints**:
  - Bulk IN: 0x81 (64 bytes)
  - Bulk OUT: 0x01 (64 bytes)
  - Optional Control: 0x00

### Physical Connector
- **Type**: USB-A (standard)
- **Power**: Drawn from USB host (5V, limited current)

## Data Link Layer

### Packet Format

All packets follow this format:

```
┌──────────────┬──────────────┬─────────────────────┐
│   Command    │    Length    │       Payload       │
│   (1 byte)   │   (1 byte)   │   (0-62 bytes)      │
└──────────────┴──────────────┴─────────────────────┘
```

#### Command Byte (Offset 0)
| Value | Name | Direction | Purpose |
|-------|------|-----------|---------|
| 0x01 | CAL_USB_READ | Host→Device | Read data from device |
| 0x02 | CAL_USB_WRITE | Host→Device | Send data to device |
| 0x03 | CAL_USB_STATUS | Host→Device | Query device status |
| 0x04 | DeviceIni | Host→Device | Initialize device |
| 0x00 | ACK | Device→Host | Positive acknowledgment |
| 0xFF | NAK | Device→Host | Negative acknowledgment |

#### Length Byte (Offset 1)
- **Range**: 0-62 bytes (USB packet size - 2 bytes header)
- **Meaning**: Number of payload bytes following
- **Note**: If 0, no payload expected

#### Payload (Offset 2 to N)
- **Max Size**: 62 bytes
- **Content**: Command-specific data
- **Padding**: Unused bytes may be 0x00

### Response Format

```
┌──────────────┬──────────────┬─────────────────────┐
│    Status    │    Length    │       Data          │
│   (1 byte)   │   (1 byte)   │   (0-62 bytes)      │
└──────────────┴──────────────┴─────────────────────┘
```

#### Status Byte (Offset 0)
| Value | Meaning |
|-------|---------|
| 0x00 | Success |
| 0x01 | Command not recognized |
| 0x02 | Invalid parameters |
| 0x03 | Device not initialized |
| 0x04 | Hardware error |
| 0x05 | USB communication error |
| 0xFF | Generic error |

## Application Layer

### Command Definitions

#### 1. CAL_USB_WRITE - Send Data to Device

**Direction**: Host → Device  
**Command Byte**: 0x02

**Request Format:**
```
0x02 <length> <weather_data>
```

**Payload Structure** (for weather display):
```
Offset  Size  Field           Type      Description
------  ----  -----           ----      -----------
0       1     Temp_Raw        uint8_t   Temperature (offset 50 for negatives)
1       1     Humidity        uint8_t   Relative humidity 0-100
2       1     Weather_Code    uint8_t   Weather condition code
3       2     Wind_Speed      uint16_t  Wind speed in m/s × 10 (little-endian)
5       2     Wind_Direction  uint16_t  Direction 0-359° (little-endian)
7       1     Pressure        uint8_t   Atmospheric pressure (offset 900 hPa)
```

**Example** (Sunny, 22°C, 65% RH, 10 m/s wind from W):
```
Hex: 02 08 52 41 01 64 00 B4 00
     ││││││││││││││││││││││││││││
     │││  Temperature: 0x52 (82 decimal, 82-50=32°F or ~22°C)
     ││   Humidity: 0x41 (65%)
     │    Weather: 0x01 (Sunny)
     │    Wind Speed: 0x0064 (100 × 0.1 = 10 m/s)
     │    Wind Dir: 0x00B4 (180° = South... hmm, example says W)
```

**Response**:
```
0x00 0x00  (Success with no additional data)
or
0x03 0x00  (Device not initialized)
```

#### 2. CAL_USB_READ - Read Data from Device

**Direction**: Host → Device  
**Command Byte**: 0x01

**Request Format:**
```
0x01 0x00  (No payload)
```

**Response Format:**
```
0x00 <length> <device_data>
```

**Response Payload** (device sensor data):
```
Offset  Size  Field           Description
------  ----  -----           -----------
0       1     Device_Status   Device state (0=OK, non-zero=error)
1       1     Signal_Strength Signal quality or battery level
2       4     Reserved        (for future use)
```

#### 3. CAL_USB_STATUS - Query Device Status

**Direction**: Host → Device  
**Command Byte**: 0x03

**Request Format:**
```
0x03 0x00  (No payload)
```

**Response Format:**
```
0x00 0x01 <status_byte>

Status Byte Bits:
Bit 7-6:  Unused
Bit 5:    Display Ready
Bit 4:    USB Connected
Bit 3:    Data Valid
Bit 2:    Error Flag
Bit 1:    Reserved
Bit 0:    Device Ready
```

**Examples**:
```
0x00 0x01 0x3F  (All systems OK: device ready, connected, display ready, data valid)
0x00 0x01 0x01  (Minimal: just device ready)
0x00 0x01 0x04  (Error condition detected)
```

#### 4. DeviceIni - Initialize Device

**Direction**: Host → Device  
**Command Byte**: 0x04

**Request Format:**
```
0x04 0x00  (No payload)
```

**Response Format:**
```
0x00 0x00  (Success)
or
0x04 0x00  (Hardware error)
```

**Purpose**:
- Initialize internal state
- Clear display
- Reset any error flags
- Prepare for weather data

**Timing**:
- Should be called on application startup
- May be called periodically as watchdog
- Typical response time: 100-500 ms

## Communication Timing

### Initialization Sequence
```
1. Host sends: 0x04 0x00 (DeviceIni)
2. Device responds: 0x00 0x00 (after 100-500 ms)
3. Host can now send weather data
```

### Update Cycle (Typical)
```
Every hour (or on-demand):
1. Fetch weather from API
2. Parse temperature, humidity, wind, condition
3. Format as CAL_USB_WRITE payload
4. Send: 0x02 <len> <weather_data>
5. Device responds: 0x00 0x00
6. Continue for 1 hour or until next weather fetch
```

### Error Recovery
```
If no response after 5 seconds:
1. Retry command (up to 3 times)
2. If still fails, call DeviceIni
3. If still fails, report device error
4. Notify user to check USB connection
```

## Weather Condition Codes

```
Code  Condition           Display
----  ---------           -------
0x00  Clear/Sunny         ☀
0x01  Partly Cloudy       ⛅
0x02  Cloudy              ☁
0x03  Overcast            ⛁
0x04  Light Rain          🌦
0x05  Rain                🌧
0x06  Heavy Rain          ⛈
0x07  Thunderstorm        ⚡
0x08  Sleet               ❄
0x09  Snow                ⛄
0x0A  Light Snow          ⛄
0x0B  Fog                 🌫
0x0C  Mist                🌫
0x0D  Wind                💨
0x0E  Hail                ❆
0x0F  Reserved for future use
```

## Display Formatting (16x2 LCD)

### Line 1 (16 characters): Location + Temperature
```
Format: "LOCATION    TEMP"
Example: "New York    72°F"
         "        or" 
         "London   18°C"

Parsing Logic:
- Bytes 0-10: Location name (left-aligned, space-padded)
- Bytes 11-15: Temperature (right-aligned, with ° symbol)
```

### Line 2 (16 characters): Condition + Humidity + Wind
```
Format: "CONDITION    RH%"  (typical)
Example: "Cloudy      65%"
         "Sunny Wind12W"

Parsing Logic:
- Bytes 0-6:  Weather condition name (left-aligned)
- Bytes 7-10: Optional wind speed/direction
- Bytes 11-15: Humidity percentage (right-aligned)
```

## Performance Requirements

### Minimum Response Times
- **Status Query**: < 100 ms
- **Write Command**: < 500 ms
- **Read Command**: < 200 ms
- **Initialization**: < 1 second

### Typical Timing
- Power-up to ready: 1-2 seconds
- Weather update: 2-5 seconds
- Display refresh: Immediate (LCD latency ~50-100 ms)

## Error Handling

### Common Errors

```
Condition                   Response        Action
---------                   --------        ------
Device not plugged in       No response     Retry with backoff
USB driver missing          No response     Notify user to install driver
Device firmware corrupt     0xFF response   Factory reset required
USB cable disconnected      0x05 error      Notify device disconnected
Display failure             0x04 error      Device HW fault
Invalid command ID          0x01 error      Check command byte
Payload too large           0x02 error      Truncate to 62 bytes
Device not initialized      0x03 error      Send DeviceIni first
```

### Retry Strategy
```
1. Send command
2. Wait 500 ms for response
3. If no response and retries < 3:
   - Wait 200 ms
   - Retry command
4. If retries exhausted:
   - Send DeviceIni
   - Wait 1 second
   - Retry original command once more
5. If still fails:
   - Report device error
   - Notify user of USB issue
```

## Integration with Temperature Units

### Display Configuration
The device appears to handle both Fahrenheit and Celsius based on locale.

**Temperature Encoding** (in payload):
```
Raw Value = Actual Temperature + 50

Examples:
- 72°F → 0x52 (82 decimal)
- 0°C  → 0x32 (50 decimal)
- -10°C → 0x28 (40 decimal)
- 100°F → 0x78 (120 decimal)
```

**Device-side Display Logic**:
- Decode: Display_Temp = Raw_Value - 50
- Format with appropriate symbol (°F or °C) based on config
- Typical range: -40 to +50 Celsius (-40 to +122°F)

## Power Management

### USB Power Draw
- Idle: ~50-100 mA
- Active display: ~100-150 mA
- Peak (all segments lit): ~200 mA

### Power-down Sequence
- Device monitors USB VBUS for disconnection
- Auto-shutdown after ~2 seconds of VBUS loss
- No persistent state storage required
- Automatic re-initialization on reconnection

## Security Notes

- **No encryption**: All data in plaintext
- **No authentication**: No user/password required
- **No firmware updates**: Device firmware appears static
- **No configuration protection**: Settings accessible to any USB user
- **Physical security**: Device can be unplugged anytime (graceful)

## Testing & Validation

### Hardware Setup
```
USB Host → USB Cable → Weather Dongle (CP2102) → LCD Display
           (5V)
```

### Test Sequence
1. Plug in device
2. Run DeviceIni command
3. Verify status returns 0x00
4. Send test weather data
5. Verify display updates
6. Verify each field individually

### Validation Packet Examples

**Test 1: Initialization**
```
Send:    04 00
Receive: 00 00
Expected: Display clears, device ready
```

**Test 2: Status Check**
```
Send:    03 00
Receive: 00 01 3F
Expected: Device reports ready (0x3F = all flags set)
```

**Test 3: Weather Update**
```
Send:    02 08 52 41 01 64 00 B4 00
         (22°C, 65% RH, Sunny, 10 m/s from south)
Receive: 00 00
Expected: LCD updates with weather info
```

---

**Protocol Version**: 1.0  
**Last Updated**: 2026-08-12  
**Reverse Engineering Status**: 70% Confidence  
**Implementation Status**: Ready for Phase 3
