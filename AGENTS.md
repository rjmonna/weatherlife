# Weather-Life Agent Instructions

This file is the canonical project guidance for coding agents. The legacy `.github/.copilot-instructions.md` points here for compatibility.

## Project

Weather-Life is a reverse-engineered cross-platform replacement for the discontinued Weather-Life Windows application. The repository contains C USB code, Python discovery tools, protocol notes, and tracing utilities.

Primary areas:

- `src/`: C USB abstraction and platform backends
- `python/`: device discovery utilities
- `scripts/`: reverse-engineering and tracing utilities
- `scripts/analyze_trace.py`: summarize captured JSONL traffic without decoding unknown fields
- `docs/`: protocol and analysis notes
- `meson.build`: primary build definition
- `CMakeLists.txt`: alternative build definition
- `python/weather_provider.py`: free Open-Meteo weather and geocoding client
- `python/dongle_protocol.py`: observed CP2102 serial operations
- `python/legacy_weather_csv.py`: parser for archived legacy city responses
- `src/weather_response.c`: native parser for verified legacy current fields

## Weather Service Replacement

`weather-life.com` is offline and must not be used as a runtime dependency. The
replacement weather source is Open-Meteo:

- Free and does not require an API key
- Geocodes city names through the Open-Meteo geocoding API
- Provides current conditions and five forecast days
- Python entry point: `python/weather_provider.py`

Validate the provider with:

```powershell
.\\.venv\\Scripts\\python.exe .\\python\\weather_provider.py
```

Keep the weather service integration separate from the dongle protocol. A
successful Open-Meteo response does not prove that a display packet is valid.

The archived backup at `C:\\Users\\rmonn\\OneDrive\\Backup\\www.weather-life.com`
contains the original named-record city responses under `update\\city\\`. The
Rotterdam fixture is `06344.csv`; parse it with `python/legacy_weather_csv.py`.
Archived compressed `weather.txt` and `usbwr.txt` files contain historical PE
payloads. They identify FSK/ASK-era helper builds, but are analysis fixtures,
not runtime dependencies.

The native response parser maps the verified legacy records `TEMP`, `HUM`, `WS`,
and `PRE` into `WeatherData`. It leaves `WEA` and forecast icon values unset
until their device encoding is proven.

## Current Hardware Findings

The connected test dongle was confirmed on Windows as:

- Silicon Labs CP2102 USB to UART Bridge Controller
- VID `0x10C4`, PID `0xEA60`
- Windows device instance `USB\\VID_10C4&PID_EA60\\0001`
- Serial port `COM4`
- Serial number `0001`

The original application is installed at `C:\\Program Files (x86)\\Weather\\` and runs as two processes:

- `weather.exe`: UI and registration flow
- `usbwr.exe`: USB helper and likely owner of device I/O

Do not describe the CP2102 as a confirmed HID device. The Windows Plug and Play result confirms UART enumeration. Earlier HID findings from binary imports remain evidence about possible code paths or supported variants, not proof of this connected device's transport.

## Reverse-Engineering Rules

- Treat command names, vendor IDs, packet layouts, and display formats as hypotheses until confirmed by call-site analysis or captured traffic.
- When reverse engineering produces a confirmed behavior, update the relevant C sources in `src/` in the same change. Keep unconfirmed protocol fields behind an explicit refusal or experimental API rather than silently implementing guesses.
- Do not send guessed commands to the physical device during discovery. Prefer passive observation.
- The existing native Windows backend is currently HID-only and therefore cannot open the confirmed CP2102 COM port. Do not use `build/weather-life-discover.exe` as proof that the dongle is absent; it reports “not found” because it searches HID interfaces.
- The original app's registration notifications were:
  - `Device Register...`
  - `Can not connect to server`
  - `Device Register End`
  These indicate a failed server-side registration attempt, but do not establish that USB registration succeeded.
- A direct COM4 test was performed at `115200 8N1`. It wrote `04 00`, then `02 07 48 41 02 20 00 B4 00`. The device returned readable diagnostic text beginning with `INFO` and including `Booted, wake cause 0`.
- The implemented `CAL_USB_READ` replay returned diagnostic text including `DEBUG | ??:??:?? 5 [PowerFSM] State: ON`.
- That response does not prove that the weather packet was accepted or displayed. No registration packet or weather-display acknowledgment was captured.
- A user-triggered refresh caused `weather.exe` to open `http://www.weather-life.com/update/city/06344.csv`; `usbwr.exe` emitted the distinct 17-byte frame `00 10 14 10 10 10 10 10 10 14 10 10 10 10 16 15 1f` twice. This is a temporal correlation, not a decoded packet format.
- A stack-enabled trace resolved the repeated 9-byte frame `00 55 53 42 43 00 10 01 00` to the native `usbwr.exe!CAL_USB_READ` call path. Keep the `... 02 00` frame and 17-byte frame unclassified until their call sites are captured.
- A later stack trace resolved the 17-byte frame `00 10 14 10 10 10 10 10 10 14 10 10 10 10 14 13 1d` to `usbwr.exe!CAL_USB_WRITE`, with caller `usbwr.exe+0x404eef`. This confirms the write path; its weather-field encoding remains unknown.
- An earlier Frida attempt reported `TypeError: not a function` while installing a hook. The tracer now uses a compatible export lookup and attaches without that error; keep it passive while collecting original application traffic.

## Discovery and Tracing

Use the project virtual environment on Windows:

```powershell
.\\.venv\\Scripts\\python.exe .\\python\\discover_device.py
```

The discovery script requires `pyserial` and `pyusb`:

```powershell
.\\.venv\\Scripts\\python.exe -m pip install pyserial pyusb
```

The discovery output should identify `COM4` and `USB VID:PID=10C4:EA60` for the connected dongle. The registry scan is supplementary and may list many unrelated USB devices.

The observed serial operations can be exercised with:

```powershell
.\\.venv\\Scripts\\python.exe .\\python\\dongle_protocol.py --poll
.\\.venv\\Scripts\\python.exe .\\python\\dongle_protocol.py --send-observed-frame
```

`--poll` sends the call-site-confirmed `CAL_USB_READ` frame. The observed
17-byte display frame can be replayed, but it is not a weather encoder and must
not be described as one until its fields are correlated with known data.

The passive tracer can attach to both original processes:

```powershell
.\\.venv\\Scripts\\python.exe .\\scripts\\trace_weatherlife.py --process weather --process usbwr
```

For a controlled parser replay, serve an archived backup and add
`--redirect-base http://127.0.0.1:8765`. The tracer preserves the original URL
and records the redirected URL, HTTP reads, call stacks, and resulting device
writes. Use the original application's Soft Update action while this capture
is active; do not substitute guessed device packets for the refresh action.

It is intended to observe:

- WinINet registration URLs, headers, and request bodies
- `WriteFile` calls containing outbound serial bytes
- `ReadFile` calls containing inbound serial bytes
- HID feature-report calls if the original software uses them

Validate tracer syntax before running it:

```powershell
.\\.venv\\Scripts\\python.exe -m py_compile .\\scripts\\trace_weatherlife.py
```

Summarize a captured trace with:

```powershell
.\\.venv\\Scripts\\python.exe .\\scripts\\analyze_trace.py .\\weather-refresh-trace.jsonl
```

Do not log or commit credentials, registration codes, personal location data, or other secrets captured from the original application.

## Protocol Confidence

The following labels were recovered from binary analysis but remain inferred until traffic confirms their framing and values:

```c
#define CAL_USB_READ    0x01
#define CAL_USB_WRITE   0x02
#define CAL_USB_STATUS  0x03
#define CMD_DEVICE_INIT 0x04
```

The previously documented two-byte command header and weather payload are not confirmed. Keep `docs/PROTOCOL.md` and `docs/REVERSE_ENGINEERING.md` explicit about confidence levels and update them only when evidence supports a claim.

## Build and Test

Meson is the preferred build system:

```powershell
meson setup builddir
meson compile -C builddir
```

CMake is supported as an alternative:

```powershell
cmake -S . -B build
cmake --build build
```

For Python changes:

```powershell
.\\.venv\\Scripts\\python.exe -m py_compile .\\python\\discover_device.py .\\scripts\\trace_weatherlife.py
```

Run the narrowest relevant validation after every edit. Do not modify unrelated user changes or commit changes unless explicitly requested.

## Code Style

- C99, four-space indentation, `snake_case` functions and variables, uppercase macros
- Python PEP 8, public-function docstrings, type hints where practical
- Prefer small focused changes and existing project APIs
- Avoid speculative protocol implementations and unrelated refactors
- Keep documentation synchronized with verified behavior

## Useful Analysis Sources

- Original binaries: `C:\\Program Files (x86)\\Weather\\`
- Ghidra project: `C:\\Users\\rmonn\\weatherlife.rep`
- Protocol notes: `docs/PROTOCOL.md`
- Analysis report: `docs/REVERSE_ENGINEERING.md`
- Discovery tool: `python/discover_device.py`
- Passive tracer: `scripts/trace_weatherlife.py`

Last updated: 2026-09-08
