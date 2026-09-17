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
- `python/dongle_protocol.py`: offline registration-frame generator
- `python/legacy_weather_csv.py`: parser for archived legacy city responses
- `src/weather_response.c`: native parser for verified legacy current fields
- `src/device_registration.c`: confirmed usbwr.dll registration-handshake algorithm (id generation, nibble packing, CRC-8)
- `src/bit_packer.c`: confirmed MSB-first variable-width bit packer used by the native weather-payload serializer

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

The confirmed present device on Windows is the Tenx composite HID device:

- VID `0x1130`, PID `0x0202`
- Bus description: `Tenx Non standard Device`
- Present child interfaces: `MI_00` and `MI_01`, both Status `OK`

The original application is installed at `C:\\Program Files (x86)\\Weather\\` and runs as two processes:

- `weather.exe`: UI and registration flow
- `usbwr.exe`: USB helper and likely owner of device I/O

The original application's traced HID operations target this Tenx device. The
handle-correlated trace found no serial transport and the project does not
implement one.

## Reverse-Engineering Rules

- Treat command names, vendor IDs, packet layouts, and display formats as hypotheses until confirmed by call-site analysis or captured traffic.
- When reverse engineering produces a confirmed behavior, update the relevant C sources in `src/` in the same change. Keep unconfirmed protocol fields behind an explicit refusal or experimental API rather than silently implementing guesses.
- Do not send guessed commands to the physical device during discovery. Prefer passive observation.
- The native Windows backend is HID-only and mirrors `onlywell.dll`: it matches VID `0x1130`/PID `0x0202`, requires HID Usage Page `1` and both Usage `0` and Usage `3`, then uses the Usage `0` interface for the device handle.
- The original app's registration notifications were:
  - `Device Register...`
  - `Can not connect to server`
  - `Device Register End`
  These indicate a failed server-side registration attempt, but do not establish that USB registration succeeded.
- A user-triggered refresh caused `weather.exe` to open `http://www.weather-life.com/update/city/06344.csv`; `usbwr.exe` emitted the distinct 17-byte frame `00 10 14 10 10 10 10 10 10 14 10 10 10 10 16 15 1f` twice. This is a temporal correlation, not a decoded packet format.
- A stack-enabled trace resolved the repeated 9-byte frame `00 55 53 42 43 00 10 01 00` to the native `usbwr.exe!CAL_USB_READ` call path. Keep the `... 02 00` frame unclassified until its call site is captured.
- A later stack trace resolved the 17-byte frame to `usbwr.exe!CAL_USB_WRITE`, caller `usbwr.exe+0x404eef`.
- **2026-09-17, confirmed via GhidraMCP decompilation of `usbwr.dll!usbdeviceread`:** the 17-byte frame is a **device registration handshake**, not weather/display content. `usbwr.dll` exports only this one function; it resolves `CAL_USB_WRITE`/`CAL_USB_READ`/`DeviceIni` from `onlywell.dll` via `GetProcAddress`, generates a time/rand-seeded ASCII id, nibble-packs it (`0x10 | nibble` per byte), and appends a CRC-8 (poly 0x31, MSB-first) over bytes[0..13] written as two nibbles at bytes[14..15]. This algorithm reproduces both historical captures' CRC bytes exactly. Byte[13] does not match `usbwr.dll`'s own logic (should stay `0x10`) and is presumed to be `onlywell.dll`'s responsibility (not yet decompiled). See `docs/REVERSE_ENGINEERING.md` for the full derivation and `src/device_registration.c`/`python/dongle_protocol.py` (`build_registration_frame`) for the implementation.
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

The discovery output should identify the Tenx HID device as `USB VID:PID=1130:0202`. The registry scan is supplementary and may list many unrelated USB devices.

The registration-frame utility is offline only:

```powershell
.\\.venv\\Scripts\\python.exe .\\python\\dongle_protocol.py --build-registration-frame
```

It generates the confirmed `usbwr.dll`-side registration buffer but does not
open hardware or send frames. The native C discoverer deliberately refuses to
replay registration until the `onlywell.dll` transport transformation and
response path are proven.

The passive tracer can attach to both original processes:

```powershell
.\\.venv\\Scripts\\python.exe .\\scripts\\trace_weatherlife.py --process weather --process usbwr
```

For a controlled parser replay, serve an archived backup and add
`--redirect-base http://127.0.0.1:8765`. The tracer preserves the original URL
and records the redirected URL, HTTP reads, call stacks, and resulting device
writes. Use the original application's Data Refresh action while this capture
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

`CAL_USB_READ`, `CAL_USB_WRITE`, `DeviceIni`, and `CAL_USB_STATUS` are confirmed
(via GhidraMCP decompilation of `usbwr.dll!usbdeviceread`) to be real exported
functions in `onlywell.dll`, resolved with `GetProcAddress` — not raw command
byte codes as the labels below might suggest:

```c
#define CAL_USB_READ    0x01
#define CAL_USB_WRITE   0x02
#define CAL_USB_STATUS  0x03
#define CMD_DEVICE_INIT 0x04
```

The registration-handshake wire format (id generation, nibble packing, CRC-8)
is confirmed and implemented in `src/device_registration.c`. The weather/display
payload format is still not located; `onlywell.dll` (which owns the actual
`WriteFile` call) has not been decompiled. Keep `docs/PROTOCOL.md` explicit
about confidence levels and update it only when evidence supports a claim.

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
- GhidraMCP bridge: `C:\Users\rmonn\bin\bridge_mcp_ghidra.py`, configured in `.vscode/settings.json`
- GhidraMCP expects Ghidra's local HTTP server at `http://127.0.0.1:8080/`; its MCP SSE endpoint is `http://127.0.0.1:8081/sse`
- Open the `C:\Users\rmonn\weatherlife.rep` project and the relevant `weather.exe` or `usbwr.exe` program in Ghidra before using the bridge. The bridge exposes analysis operations but does not list or open Ghidra projects.
- Verify the loaded program before analysis by searching bridge functions for `CAL_USB`, `usbwr`, or `weather`; generic PE sections or empty matches mean the wrong program is loaded.
- The GhidraMCP plugin's "active program" is tied to the CodeBrowser tool/tab that has actual UI focus (clicking inside its Listing view), not just OS window focus, and not merely having a file imported/open in the project tree. If `/segments` or `/strings?filter=` keep returning a stale program after switching tools, click inside the target CodeBrowser's Listing view, then re-check.
- Confirmed 2026-09-17: `usbwr.dll` contains a single real function, `usbdeviceread` (exported), which implements the full device registration handshake (id generation, nibble packing, CRC-8). It is not the weather/display write path. See `docs/REVERSE_ENGINEERING.md` and `src/device_registration.c`.
- Confirmed 2026-09-17 via `onlywell.dll` decompilation and a handle-correlated Frida trace: the `CAL_USB_READ`/`CAL_USB_WRITE`/`DeviceIni` path uses SetupDi plus `HidD_GetFeature` and targets VID `0x1130`/PID `0x0202`. Treat the captured 9-byte and 17-byte frames as Tenx HID traffic; the weather payload meaning remains unconfirmed.
- Confirmed 2026-09-17 in `usbwr.exe` (the EXE, not the DLL, which has its own `CAL_USB_READ`/`CAL_USB_WRITE` strings): `FUN_00412fb0` parses weather-file text into up to 7 days of values, and `FUN_00401400` bit-packs them (variable-width, 2-6 bits per field) via `FUN_0040b9d0`/`FUN_0040d340`. This is the likely weather-payload serializer, structurally different from the registration handshake's nibble packing. Ghidra's decompiler fails on `FUN_00401400` itself; use `disassemble_function` instead. See `docs/REVERSE_ENGINEERING.md` §"Weather-Data Parser and Bit-Packing Serializer Located".
- Confirmed 2026-09-17: `FUN_00401400` packs `SYSTEMTIME` fields month(4 bits)/day(5)/hour(5)/minute(6) = 20 bits, exactly matching the archived legacy CSV's `UPD <20>` record. This proves the CSV's `BIT LENGTH` column is the literal native wire bit-width, not decorative — treat `python/legacy_weather_csv.py`'s schema as a high-confidence spec for the whole weather payload. The "today" packed segment totals exactly 128 bits (16 bytes, one `CAL_USB_WRITE` chunk); a second segment totals 64 bits (8 bytes). The packed buffer is sent via a resolved `CAL_USB_WRITE` function pointer in a chunked loop identical in structure to the registration handshake. See `docs/REVERSE_ENGINEERING.md` §"UPD Bit-Widths Confirmed".
- Protocol notes: `docs/PROTOCOL.md`
- Analysis report: `docs/REVERSE_ENGINEERING.md`
- Discovery tool: `python/discover_device.py`
- Passive tracer: `scripts/trace_weatherlife.py`

## GhidraMCP Mechanics

GhidraMCP is a plain HTTP bridge, not a registered agent tool in this
environment: call `http://127.0.0.1:8080/<endpoint>` directly (e.g. with
`Invoke-RestMethod` or curl), do not search for an MCP tool named "ghidra".

Useful endpoints: `GET segments`, `GET strings?filter=X&limit=N`,
`GET searchFunctions?query=X`, `GET imports`, `GET exports`,
`GET xrefs_to?address=0xNNNN`, `GET function_xrefs?name=X`,
`GET get_current_function`, `GET get_current_address`,
`POST decompile` (raw text body = function name),
`GET decompile_function?address=0xNNNN`,
`GET disassemble_function?address=0xNNNN`. Renames: `POST renameFunction`,
`POST rename_function_by_address`, `POST renameVariable`, `POST renameData`.

**Active-program gotcha**: the bridge always reflects whichever CodeBrowser
tool currently has actual UI focus — not OS window focus, and not merely
having a binary imported into the project tree. Importing/opening 4 files
does not make any of them active; you must click *inside* the target
CodeBrowser's Listing view (an in-app Ghidra action) after switching windows.
Always verify before trusting results: check `GET segments` (expect a
`.text` range matching the target binary's known size) and
`GET strings?filter=<a name unique to that binary>`. Generic MFC/ActiveX
exports (`DllRegisterServer`, `CView`, `CMiniDockFrameWnd`...) or an empty
filter match mean the wrong program is loaded.

**Function name vs. address lookups**: `POST decompile` takes a function
*name* (e.g. `usbdeviceread`, `FUN_00412fb0`) and works even for names Ghidra
auto-generated. `GET decompile_function?address=` and
`GET get_function_by_address?address=` require the address to be an actual
function *entry point*; an address that is merely inside a function (e.g. a
runtime return address from a stack trace) returns "No function found". Use
`GET xrefs_to` on a nearby string or `GET disassemble_function?address=` on
the raw address to locate the containing function first.

**Large/complex functions can fail to decompile** (`Decompilation failed`,
even with an extended timeout) while still disassembling fine. Fall back to
`GET disassemble_function?address=` and read the raw instructions manually;
look for repeated call patterns to small helper functions (which usually
*do* decompile) to infer what the large function does.

**Import/thunk addresses aren't real functions**: an address referenced from
disassembly may just be a `JMP` thunk to the real implementation elsewhere
(common for both real function calls and OS-import stubs). If
`POST decompile`/`GET decompile_function` returns "Function not found", run
`GET disassemble_function?address=` on that address first, follow the `JMP`
target, then decompile that address instead.

**Handle/device correlation**: to determine which physical device a
`WriteFile`/`ReadFile`/`HidD_GetFeature` call actually targets, decompiled
code alone isn't enough — pair it with a live Frida trace
(`scripts/trace_weatherlife.py`) that hooks `CreateFileA`/`CreateFileW` to
record a handle→path map, then tags later I/O calls with the resolved path.
Kill and respawn the target process under the tracer (`frida.spawn`) rather
than attaching to an already-running one, so no `CreateFileA` calls are
missed.

## Units used
Metric Units:
- Temperature in Celsius (¡ãC)
- Rainfall in Centimeters (cm)
- Wind Speed in Kilometers per Hour (km/h).

Imperial Units:
- Temperature in Fahrenheit (¡ãF)
- Rainfall in Inches (in)
- Wind Speed in Miles per Hour (mph).

Last updated: 2026-09-17
