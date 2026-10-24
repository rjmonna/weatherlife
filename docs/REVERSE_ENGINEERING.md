### Native weather-report differential proof

The passive hosts-file replay traces now provide direct native weather output,
separate from the registration handshake. The original application reads the
archived `weather.dat`, reaches `usbwr.exe!CAL_USB_WRITE`, and emits a 17-byte
HID report whose first byte is the report selector and whose remaining 16 bytes
are the serializer payload. Comparing the reports in
`traces/weather-hosts-replay-trace.jsonl` and
`traces/weather-localhost-replay-trace.jsonl`
finds three reports and four changed MSB-first bit offsets: 69, 108, 109, and
117. This proves that the native transport output is reproducible and responds
to changed fixture input.

It does not yet prove the semantic owner, scaling, or absolute field mapping of
those bits. Run `scripts/prove_encoder.py` for this transport-level check. The
confirmed `BitPacker` and standalone `UPD` append are safe for offline
serialization tests. The composite `weather_frame_build()` remains unproven.
The live USB send API refuses to transmit it until controlled one-field fixture
captures establish its field order and offsets. The proven-only offline helper
builds the native cursor boundary and `UPD` fields without claiming to produce
a display-ready packet.

### One-record native capture

`traces/weather-call-boundary-trace.jsonl` is a minimal native weather capture:
it contains one `CAL_USB_WRITE` call, one 17-byte weather report, and no
registration retransmission. The original request opened the archived
`06344.csv` URL. The serializer payload, excluding the HID report ID, is:

```text
10 14 10 10 10 10 10 10 14 10 10 10 10 12 19 1b
```

Compared with the multi-report replay in
`traces/weather-hosts-replay-trace.jsonl`, serializer bytes `0..6` remain
unchanged while bytes `7..15` vary. This confirms a stable header prefix and a
single-record native output boundary, but it does not assign weather fields:
the source response was not changed one named record at a time. Run
`scripts/prove_encoder.py` with both traces to reproduce the bit comparison.

### Cross-runtime C validation (2026-10-10)

The current native C tests pass in the Linux Meson build. The same pure-C test
sources (`test_native.c`, `device_registration.c`, `bit_packer.c`,
`weather_response.c`, and `weather_frame.c`) also compile with MinGW x86-64
under `-Wall -Wextra -Werror` and pass when run with Wine 10 x64. This validates
the tested C logic under the Windows ABI; it does not validate the Windows HID
backend, 32-bit binaries, or communication with the physical device through
Wine. The connected device was separately checked through the Linux backend
with a read-only poll, which returned sixteen `0x10` cells.

### Headless decompilation of `onlywell.dll` (2026-10-10)

The installed `C:\Program Files (x86)\Weather\onlywell.dll` was imported into
Ghidra 11.1 headlessly as a 32-bit x86 PE. The following transport call chain
was decompiled from the installed binary:

| Address | Function | Confirmed behavior |
| --- | --- | --- |
| `0x10001ef0` | `CAL_USB_WRITE` | Allocates a 17-byte report buffer, copies exactly 16 caller bytes to offsets 1..16, and calls the low-level writer. |
| `0x10001b80` | `FUN_10001b80` | Builds the eight-byte `USBC` write header, sends it to the command handle, then sends the caller payload to the payload handle. |
| `0x100019f0` | `FUN_100019f0` | Copies each payload byte into the HID report buffer at offset 1 and calls `WriteFile` using the HID output-report length. |
| `0x10001ab0` | `FUN_10001ab0` | Builds the eight-byte `USBC` read header, sends it, then calls `HidD_GetFeature` for the requested report length plus the report ID. |
| `0x10001970` | `FUN_10001970` | Sends the status/read header and performs a 17-byte `HidD_GetFeature` call. |
| `0x10001000` | `FUN_10001000` | Enumerates HID interfaces and opens two matching Tenx handles. |
| `0x100011d0` | `FUN_100011d0` | Opens a HID path, verifies VID `0x1130` and PID `0x0202`, checks HID capabilities, and records the handle. |

`FUN_100019f0` is the display-write boundary. It does not inspect or
transform payload contents: it only copies bytes, prefixes the report-ID byte
already present in its allocated report buffer, and invokes `WriteFile`.
`CAL_USB_WRITE` likewise does not decode weather fields or determine whether
the device accepted or rendered the data. No display update routine exists in
this DLL. The weather serializer and any device-side display interpretation
must therefore be in `usbwr.exe` or firmware; `onlywell.dll` cannot resolve
the remaining display-acceptance assumption.

### Headless decompilation of the native weather sender in `usbwr.exe` (2026-10-10)

The installed `usbwr.exe` was imported and analyzed with the same headless
Ghidra workflow. Its startup path at `0x00401400` loads `onlywell.dll` and
resolves `CAL_USB_WRITE`, `CAL_USB_READ`, and `DeviceIni` with
`GetProcAddress`. It calls `DeviceIni`, performs readback through
`CAL_USB_READ`, and retains the `CAL_USB_WRITE` function pointer for the
weather update loop.

The static update path is:

```text
FUN_00412fb0(weather_values)
  -> parse weather.dat named records and derive current/daily values
FUN_00401400
  -> initialize selector/header cells
  -> FUN_0040d340(..., bit_count, ...)
       MSB-first variable-width bit copy into the native buffer
  -> FUN_0040bc70(..., 0x40)
       finalize/copy a 64-bit chunk
  -> CAL_USB_WRITE(...)
       write each 16-byte chunk, with a 2 ms inter-chunk delay
```

The decompiler recovered these relevant behaviors:

- `FUN_00412fb0` reads named records beginning with `TEMPH`, computes the
  current value and up to six daily values, and stores the results for the
  sender.
- `FUN_0040d340` copies source bits MSB-first, advances the destination nibble
  cursor, and supports the native variable field widths.
- The sender initializes buffers with `0x10` nibble cells, sets selector/header
  cells, packs fields, computes the number of 16-byte writes from the bit
  length, and invokes the resolved `CAL_USB_WRITE` pointer for every chunk.
- The same loop appears in the selector-specific branches, including the
  128-bit primary segment and the 64-bit secondary segment. The native code
  sleeps 2 ms between chunks.

This proves that the original application does construct and transmit native
weather-update buffers through the same HID transport implemented by the
replacement. It does **not** prove LCD rendering: `usbwr.exe` does not receive
an explicit display-success result, and the device-side interpretation is not
present in either executable. The live tests therefore establish transport
delivery and transient device responses, but physical display acceptance still
requires a visible device update or firmware-level evidence.

### LCD mechanics search (2026-10-10)

Additional inspection of all installed Weather-Life binaries found no
controller-specific LCD instructions or data tables. Searches for LCD,
segment, character, matrix, framebuffer, and screen operations found only UI
resources and generic HID/Win32 I/O symbols. The decompiled call graph also
contains no character-rendering loop or display-success parser. The proven
LCD-facing contract therefore stops at:

```text
native weather bits -> 16-byte CAL_USB_WRITE payload chunks -> Tenx HID
```

The LCD's character/segment mapping and firmware acceptance behavior are not
contained in the available executables. The C implementation preserves this
boundary: it implements the proven native chunk sender with the original
approximately 2 ms inter-chunk delay in the selector-6 experiment, but
continues to refuse general weather transmission until an LCD update is
observed or the firmware protocol is recovered.

The user reports that the connected hardware visibly shows sun/weather
symbols, segmented date/time, and a fixed view of approximately five forecast
days. This confirms the physical presentation is richer than a plain 16x2
character display, but does not identify the wire selector for each visible
slot. It also does not reconcile the visible five-day view with the native
parser's statically present Day1--Day6 selector families; the extra source day
may be rotated, unused, or a separate internal record.

The user further reports that a tray-icon action appeared to send current
conditions plus five forecast days over USB/HID. This matches the recovered
process split in which `weather.exe` initiates the refresh and `usbwr.exe`
parses the resulting records and emits multiple selector writes. The report
is retained as runtime evidence, not as a decoded packet capture: the
decompiled parser has capacity for one current value plus up to six daily
values, while the visible five-day presentation does not identify which
selector family is omitted or rotated.

### Additional binary coverage (2026-10-10)

The installed `usbwr.dll` was independently imported and analyzed. Its sole
export is `usbdeviceread` at `0x1000100f`. The decompiled helpers confirm the
registration mechanics implemented in `src/device_registration.c`:

- `FUN_10001650` forms the five-character ASCII identifier from the low
  millisecond nibble and a zero-padded hexadecimal `rand()` value.
- `FUN_10001830` normalizes each response cell with `cell & 0x0f`.
- `FUN_100018b0` reconstructs logical bytes from adjacent nibble cells,
  applies the embedded 256-entry CRC table, and writes the checksum as two
  `0x10 | nibble` cells.
- `usbdeviceread` waits for a response whose normalized first cell is zero,
  second cell is nine, and next ten cells echo the generated identifier.
  Otherwise it builds the registration payload, sends it in 16-cell chunks,
  and retries reads.

This independently confirms the registration behavior implemented in
`src/device_registration.c` and `python/dongle_protocol.py`.

The installed `weather.exe` imports as a Delphi-packed PE. Ghidra's headless
function recovery identifies only an entry stub and reports invalid control
flow when decompiling it. Its UI strings confirm city selection, refresh, and
update forms, but this binary did not yield a reliable weather-to-USB call
graph. The reliable sender remains `usbwr.exe`; no code was changed based on
the unusable `weather.exe` entry decompilation.

# Reverse Engineering Report: Weather-Life Dongle

**Date**: 2026-09-18
**Status**: Partial - Live HID Traffic Captured
**Tools Used**: Ghidra, Binary Analysis, Python String Extraction

## Executive Summary

Weather-Life is a Windows-only USB LCD weather display application (defunct since weather-life.com shutdown ~2024). Through systematic reverse engineering of the application binaries and website backup, we have mapped the confirmed transport and registration framing and identified the hardware interface. The complete semantics of the weather payload remain unconfirmed.

**Key Finding**: The confirmed application path is a Tenx composite HID device
at VID `0x1130`, PID `0x0202`. The handle-correlated trace found no alternate
transport, so the implementation is HID-only.

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

**Other numeric candidates found in binaries:**

Running `find_all_vid_pids.py` against all binaries:

```
The scan found many vendor-like numeric values, but none of these values were
used as a confirmed device identity by the traced discovery path.
```

**Conclusion**: The numeric Silicon Labs matches were incidental binary data
and are not a confirmed Weather-Life target. The confirmed target is the
hardcoded Tenx HID pair `0x1130:0x0202` in `onlywell.dll`.

### Hardware Specification

**Confirmed Device:**
- **Manufacturer/description**: Tenx Non standard Device
- **VID**: 0x1130
- **PID**: 0x0202
- **Interface**: Composite HID with Usage `0` and Usage `3`
- **Report transport**: HID feature reports; exact weather payload remains unconfirmed

### Display Hardware

- **Type**: 16x2 character LCD (most common)
- **Interface**: Tenx composite HID
- **Data Format**: Binary HID feature reports; weather field mapping remains
  incomplete
- **Update Rate**: Hourly or on-demand (inferred)

## USB Communication Protocol

### Live HID Trace (2026-09-08)

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
path, with the traced caller at `usbwr.exe+0x404eef`.

### Registration Handshake Confirmed via GhidraMCP (2026-09-17)

GhidraMCP was connected to a live Ghidra CodeBrowser session with `usbwr.dll`
as the active program (verified by finding `CAL_USB_READ`/`CAL_USB_WRITE`
strings at `1002d028`/`1002d038` before trusting any decompilation). `usbwr.dll`
exports exactly one real function, `usbdeviceread` (`0x1000100f`), which is the
entire content of the DLL. Decompiling it, and its helpers `thunk_FUN_10001650`,
`FUN_10002250`, `thunk_FUN_10001830`, and `thunk_FUN_100018b0`, proves the
17-byte frame is a **device registration handshake**, not weather/display
content:

1. `usbdeviceread` loads `onlywell.dll` and resolves `CAL_USB_WRITE`,
   `CAL_USB_READ`, and `DeviceIni` via `GetProcAddress` — these are real
   exported functions in `onlywell.dll`, not raw command byte codes.
2. It generates a registration id: the ASCII string `"%x%04x"` of
   `(milliseconds & 0xf, rand() & 0x7fff)` (`thunk_FUN_10001650`; `FUN_10002250`
   is a plain `strcpy`, not encryption). The first 5 characters of that string
   are nibble-expanded into 10 bytes (high nibble, then low nibble of each
   ASCII byte).
3. It builds a 16-byte buffer: every byte starts at `0x10`, byte[1] additionally
   has bit `0x08` set, and the 10 id nibbles are OR'd into bytes[2..11].
4. `thunk_FUN_100018b0` computes a standard **CRC-8 (polynomial 0x31,
   MSB-first)** over the 7 byte-pairs at bytes[0..13] (each pair recombined as
   `(high<<4)|(low&0xf)`), XORing `table[reconstructed_byte]` into a running
   checksum (not a proper feedback CRC — the table is only ever indexed by the
   raw byte, matching a well-known public CRC-8 table verified independently
   against the decompiled constants, e.g. `table[0x8c] == 0x07`). The checksum
   is written as two nibbles at bytes[14..15] (`0x10 | (crc>>4)`, `0x10 | (crc&0xf)`).
5. This 16-byte buffer is sent via `CAL_USB_WRITE` in up to 4 chunks with
   100 ms delays, and `CAL_USB_READ` is polled up to 20 times; `thunk_FUN_10001830`
   masks each response byte with `& 0xf` (undoing the same nibble encoding),
   and a match requires `decoded[0]==0`, `decoded[1]==9`, and `decoded[2..11]`
   equal to the id nibbles.

This algorithm was verified **byte-exact** against both historical raw
captures: recombining bytes[0..13] of each capture and running them through
the CRC-8 table reproduces bytes[14..15] exactly (`0x3D` → `13 1d` for the
first capture, `0x5F` → `15 1f` for the second). The two captures differ only
because each `usbdeviceread()` invocation generates a fresh, time/rand-seeded
id.

The complete `usbdeviceread()` control flow adds these details:

- It performs an initial `CAL_USB_READ` on device index `1` before generating
  or sending the registration frame.
- The generated ID is one hexadecimal millisecond nibble followed by a
  four-character, zero-padded hexadecimal `rand()` value. On MSVC, `rand()` is
  already 15-bit, matching the equivalent `rand() & 0x7fff` expression in the
  offline implementation.
- It polls reads up to 20 times. A failed read advances the device index from
  `1` through `4` before wrapping back to `1`; a successful but non-matching
  response causes the same registration frame to be sent again in four
  16-byte chunks with 100 ms delays.

### Device-button registration capture (2026-09-17)

The long passive capture in `traces/device-registration-trace.jsonl` was started
after `weather.exe` and `usbwr.exe` were already running, then the original
application's **Device** action was used. It contains 65,334 events over about
17 minutes. The capture shows:

- 47 successful 17-byte HID reports through `CAL_USB_WRITE`; the captured
  stacks do not include the high-level caller, so these cannot be classified
  as registration or weather payloads;
- 47 `CAL_USB_WRITE` calls and 47 matching command-2 writes;
- 15,267 repeated read-poll writes of
  `00 55 53 42 43 00 10 01 00`;
- every `HidD_GetFeature-return` report was
  `00 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10`;
- no `usbdeviceread` or `GetProcAddress` events, because the tracer attached
  to already-loaded processes and therefore missed the earlier export
  resolution and DLL entry call.

All captured feature reports were `0x10`-filled, but this trace alone cannot
tie those reads to registration because it did not observe `usbdeviceread` or
the high-level write caller. A separate one-shot probe using the decompiled
registration builder did receive an ID-matched acknowledgment. Do not use the
17-byte report length in this trace as proof that a report was a registration
frame.
Run `scripts/analyze_trace.py traces/device-registration-trace.jsonl` to print this
registration summary automatically.

**Still unconfirmed**: the semantic meaning of byte[13] and the weather fields
is not established by the registration analysis. The active `onlywell.dll`
decompilation confirms that the low-level transport passes the 16-byte buffer
through unchanged, adds the HID report framing, and owns the real `WriteFile`
call. Separately, the 9-byte poll/control
frames begin with `55 53 42 43` = ASCII `"USBC"`, the same signature bytes as
a USB Mass-Storage Bulk-Only Transport CBW (`dCBWSignature = 0x43425355`);
this is a pattern match, not a decompiled confirmation, and belongs to
`onlywell.dll`, not `usbwr.dll`.

The confirmed algorithm (id generation, nibble packing, CRC-8) is implemented
in `src/device_registration.c` and mirrored in `python/dongle_protocol.py`
(`build_registration_frame`) for independent cross-checking. It builds a
valid `usbwr.dll`-side buffer; it does not yet reproduce `onlywell.dll`'s wire
framing.

### Transport Target Confirmed via Handle Correlation (2026-09-17)

GhidraMCP was switched to `onlywell.dll` as the active program (confirmed via
its 4 exports: `CAL_USB_READ`/`CAL_USB_WRITE`/`DeviceIni`/`CAL_USB_STATUS`).
Decompiling all four, plus `FUN_10001000` (device open), `FUN_100011d0` (HID
open+match), `FUN_100019f0` (write transport), and `FUN_10001ab0` (read
transport), proves:

- `CAL_USB_WRITE` builds an 8-byte header (`55 53 42 43` = ASCII `"USBC"`,
  reserved byte, length byte, an **opcode byte** — hardcoded `0x02` for write,
  `0x01` for read — and a device-index byte from `'0'`/`'1'`/`'2'` char
  codes), sends it via `FUN_100019f0`, then sends the 16-byte `usbwr.dll`
  buffer **unmodified** (byte[13] passes through untouched; `onlywell.dll`
  does not explain it either).
- `FUN_100019f0` (the real transport) forces a **leading report-ID byte of
  0x00** before every `WriteFile`, chunked to the device's HID report length.
  This explains the leading `0x00` seen in every capture.
- `CAL_USB_READ`'s response comes from **`HidD_GetFeature`** (a HID feature-
  report IOCTL), not `ReadFile`.
- `FUN_100011d0` requires the opened device to match **VID `0x1130`, PID
  `0x0202`**, with the required HID usages described above.

The `onlywell.dll` decompilation also establishes the exact low-level contract:

- `CAL_USB_WRITE(device, buffer)` emits the eight-byte control header
  `55 53 42 43 00 <length-low> <opcode> <device-index>`, where the opcode is
  `0x02` and the device index is derived from the character `'0'`, `'1'`, or
  `'2'`. It then emits the caller buffer unchanged.
- `CAL_USB_READ(device, buffer)` emits the same header with opcode `0x01`,
  calls `HidD_GetFeature` with `length + 1`, and copies the returned report
  without its first byte.
- `FUN_100019f0` prepends a zero report-ID byte before each `WriteFile` call;
  this is why the observed control and payload reports are 9 and 17 bytes
  while the logical buffers are 8 and 16 bytes.
- `FUN_10001000` opens the matching Usage `0` and Usage `3` interfaces into
  separate handles. The feature reads and payload writes use the Usage `0`
  handle, while the eight-byte control writes use the Usage `3` handle.

This was verified empirically, not just from decompiled code: `usbwr.exe` was
killed and respawned fresh under Frida with a `CreateFileA`/`CreateFileW`
handle-to-path hook (`scripts/trace_weatherlife.py`). Every single write — the
9-byte poll frame, the `... 02 00` control frame, and the 17-byte
registration frame — targeted
`\\?\hid#vid_1130&pid_0202&mi_01#...` or `mi_00#...`. No serial device was
opened by `usbwr.exe` in this trace.

**Conclusion**: the registration-handshake algorithm above is real and
byte-exact, but its weather-display meaning remains unconfirmed. All previous
9-byte and 17-byte captures are Tenx HID traffic. The weather payload format
has not yet been fully decoded.

### Weather-Data Parser and Bit-Packing Serializer Located (2026-09-17)

GhidraMCP was switched to `usbwr.exe` (the EXE, not the DLL) as the active
program (confirmed via base `0x400000`, `.text: 0x401000-0x446fff`, matching
the earlier objdump layout). `usbwr.exe` has its own separate
`CAL_USB_READ`/`CAL_USB_WRITE` strings (`0x447040`/`0x447050`), independent of
`usbwr.dll`'s registration-only `usbdeviceread`.

- `FUN_00412fb0` (xref'd from the `TEMPH` string at `0x4478c8`) parses a
  weather-file (`device.dat`/`weather.dat`-style) text buffer for named
  records, spaced 7 bytes apart per key, across a **current-conditions block
  (5 sub-fields) plus up to 7 forecast-day blocks (4 sub-fields each)**,
  computing one temperature-like float per day into an output array
  (`param_1[0..6]`). This is the weather-data equivalent of
  `python/legacy_weather_csv.py`, implemented natively in `usbwr.exe`.
- `FUN_00401400` references both `CAL_USB_READ` and `CAL_USB_WRITE` strings
  and is almost certainly the weather-write orchestrator, but Ghidra's
  decompiler fails on it outright (`Decompilation failed`, even with a 90s
  timeout) — it had to be read from raw disassembly instead
  (`disassemble_function`).
- Its repeated pattern calls two small helpers (each reached via an import-
  style jump thunk): `FUN_0040b9d0` converts a numeric value into 1-2 raw
  bytes, and `FUN_0040d340` appends **N bits (MSB-first, variable width —
  observed 2, 3, 4, 5, and 6 bits per field in the disassembly)** of a source
  byte into a shared output bitstream at a running bit position. This is a
  genuine **variable-width bit-packing serializer**, structurally different
  from the registration handshake's one-nibble-per-byte scheme, and is the
  strongest lead yet for the actual "send temperature/humidity for today and
  the next few days" payload format.
- `FUN_004137a0`, a second, simpler `LoadLibraryA("onlywell.dll")` +
  `GetProcAddress("CAL_USB_WRITE")` wrapper, has **zero static callers** and
  appears unreachable/vestigial.

`FUN_00401400` is not a single linear routine: it is a **dispatcher with a
~20-entry jump table** at `0x409374` (bound-checked against `0x13`), so most of
its body handles other operations. The bit-packing code identified above is
inside one branch (reached when a dispatch value equals `5`). That branch:

- Initializes the shared output-bitstream cursor globals (`DAT_0044e470`,
  `DAT_0044e574`, the same ones read/written by `FUN_0040d340`) to **byte
  offset 10, bit 4** before packing any fields, implying a 10-byte header
  precedes the case-5 packed date/time data.
- Reads the current local time via a `SYSTEMTIME`-shaped import call (four
  `AND reg,0xffff` extractions matching `wYear`/`wMonth`/`wDay`/`wDayOfWeek`
  or similar WORD fields), which is then packed into the frame \u2014 consistent
  with the legacy `UPD <20> <03>month,<08>day,<07>hour,<41>minute;` record
  already confirmed in `python/legacy_weather_csv.py`.

The case-5 branch must not yet be identified with the complete current-weather
record. Its append sequence begins with conditional year fragments and local
calendar fields, then appends the four `UPD` components. The branch later sets
the primary length to `0x80` bits and sends that buffer. If its pending-send
flag is set, it clears the flag, ORs working-buffer bytes `0` and `1` with
`0x0f`, sets the length to `0x40` bits, and sends a second 8-byte segment.
This proves the two segment lengths and their control-flow relationship, but
does not identify the second segment's semantic fields.

### UPD Bit-Widths Confirmed; CSV Bit-Length Column Proven Authoritative (2026-09-17)

Continuing the disassembly of `FUN_00401400`'s bit-packing branch (dispatch
case `5`) through its `SYSTEMTIME` field packing calls:

- `wMonth` is packed with `FUN_0040d340(..., bits=4)`
- `wDay` is packed with `bits=5`
- `wHour` is packed with `bits=5`
- `wMinute` is packed with `bits=6`

`4 + 5 + 5 + 6 = 20` bits — an **exact match** for the archived legacy CSV's
`UPD <20> <03>month,<08>day,<07>hour,<41>minute;` record. This is strong
evidence that the **`BIT LENGTH` column in the archived `update/city/*.csv`
format is not decorative documentation — it is the literal native
bit-packed wire width for that field**, and the field order matches the CSV's
own top-to-bottom listing. This means the schema already parsed by
`python/legacy_weather_csv.py` (current-conditions fields `TEMP<9>`,
`BTEMP<9>`, `WEA<6>`, `PRE<11>`, `TREND<2>`, `WS<6>`, `BFT<6>`, `WW<4>`,
`HUM<7>`, `VIS<7>`, `UVI<5>`, `DEWP<9>`, and per-day forecast fields
`TEMPH<9>`, `TEMPL<9>`, icon `<6>`, wind `<8>`, etc.) can be treated as a
**high-confidence specification for the entire wire payload**, without
needing to individually re-derive every field's bit-width from assembly —
only a sample (UPD's 4 sub-fields) has been directly verified this way so far.

Further disassembly also confirmed the case-5 primary segment's **total size
is exactly `0x80` bits = 128 bits = 16 bytes** — one full `CAL_USB_WRITE`
chunk — computed via the same ceiling-division idiom seen in the registration
handshake (`(len+3)>>2` then `(x+15)>>4`). A second, smaller segment totals
When the branch's pending-send flag is set, a second segment totals `0x40`
bits = 64 bits = 8 bytes; before sending it, the first two working-buffer
bytes are ORed with `0x0f`. The packed buffer is then sent through a
function pointer stored at a local stack slot (resolved earlier via
`LoadLibraryA("onlywell.dll")` + `GetProcAddress("CAL_USB_WRITE")`, the same
pattern as `usbwr.dll`'s registration path) in a chunked loop structurally
identical to the registration handshake's (`for i in 0..count:
call(ptr, buffer + i*16)`), confirming this branch does end with a real
`CAL_USB_WRITE` call.

The confirmed, reusable bit-packing primitive (MSB-first, variable width) is
implemented in `src/bit_packer.c` for use once the remaining field order is
mapped into a real weather-frame builder. It reproduces `FUN_0040d340`'s
append semantics; it is not a byte-for-byte port of the two-step
value-to-bytes-then-bits conversion (`FUN_0040b9d0` + `FUN_0040d340`) since
that split is an implementation detail, not part of the wire format.

### Daily Dispatch Indices Correlated (2026-09-17)

### Arbitrary Bit-Cursor Behavior Confirmed (2026-09-27)

With `usbwr.exe` active in GhidraMCP, `FUN_0040d340` was decompiled again.
Its destination cursor is split across `DAT_0044e574` (destination byte
offset) and `DAT_0044e470` (MSB-first bit-mask index). The weather branch at
the case-5 branch at `0x4084f5` initializes these values to byte `10` and bit offset
`4`, then appends fields without requiring byte alignment. The helper rolls
the source bit index from `7` back to `0`, advances the source byte, and rolls
the destination bit index from `7` back to `0`, advancing the destination
byte and resetting its bit offset to `4`.

The local `BitPacker` now exposes `bit_packer_init_at()` for this confirmed
non-zero starting cursor. This adds the primitive needed to model the native
header-plus-payload layout, but the field values and complete header remain
unconfirmed and are intentionally not wired into `weather_frame_build()`.

The same inspection reconfirmed that `usbwr.exe` resolves `CAL_USB_WRITE` at
`0x40336a` from `onlywell.dll`; the weather branch sends a primary `0x80`-bit
segment and can send a second `0x40`-bit segment in 16-byte chunks. The
registration frame remains a separate path and must not be used as a weather
payload.

The lookup tables initialized at the start of `FUN_00401400` provide a direct
day correlation for the weather record dispatcher. `f9cc[]` contains paired
day-block selectors: dispatch records `0..5` select the current-condition
block, then records `6/7`, `8/9`, `10/11`, `12/13`, and `14/15` select the two
daily fields for `DAY1` through `DAY5`. Records `16/17` and `18/19` continue
the same pattern for `DAY6` and `DAY7`.

The first two fields in each daily block are `TEMPH` and `TEMPL` in the
archived weather file, and both are initialized with 9-bit widths in the
serializer's daily-width table. This confirms that the data for the next four
days is part of the weather serialization path; it is not a clock-only update
or an optional local display calculation.

### Case-5 Value and Header Boundary Confirmed (2026-09-27)

Additional GhidraMCP decompilation of `FUN_00412fb0`, `FUN_0040b9d0`, and
`FUN_0040d340`, together with the `0x4084f5` case-5 disassembly, narrows the
remaining uncertainty:

- `FUN_00412fb0` reads the named weather file, extracts text between `<` and
  `>`, parses decimal values, converts the encoded value to a float, and
  computes one rounded temperature-like result. It processes five fields for
  the current block and four fields for each daily block, returning up to seven
  values in `param_1[0..6]`. This proves the provider boundary must supply the
  legacy record semantics, not merely `temperature`, `humidity`, and a weather
  code.
- The numeric helper used by the serializer stores values below `0x100` in one
  byte and larger values in two bytes, high byte first, before
  `FUN_0040d340` appends the requested bit count. Negative values are retained
  as signed integers until this conversion; the separate temperature rounding
  path remains the source of the 9-bit temperature values.
- Case 5 loads the local `SYSTEMTIME` fields and appends year fragments, month
  (4 bits), day (5), hour (5), minute (6), second (6), and day-of-week (3).
  It then parses and appends the `UPD` month/day/hour/minute values at
  `0x408c05`, `0x408c8f`, `0x408d1a`, and `0x408da5` with widths 4/5/5/6.
- Before those appends, case 5 copies the existing working buffer, sets the
  destination cursor to byte `10` and mask `0x08`, and applies
  `working[1] |= 0x07`. There are no literal writes that construct working
  bytes `0..9` in this branch. Those ten header bytes are prepared by an
  earlier dispatcher operation and are still not semantically decoded.
- The same working buffer is passed to `CAL_USB_WRITE` with a primary length
  of `0x80` bits. When the pending-send flag is set, the buffer is copied,
  `working[0] |= 0x0f` and `working[1] |= 0x0f` are applied, and a second
  `0x40`-bit segment is sent. This proves the segment control bytes but not
  the meaning of every header bit.

Therefore the BitPacker can now safely model the cursor, time fields, `UPD`,
segment lengths, and the confirmed value conversion. It still must not invent
bytes `0..9`, the current-condition field order, or the optional segment's
semantic fields. `weather_frame_build()` remains experimental until the
earlier header-producing dispatcher cases are traced.

### Composite Case-5 Findings (2026-09-27)

The active `usbwr.exe` program remains the correct Ghidra target; no switch to
`weather.exe` or `onlywell.dll` is needed for this serializer. Raw disassembly
of `FUN_00401400` shows the case-5 branch appending multiple fields before the
`UPD` record and additional fields after it. The confirmed `UPD` calls occur
at `0x00408c05`, `0x00408c8f`, `0x00408d1a`, and `0x00408da5`, with widths
4/5/5/6, but they are not the beginning of the primary segment. Immediately
before them, the branch appends fields with widths 7, conditional 6/8, 4, 5,
5, 6, 6, and 3. This disproves the repository candidate's assumption that
`UPD` starts at bit zero followed directly by the current-condition fields.

The same dispatcher calls `FUN_0040bc70` through its thunk `0x00401069` after
building packed segments. It uses the same lookup table and nibble-pair
reconstruction algorithm as `usbwr.dll!thunk_FUN_100018b0`; the bit-length
argument selects the coverage and checksum-cell positions. For the 128-bit
primary segment it checks 15 reconstructed bytes and appends checksum nibbles
at cells 30 and 31. Registration uses the same algorithm with a 64-bit length,
covering seven reconstructed bytes and storing its checksum in cells 14 and
15. The algorithm is shared; the frames and their coverage are distinct.

To promote the composite encoder, the remaining work is concrete: trace the
dispatcher cases that populate working-buffer bytes `0..9`, map each append
source to `TEMP`, `BTEMP`, `WEA`, `PRE`, `TREND`, `WS`, `BFT`, `WW`, `HUM`,
`VIS`, `UVI`, `DEWP`, and the daily records, and prove the `0x40` segment's
field list. Then replay one-field legacy fixtures and require byte-for-byte
agreement with the original `CAL_USB_WRITE` payload before enabling hardware
transmission.

### Weather Write Caller and Response Dispatch (2026-10-09)

Ghidra confirms the captured writer's call chain: `entry` calls
`thunk_FUN_00401400` with the standard WinMain arguments; that thunk jumps to
`FUN_00401400`. The captured stack return address `0x00404eef` is immediately
after the indirect `CAL_USB_WRITE` call at `0x00404ee9` inside that dispatcher.
That block computes a `0x40`-bit segment and sends one 16-byte
`CAL_USB_WRITE` payload report, so the caller-identified
`weather-fixture-trace.jsonl` writes are native serializer output, not
registration frames. `FUN_004137a0` is only a dynamically resolved
`CAL_USB_WRITE` wrapper and has no static callers.

The matching read call is at `0x00403dc8` (the captured return address is
`0x00403dce`). Its response passes through `thunk_FUN_0040ce60`, which masks
each of 16 bytes with `0x0f`. The dispatcher uses decoded byte 1 as a selector:
handlers exist for `1`, `6`, and `8`; a response with decoded prefix `00 0b`
falls through to the shared continuation and has no explicit success/error
handler. That continuation sends another `0x40`-bit segment only if a separate
pending-send flag is set; selector `0x0b` itself does not set that flag. The
live `0x0b` report is therefore a real post-write device response, but its
status meaning and display effect are not established by this executable.

The 1,819 historical `0x0b` reports checked from the controlled, long-capture,
and reconnect traces do not match the registration frame's 64-bit checksum
layout. This distinguishes them from registration acknowledgments; it does
not prove a weather-response checksum.

### Fixture-Backed Forecast Write Differential (2026-10-09)

The original Weather-Life `Data Refresh` tray command was captured while its
HTTP request was redirected to the archived Rotterdam CSV. With the redirect
buffer lifetime fixed in `scripts/trace_weatherlife.py`, `InternetOpenUrlA`
returned success and `InternetReadFile` read all 5,642 fixture bytes. The
native helper then emitted 64-bit, 16-cell weather segments at callsites
`0x00404b89` and `0x00404eef`. Captured cells all had the `0x10` prefix, and
the 64-bit checksum in cells 14 and 15 matched the decompiled native table
algorithm.

A controlled second refresh served a temporary fixture changing only
`DAY1/TEMPH` from `6` to `12`; the archived source was not modified. Comparing
the first post-fetch write at `0x00404b89` showed exactly three changed cells:
cell 8 changed `0x14` to `0x18`, and checksum cells 14 and 15 were recomputed.
The other 13 cells were identical, and both segments passed the native
64-bit checksum check. This is runtime evidence that a Day-1 forecast field
flows through the original parser/serializer into a write accepted by the HID
transport. The following response still decoded to `00 0b`; the visible
forecast grid remained placeholders, so display acceptance/rendering is not
proven. The 128-bit selector-6 primary segment and its complete Day-1 layout
were not observed in this refresh.

A separate fixture changed only current `TEMP` from `-1.0 C` to `10.0 C`. Its
first post-fetch `0x00404b89` write changed the same data cell from `0x14` to
`0x1c`, with only checksum cells 14 and 15 also changing. The 64-bit checksum
remained valid and the first response again decoded to `00 0b`. These
independent current-weather and Day-1 forecast mutations prove both inputs
flow through the original serializer into dongle writes; neither proves that
the display accepts or renders the data.

### Final HID Wire Offset Proven (2026-09-17)

The active `onlywell.dll` analysis resolves the final framing boundary:

- `CAL_USB_WRITE` at `0x10001ef0` accepts the report selector and a pointer to
  the serializer buffer.
- It allocates a 17-byte temporary buffer, copies exactly 16 input bytes into
  temporary bytes `0..15`, and passes those 16 bytes unchanged to
  `FUN_10001b80`.
- `FUN_10001b80` sends an 8-byte control report, then sends the payload through
  `FUN_100019f0`.
- `FUN_100019f0` writes a zero report byte at destination byte `0`, copies the
  payload to destination bytes `1..16`, and calls `WriteFile` with the HID
  report length. The observed data report is therefore 17 bytes.

Consequently, a serializer-buffer byte `n` is HID wire-report byte `n + 1`.
The serializer branch that initializes `DAT_0044e574` to `0x0a` and
`DAT_0044e470` to `4` writes its first packed bit at serializer byte `10`,
mask `0x08`, which is wire-report byte `11`, mask `0x08`. If reports are
artificially concatenated, the preceding 9-byte control report would add
another 9 bytes, making that byte concatenated-stream offset `20`; the device
actually receives separate HID reports, so report-relative offset `11` is the
meaningful wire offset.

This proves the transport offset, but not the semantic identity of every field
at that position. The current weather field order remains experimental until
each append call is mapped to its source record.

### Full HID Request/Response Contract Proven (2026-09-27)

The installed `onlywell.dll` disassembly closes the HID framing contract for
both directions. `CAL_USB_WRITE` and `CAL_USB_READ` exchange separate control
and data reports:

```text
write request:
  HID report 0: 00 55 53 42 43 00 10 02 <device-index>
  HID report 1: 00 <16 serializer bytes>

read request:
  HID report 0: 00 55 53 42 43 00 10 01 <device-index>
  HID feature response: 17 bytes, report id 00 + 16 response bytes
```

The low-level transport writes the control report through `WriteFile`, then
writes the 16-byte payload unchanged. It allocates a 17-byte temporary buffer,
copies serializer byte `n` to HID report byte `n + 1`, and sends the report
with a leading report-id byte `0x00`. The read path sends the opcode-`0x01`
control report and invokes `HidD_GetFeature` with length `0x11` (17); success
requires the HID call to succeed and the returned byte count to equal 17.

The registration caller adds the semantic response condition: after masking
each response byte with `0x0f`, it accepts only a response whose decoded bytes
have `decoded[0] == 0`, `decoded[1] == 9`, and `decoded[2..11]` equal to the
ten registration-id nibbles it generated. This is the complete proven
request/response contract for the registration path. Existing captures show
`HidD_GetFeature` returning `ok=true` and 17 bytes of `00 10 ... 10`, which
proves transport success but fails the semantic registration acknowledgement.
No weather-payload response semantics are present in this path; weather writes
are one-way serializer reports.

### `weather_frame_build` Proof Status (2026-09-27)

The current C builder cannot be promoted as a native encoder. Its first output
bit is written at frame byte `0`, bit mask `0x80`, and it appends `UPD` first.
Native case 5 starts at serializer byte `10`, mask `0x08`, after a separately
constructed ten-byte header; it appends local calendar fields, `UPD`, and
additional fields in a dispatcher-controlled order. Native header construction
also includes five selector cases, dynamic record processing, and a checksum
helper. Therefore the current `weather_frame_build()` differs in cursor,
header, field order, and checksum behavior before any input values are
considered.

The available traces cannot prove or disprove a corrected builder: they contain
multiple changed records and do not identify the selector or source fixture
for each write. The required proof is a fresh controlled replay with one
legacy record changed per run, capturing the 16 serializer bytes immediately
before `CAL_USB_WRITE`, followed by byte-for-byte comparison against a builder
fed the same complete legacy record model. Until that comparison exists,
`weather_frame_build()` remains experimental and live transmission must remain
disabled.

### Case-5 Append Inventory (2026-09-17)

The dispatcher branch entered when the operation value is `5` initializes the
append cursor to destination byte `0x0a`, mask index `4` (`0x08`). Its traced
append calls are:

```text
call       source scratch       source bit  width  known source
00408936   f85c                  7           1      adjusted year path
00408982   f85c                  6           2      year path, conditional
004089a0   f85d                  0           8      year path, conditional
004089c0   f85d                  6           2      year path, conditional
004089de   f85c                  0           8      year path, conditional
00408a12   f85c                  4           4      month (fb5c)
00408a46   f85c                  3           5      day (fb58)
00408a7a   f85c                  3           5      hour (fb54)
00408aae   f85c                  2           6      minute (fb50)
00408ae2   f85c                  2           6      second (fb4c)
00408b16   f85c                  5           3      day of week (fb48)
00408c05   f85c                  4           4      UPD month
00408c8f   f85c                  3           5      UPD day
00408d1a   f85c                  3           5      UPD hour
00408da5   f85c                  2           6      UPD minute
```

These four values are now proven as the `UPD` record's month/day/hour/minute
fields. In case 5, `FUN_004129c0` opens `weather.dat`; three successive
`FUN_0040b5e0` calls leave the third source line in the shared text buffer.
`FUN_00429e60` uppercases that line, `FUN_0040c720` extracts each value between
`<` and `>`, and `FUN_00417d00` converts it to an integer. The serializer then
reuses the cursor returned by each extraction and appends the four values in
order. The archived fixture confirms the exact source line:

```text
UPD           <20>        <03>month,<08>day,<07>hour,<41>minute;
```

The four append sites therefore encode `03` with 4 bits, `08` with 5 bits,
`07` with 5 bits, and `41` with 6 bits. The later `GetLocalTime` call occurs
after these appends and belongs to the subsequent serializer work, so it does
not change the `UPD` mapping.

### Non-Clock Dispatch and Dynamic Record Tables (2026-09-27)

Raw disassembly resolves the dispatch before the case-5 clock branch:

- At `0x00406e41`, the operation selector is copied from local `fb7c` into
  local `f53c`.
- `0x00406e4d` rejects selectors greater than `0x13`.
- `0x00406e60` jumps through the 20-entry table at `0x00409374`.
- Selectors `0..9` target `00406e67`, `00406efb`, `00406f52`, `00406fa9`,
  `00407000`, `00407057`, `0040709c`, `00407890`, `00407df1`, and
  `004081b2`.

Selectors `0..4` are header-producing operations. Each packs ten successive
8-bit values with `FUN_0040d340`; their source scratch arrays begin at locals
`fca4`, `fca3`, `fca2`, `fca1`, and `fc9f`. This explains why case 5 reuses
an existing ten-byte header instead of constructing bytes `0..9` itself.
Selector 5 enters at `0x00407057`, obtains local time, and rejoins the
shared path at `0x0040849c`; the later `0x004084d9` test selects the clock and
`UPD` append work. It is not the operation that names the weather records.

Selectors `6..9` are the non-clock record-processing families. They dispatch
again through nested tables at `0x004093c4`, `0x004093f4`, and `0x0040940c`,
then repeatedly call the BitPacker thunk with widths from local width tables.
The inputs are converted record text, not direct `WeatherData` fields. Their
output joins the same working buffer before the common `0x80`-bit write at
`0x0040902d`.

The serializer initializes these tables at the beginning of
`FUN_00401400`:

```text
local f4f4..f4c8: 9, 9, 7, 11, 2, 8, 0, 5, 7, 8, 5, 9
local f528..f4f8: 9, 9, 7, 8, 0, 5, 7, 7, 8, 0, 5, 7
local f55c..f534: 8, 0, 5, 7, 11, 11, 7, 8, 5, 5, 8
```

The first two rows are 12-entry width families; the third is the daily
family used by nested forecast branches. The tables alone do not carry record
names. Selector 6's specific record names can, however, be established from
its pre-skip and the ordered `DAY1` fixture below.

A separate 27-entry selector table at locals `f634..f5c8` is initialized as
six zero entries followed by pairs `1,1`, `2,2`, through `10,10`, with a
final zero sentinel. The non-clock branches use it to choose the current
record group or daily block while iterating parsed records. This proves the
native data path groups daily records, but not which four named records belong
to each group.

The named-record lookup is dynamic. `FUN_00412fb0` supplies key storage with
a seven-byte stride and calls `FUN_004128a0` to find each key. It reads five
keys for the current block and four keys per daily block, up to seven days.
`FUN_0040b5e0` advances the shared record iterator and copies the next line
into scratch storage; `FUN_0040b730` tests the record object's `+0x30` slot
for `-1`; `FUN_0040b770` forwards through the record object's virtual table;
and `FUN_0040b7c0` returns the record-data pointer at `+4`. Only `TEMPH` is
present as a useful embedded key string. The remaining keys are loaded or
constructed at runtime, so static string search cannot complete the mapping.

This proves the non-clock dispatch and dynamic grouping mechanism. Selector
6's field names, widths, and logical bit offsets are mapped below. Other
selector families and the semantic meaning of the 40-bit prefix remain
unresolved.

### Header Source and Record Mapping Proof (2026-09-27)

The dispatcher source blocks are now resolved further. Before selector dispatch,
the native code reads a line into a scratch buffer, takes the text after its
first semicolon with `FUN_004105e0`, and writes that value into a zero-filled
40-byte local table at `ebp-0x364` through `ebp-0x33d`. The table is therefore
not a guessed constant header; it is derived from the parsed `weather.dat`
record line.

The five header-producing selectors use the following byte sources, all packed
with ten calls to `FUN_0040d340` at width 8:

```text
selector 0: converted value at ebp-0x7a4, then table[0..8]
selector 1: table[7..16]
selector 2: table[15..24]
selector 3: table[23..32]
selector 4: table[31..39]
```

The first four ranges are directly visible in the address calculations at
`0x00406edd`, `0x00406f34`, `0x00406f8b`, `0x00406fe2`, and `0x00407039`.
The apparent overlap between selector 0 and selector 1 is intentional: case 0
uses a separately converted value for its first byte, while its remaining nine
bytes and the other cases consume the rolling 40-byte table. This proves the
header construction mechanism and byte order, but not the semantic label of
each table byte.

The parser's semantic record names are now proven from the executable's
embedded constants and seven-byte key layout. The five current keys are:

```text
TEMP, WS, HUM, TEMPH, TEMPL
```

The four keys repeated for each daily block are:

```text
TEMPH, TEMPL, WS, HUM
```

`FUN_00412fb0` searches those keys with `FUN_004128a0`, converts their values,
and combines five current values followed by four values for each daily block,
up to seven blocks. The constants are visible at `0x004478c0`
(`TEMPL`), `0x004478c8` (`TEMPH`), `0x004478d0` (`HUM`),
`0x004478d4` (`TEMP`), and `0x00447330` (`WS`). This proves the parser record
mapping, independent of archive record order.

### Current-Condition Sender Contract Still Open

Further bytecode tracing narrows, but does not close, the normal current-send
path. `FUN_004105e0` copies the substring after the first semicolon from its
input line and terminates it at the next semicolon or carriage return. The
five header selectors `0..4` serialize overlapping ten-byte windows from
this 40-byte table; selector 0 prepends a separately converted byte. The
table's producer is record text, not a direct serialization of `WeatherData`,
and its byte semantics have not been mapped to the archived current fields.

`FUN_00412e60`, called by `FUN_00412fb0`, computes a piecewise
temperature-like float from five parsed current inputs and a validity flag.
When the flag is set it returns the native sentinel global; otherwise it
applies a formula using binary constants and branches on the first input.
This is more than a direct copy of current temperature, humidity, and wind
speed. The public `WeatherData` contract lacks the full five-input record set,
and `weather_parse_legacy_current` intentionally does not map `BTEMP`, `WEA`,
`TREND`, `BFT`, `WW`, `VIS`, `UVI`, or `DEWP` into native current-wire values.

Therefore the current-only `usb_send_weather_data` refusal is intentional:
neither mutating archived records and fuzzing a replacement encoder nor
reusing forecast selector layouts proves the device's current-condition wire
mapping. Removing the refusal requires an original `CAL_USB_WRITE` capture
for the current path, correlated against controlled one-field source changes,
including the header windows. No current-condition differential capture is
available yet.

The 40-bit prefix's semantic meaning and a few selector-6 value conversions
remain open. The selector-6 field positions and widths are resolved below
from the stream skip, ordered fixture, append calls, and checksum boundary;
they do not depend on a runtime byte-difference capture.

### Archive Fixture Cross-Check (2026-09-17)

The archived import fixture `C:\Users\rmonn\Desktop\www.weather-life.com\update\city\06344.csv`
was compared with its `.csv.bak` copy and the compact
`update\city1\06344.csv` form. All three preserve the same current-condition
record order:

```text
TEMP, BTEMP, WEA, PRE, TREND, WS, BFT, WW, HUM, VIS, UVI, DEWP
```

The fixture also confirms the documented widths for these records (`UPD=20`,
`WEA=6`, `TREND=2`, `WS=6`, `BFT=6`, `WW=4`, and `UVI=5`). The `UPD` line's
four component widths add to 20 bits and match the four case-5 append widths
exactly, providing an independent archive-to-assembly cross-check.

The serializer sets selector 6's primary segment length to `0x80` bits. The
archived service-record widths are not the native output widths: selector 6
uses its own per-slot conversion and append lengths. Its exact block is mapped
below. A separate `0x40`-bit path exists; its full semantic field list remains
unresolved.

### Selector-6 Day-1 Field Layout (2026-09-27)

This mapping is established by four independent parts of the executable:

- The input stream is rewound; the first line supplies the city header, then
  `DES` and `CITY_AND_WMO` are consumed. The stream is positioned at `UPD`.
- For selector 6, `local_638[6]` is 1. `FUN_0040d850` skips the current
  blank-separated record group. `local_6a8[6]` is 0, so no additional records
  are skipped. The loop ignores the `DAY1 yyyymmdd` line because it contains
  no `<...>` value and starts at `TEMPH`.
- The selector-6 switch increments `local_534` once per record. Its twelve
  cases match the ordered Day-1 fixture records through `AFHUM`; case 6
  consumes `HUM` but appends zero bits.
- The append widths total 79 bits. With the cursor at logical bit 40, the
  fields end at bit 119. One pad bit completes the first 120 data bits; the
  checksum helper then appends the 8-bit check value.

Offsets below are zero-based logical bit offsets in the decoded 16-byte
segment. The packer is MSB-first. In the HID working buffer, each logical
nibble is stored in one byte with a `0x10` prefix, so physical cell 10 / mask
index 4 corresponds to logical bit 40.

| Slot | Day-1 record | Start bit | Bits | Native conversion/append behavior |
| ---: | --- | ---: | ---: | --- |
| 0 | `TEMPH` | 40 | 9 | Signed decimal integer plus suffix-derived increment; `N...` maps to `0xff`; encoded as 9-bit two's complement |
| 1 | `TEMPL` | 49 | 9 | Provider-derived float truncated toward zero by `__ftol`, then encoded by the `TEMPH` path |
| 2 | `MORING_ICON` | 58 | 7 | Matching index in either of two 79-name tables; unknown maps to 79 |
| 3 | `WS` | 65 | 11 | Text after `.` is discarded; signed integer is packed modulo 11 bits |
| 4 | `WBFT` | 76 | 2 | Signed integer, or leading `-`/`N/A` as `0xff`; low 2 bits are packed |
| 5 | `WW` | 78 | 8 | Text after `.` is discarded; signed integer is packed modulo 8 bits; no missing-value sentinel |
| 6 | `HUM` | 86 | 0 | Record consumed, no bits appended |
| 7 | `AFTERNOON_N` | 86 | 5 | Compass alias index `0..7`; unknown maps to 8 |
| 8 | `AFWS` | 91 | 7 | Signed integer, or leading `-`/`N/A` as `0xff`; low 7 bits are packed |
| 9 | `AFBFT` | 98 | 8 | `integer_part * 10 + parsed_suffix`, upper-clamped to 254 |
| 10 | `AFWW` | 106 | 5 | Signed integer, or leading `-`/`N/A` as `0xff`; low 5 bits are packed |
| 11 | `AFHUM` | 111 | 8 | Rounded signed magnitude: sign flag in bit 7, positive magnitude capped at 127 |

The fields occupy logical bits 40..118 (79 bits). Logical bit 119 is padding,
and logical byte 15 is the checksum. `FUN_0040bc70(buffer, ..., 0x80)`
reconstructs the first 15 logical bytes from 30 nibble cells, computes the
table-based check, and stores it in cells 30 and 31. The 128-bit segment is
therefore 16 logical bytes represented by 32 nibble cells. `CAL_USB_WRITE`
copies 16 cells per call, so this segment is sent in two writes at buffer
offsets 0 and 16. The separate `0x40`-bit path occupies 16 nibble cells and
uses one write.

The first ten nibble cells (40 logical bits) are copied from the preceding
`CAL_USB_READ` result. Selector 6 preserves those cells, ORs `0x07` into cell
1, resets following cells to the `0x10` nibble-cell marker, then appends the
Day-1 fields beginning at bit 40. The response dispatcher uses normalized
nibble cell 1 directly (`local_310._1_1_ & 0x0f`), not a reconstructed logical
byte, to select response handlers.

One response state has a fully proven prefix interpretation. The registration
acknowledgment check requires normalized cells 0 and 1 to be `0` and `9`, then
compares cells 2 through 11 against the ten generated registration-ID
nibbles. If this acknowledgment is the readback that seeds a selector-6
frame, its copied prefix is cell 0=`0`, cell 1=`0x0f` after the `|= 0x07`
operation, and cells 2 through 9 equal registration-ID nibbles 0 through 7.
Those eight ID nibbles plus the two control cells make the 40-bit prefix; ID
nibbles 8 and 9 are beyond the prefix and are overwritten by the weather
fields. This mapping is conditional on the readback being that verified
registration acknowledgment, not a universal meaning for every prefix.

The native C primitives mirror the proven portion of this path:
`weather_frame_begin_native_selector6` normalizes/copies the ten prefix cells,
applies the cell-1 OR, and initializes a 32-cell buffer;
`weather_frame_append_native_selector6_fields` appends the exact 79-bit field
order from preconverted values; and `weather_frame_finalize_native_selector6`
adds the zero pad and the 15-byte XOR-of-table checksum. The named one-shot
test mode sends two 16-cell writes. `weather_sender_build_weather_segment`
now performs the recovered named-record conversions for selector 6 and
selectors 8..17, including icon/direction lookups, sentinels, decimal handling,
and the paired later-day blocks. The response parser allows up to 32 records
per day so it can retain the complete 24-record forecast group.

`weather-life-discover --serve-weather <legacy-weather-file> <seconds>` is an
explicit opt-in live loop. It polls every 130 ms, answers only those supported
forecast selectors, sends each 128-bit segment as two 16-cell writes 2 ms
apart, and stops on a malformed response, missing/invalid requested data,
partial write, or four consecutive read failures. It does not synthesize
missing records, log readback payloads, register the device, recreate the
unresolved startup initialization field, or automatically reconnect. Selector
1 (city exchange), selector 7 (a distinct eight-record path), current
selectors 0..5, and selectors 18/19 remain unanswered. The current-condition
`usb_send_weather_data` API remains refused: a forecast segment is not a
complete renderer or proof of display acceptance. No physical transmission
was performed while implementing this source-text path.

For the separately observed idle response, all normalized cells are zero, so
the emitted prefix cells are `0, 7, 0, ...` after cell 1 is modified. The
weather-write-associated response with cell 1=`0x0b` has no explicit
dispatcher handler; if it seeds selector 6, the emitted cell 1 is proven to
be `0x0f` after the OR operation. The meanings of its remaining cells, and
their interpretation when carried into a selector-6 frame, remain unknown.

The original weather path's HID control selector is statically resolved to
zero; it is distinct from the weather-record dispatcher selector 6. At
`usbwr.exe:0x4034e1`, the caller initializes its retry byte to numeric `1`
and passes it as the first argument to the resolved `CAL_USB_READ` pointer.
Retries increment that byte through numeric `4`. The decompiled export has
signature `CAL_USB_READ(char selector, int buffer)` and forwards `selector` to
`FUN_10001ab0`. That helper initializes control-header byte 8 to zero and
overrides it only when the argument equals ASCII `'0'`, `'1'`, or `'2'`.
Numeric retry bytes `1..4` match none of those values, so the original path's
on-wire HID selector is zero, including retries. This resolves which read
response seeds the prefix; response-state-specific contents are described
above.

An idle-device read-only capture on 2026-10-09 sent operation `1`, length
`0x10`, with normalized control selectors `0`, `1`, and `2`. Each feature
response was report ID `00` followed by sixteen `10` nibble cells. The
low-nibble-normalized contents were therefore all zero for these reads. This
matches the selector-0 response used by the original weather path. The
selector-1 and selector-2 reads were separate comparison probes; they also
returned the marker-only response. This is a baseline observation, distinct
from the verified registration-ack state above. No write operation was issued
during the capture.

### Authorized Selector-6 Hardware Test (2026-10-10)

After the one-shot registration probe matched its generated acknowledgment, a
separate selector-6 test was built from the subsequent idle readback (sixteen
`0x10` cells, normalized to zero). Numeric field values came from the archived
Rotterdam `DAY1` fixture. Because the icon ordinal was not recovered, the
documented unknown-icon result `79` was used; `AFTERNOON_N` likewise used the
unknown-direction result `8`. This was an exploratory segment, not an exact
encoding of every archived string value.

The complete checksummed 32-cell segment was:

```text
00 07 00 00 00 00 00 00 00 00 10 13 17 1f 12 17
18 10 19 18 10 11 10 15 10 18 18 10 1a 14 1c 16
```

An independent calculation gave checksum `0xc6`, stored in cells `1c 16`.
The replacement sent this as two 16-cell writes, then polled six times at
100 ms intervals. Every response remained sixteen `0x10` cells; there was no
response transition or acknowledgment. This does not prove whether the device
accepted or rendered the frame. No second weather write was sent.

A separate one-shot test used the exact 16-byte payload from the original
application's captured `CAL_USB_WRITE` report:

```text
10 14 10 10 10 10 10 10 14 10 10 10 10 12 19 1b
```

The idle read was sixteen `0x10` cells. The 100 ms post-write read was
`10 1b 18 10 10 10 10 10 14 10 10 10 10 12 19 1b`; after a later read-only
poll, the response had returned to sixteen `0x10` cells. The user observed no
visible display change. This reproduces a transient response beginning with
normalized cells `0, 0x0b`, but gives no positive evidence of display
acceptance. The response's protocol meaning and the cause of the unchanged
display remain unresolved.

### Bounded Physical Selector-6 Transmission (2026-10-11)

With explicit authorization, the Linux client reopened the same Tenx VID/PID
(`0x1130:0x0202`, payload interface `/dev/hidraw2`). A generated 17-byte
registration-handshake payload received a matching acknowledgment. The client
then sent the checksummed selector-6 segment shown above as two 16-cell HID
writes with 2 ms pacing. Six polls at 100 ms intervals each returned sixteen
`0x10` cells. The HID calls completed, but there was no response-state change
or device acknowledgment. This confirms delivery to the USB/HID write path
only; it does **not** prove firmware acceptance or LCD rendering. The available
camera was not aimed at the display, so no visual observation was made and no
image was retained. Do not describe this test as a successful display update.

Conversion details confirmed from the selector-6 switch and helper bodies:

- The decimal integer helper accepts an optional sign and parses leading
  digits. `TEMPH` and negative `AFHUM` separately parse the substring after
  `.` as an integer and increment the magnitude when that parsed value is at
  least 5. This is conventional rounding for the one-digit suffixes in the
  fixtures, but multi-digit suffixes are compared as a whole integer.
- `TEMPH` encodes positive magnitudes directly and negative magnitudes as
  `0x100 - magnitude`, with the sign bit, yielding a 9-bit two's-complement
  value. Negative zero is encoded as zero; an input beginning with `N` maps
  to `0xff`. `TEMPL` converts its provider-side float with `__ftol`, formats
  that integer, and then follows the same path. The helper sets the x87
  rounding-control bits to `11` for `FISTP` and restores the prior control
  word, so conversion truncates toward zero.
- The 79-icon lookup searches two parallel name tables and returns the
  matched position, independent of which table matched. No match returns 79.
- `AFTERNOON_N` returns the position in one of two eight-entry direction
  alias tables. Their orders are `N, WN, W, SW, S, OS, O, NO` and
  `N, NW, W, WS, S, SO, O, ON`; no match returns 8. The aliases share
  ordinal positions, progressing around the compass from north through west,
  south, and east.
- `WS` and `WW` discard the decimal point and everything following it before
  integer parsing. The bit appender takes the requested low bits; neither
  field clamps the parsed value. `WBFT`, `AFWS`, and `AFWW` explicitly map
  any input beginning with `-`, and `N/A`, to `0xff` before taking 2, 7, and
  5 bits respectively. Thus negative numerics in those fields use the
  all-ones marker rather than signed numeric encoding.
- `AFBFT` parses the integer part and the substring after `.` separately,
  computes `integer_part * 10 + suffix`, and caps values above 254 at 254.
  `AFHUM` uses the temperature-style suffix increment; negative values set
  bit 7 and retain the low seven magnitude bits, while positive values cap at
  127. It has no explicit `N/A` mapping.

### Later-Day Forecast Selector Families (2026-10-10)

The initialized selector tables and nested switch prove the later-day mapping:
`local_638` assigns group skips 1 through 7 to selector pairs `6/7` through
`18/19`. For pairs from `8/9` onward, `local_6a8` assigns record skips `1`
and `14`. `FUN_0040d850` skips blank-separated groups; `FUN_0040dad0` skips
individual lines. Against the archived fixture, an even selector skips the
`DAYn` header and starts at `TEMPH`; its paired odd selector skips that header
plus the first 13 records and starts at `NWS`.

| Day | First-part selector | Second-part selector | Group skip | Record skip |
| ---: | ---: | ---: | ---: | ---: |
| 2 | 8 | 9 | 2 | 1 / 14 |
| 3 | 10 | 11 | 3 | 1 / 14 |
| 4 | 12 | 13 | 4 | 1 / 14 |
| 5 | 14 | 15 | 5 | 1 / 14 |
| 6 | 16 | 17 | 6 | 1 / 14 |
| 7 | 18 | 19 | 7 | 1 / 14 |

Selectors `8, 10, 12, 14, 16, 18` share one 13-slot encoder. The archived
record order aligns those slots through `NIGHT_ICON`; the initialized native
widths and special cases are:

| Slot | Record | Bits | Conversion |
| ---: | --- | ---: | --- |
| 0 | `TEMPH` | 9 | Rounded signed temperature |
| 1 | `TEMPL` | 9 | Provider float truncated toward zero, then the temperature encoder |
| 2 | `MORING_ICON` | 7 | 79-name icon index; unknown is 79 |
| 3 | `WS` | 8 | Parsed integer |
| 4 | `WBFT` | 0 | Consumed, not appended |
| 5 | `WW` | 5 | Direction alias index; unknown is 8 |
| 6 | `HUM` | 7 | Numeric/sentinel conversion |
| 7 | `AFTERNOON_N` | 7 | 79-name icon index; unknown is 79 |
| 8 | `AFWS` | 8 | Numeric/sentinel conversion |
| 9 | `AFBFT` | 0 | Consumed, not appended |
| 10 | `AFWW` | 5 | Direction alias index; unknown is 8 |
| 11 | `AFHUM` | 7 | Numeric/sentinel conversion |
| 12 | `NIGHT_ICON` | 7 | 79-name icon index; unknown is 79 |

The first-part widths total 79 bits, so they occupy logical bits 40..118;
bit 119 is zero padding, followed by the checksum byte at bits 120..127.
Selectors `9, 11, 13, 15, 17, 19` share an 11-record encoder starting at
`NWS`:

| Record(s) | Bits | Conversion |
| --- | ---: | --- |
| `NWS` | 8 | Parsed integer |
| `NBFT` | 0 | Consumed, not appended |
| `NWW` | 5 | Direction alias index; unknown is 8 |
| `NHUM` | 7 | Numeric/sentinel conversion |
| `SUNR` | 11 | Hour (5) and minute (6), parsed as separate components |
| `SUNS` | 11 | Hour (5) and minute (6), parsed as separate components |
| `PPCP` | 7 | Parsed integer |
| `RAINMS` | 8 | Rounded numeric suffix conversion |
| `SUNTIME` | 5 | Parsed integer |
| `UVI` | 5 | Parsed integer |
| `DEWP` | 8 | Numeric/sentinel conversion |

The second-part widths total 75 bits, occupying logical bits 40..114; bits
115..119 are zero padding and bits 120..127 hold the checksum. Both parts use
the same 40-bit readback prefix and the same 128-bit finalizer. The native C
builders `weather_frame_build_native_later_day_first` and
`weather_frame_build_native_later_day_second` encode these two proven layouts
from preconverted values. A Day2 golden vector is covered by native tests.

This establishes the archived and parsed forecast range **Day1 through
Day6**. `FUN_00412fb0` produces one current value plus at most six daily
values, and the archive format and C parser recognize `DAY1` through `DAY6`.
Selectors `18/19` follow the same bytecode family with group skip 7, but there
is no Day7 record in the archived fixture or parser contract. Their compiled
branches are not proof that the normal application has Day7 data. Therefore
the proven days after Day1 are Day2..Day6; a seventh forecast day is not
established.

This is a Day-1 morning/afternoon block, not the current-condition block.
The later-day branch layouts are documented below. The current-condition
group remains unresolved. The explicit selector-response loop supports the
verified forecast layouts only; it does not enable the general
`usb_send_weather_data` API or prove that the device renders these segments.

**Still open**: meanings of response-prefix cells for non-registration states,
the selector-1 city exchange, selector 7's distinct eight-record format, the
current-condition group, source data for Day7 despite selectors 18/19, startup
initialization/recovery field extraction, and whether any forecast segment is
accepted or displayed. An original-app differential capture for the forecast
selectors is still unavailable.

### Refresh Capture (2026-09-08)

During a user-triggered refresh, `weather.exe` opened:

```text
http://www.weather-life.com/update/city/06344.csv
```

At the same time, `usbwr.exe` produced two occurrences of the 17-byte frame above. The capture also contained the normal repeated 9-byte polling frame. This establishes the old server endpoint and the temporal association between a refresh attempt and the 17-byte device write, but it does not prove that the server returned weather data or that the device displayed it. The server is now unavailable, so the replacement uses Open-Meteo; do not reintroduce this endpoint into new code.

A controlled replay then served the archived `06344.csv` fixture locally while
preserving the original hostname through the Windows hosts file. The original
application accepted the response and the DeskWeather window displayed data.
This confirms the parser-to-serializer-to-display path. The captured 17-byte
write remains a registration/control frame; it is not sufficient to assign
individual weather fields, so differential fixtures are still required.

The following packet layouts remain historical hypotheses and must not be treated as the live wire format until more traces correlate bytes with a known UI action.

### onlywell.dll Control Flow Confirmed (2026-09-27)

`DeviceIni()` at `0x10001e70` does not transmit an initialization command. It
calls `FUN_10001000(0)`, which enumerates HID interfaces and opens the matching
Usage Page 1 interfaces with Usage 0 and Usage 3. The Usage 3 handle sends
command headers; the Usage 0 handle performs feature-report reads and payload
writes.

`CAL_USB_STATUS()` at `0x10001ea0` calls `FUN_10001970`. That helper sends a
zero-length `CAL_USB_READ` header through Usage 3, then calls
`HidD_GetFeature` through Usage 0 with a 17-byte report buffer. The exported
status function returns byte 0 of the buffer, or `0xff` when the exchange
fails. Static analysis does not establish byte 0 as a device status code or
acknowledgment; do not infer response semantics from the export name.

`CAL_USB_READ(selector, buffer)` requests 16 payload bytes in the weather
polling path. The common `onlywell.dll` helper constructs this exact
zero-report-ID output report:

```
[0]      Report ID: 0
[1..4]   55 53 42 43 (ASCII "USBC")
[5..6]   Transfer length, unsigned 16-bit little-endian
[7]      Operation: 1 = read, 2 = write
[8]      Selector: input ASCII '0', '1', or '2' normalized to 0, 1, or 2
```

The transfer length is the number of data bytes, excluding the HID report-ID
byte. The observed 16-byte read request is therefore
`00 55 53 42 43 00 10 01 00`; a 16-byte write uses
`00 55 53 42 43 00 10 02 00`. `CAL_USB_STATUS()` is a separate zero-length
read operation, `00 55 53 42 43 00 00 01 00`. These examples establish the
selector field's encoding, not what selector values mean to the device.

For writes, `CAL_USB_WRITE` sends the 8-byte header through the Usage 3
interface, then sends the 16-byte payload through Usage 0 as a separate
17-byte output report: report ID `00` followed by the unchanged payload. For
reads, the same header is sent through Usage 3, then `HidD_GetFeature` requests
17 bytes through Usage 0: report ID `00` plus 16 response bytes. The exported
read wrapper removes the report-ID byte and copies response data only up to
the first zero byte, so its API is string-like rather than binary-safe.

`DeviceIni()` only enumerates and opens the two HID interfaces. There is no
evidence for the previously proposed generic command-plus-length packet,
status-byte response, or byte-oriented temperature/humidity payload. The
weather serializer's first 40 logical bits are still semantically unresolved;
they must not be inferred from this separate HID control header.

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
│   USB Hardware (Tenx HID 0x1130:0x0202)│
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
current fields such as `TEMP`, `PRE`, `WS`, `HUM`, and `DEWP`. The current
`RAINMS` record is rainfall in millimetres and is now parsed into
`WeatherData.precipitation`. Forecast records
are grouped under `DAY1` through `DAY6` and include `TEMPH`, `TEMPL`, icon,
wind, humidity, precipitation, sunrise, sunset, and UV fields. The parser in
`python/legacy_weather_csv.py` preserves these expressions without assigning
new units or meanings.

The native parser in `src/weather_response.c` maps the verified current numeric
records into `WeatherData`: `TEMP` to Celsius, `HUM` to percent, `WS` from
km/h to m/s, `PRE` to hPa, and `RAINMS` to millimetres. It deliberately leaves `WEA` and forecast icon
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
- **API**: hidapi
- **Device Discovery**: hid_enumerate(), filtered by Usage Page 1 and Usage 0/3
- **Communication**: hid_write() for Usage 3 command reports and Usage 0 payload reports; hid_get_feature_report() for reads
- **Permissions**: /etc/udev/rules.d for device access
- **Status**: Implemented using the shared hidapi transport

### macOS Implementation
- **API**: hidapi
- **Device Discovery**: hid_enumerate(), filtered by Usage Page 1 and Usage 0/3
- **Communication**: hid_write() for Usage 3 command reports and Usage 0 payload reports; hid_get_feature_report() for reads
- **Status**: Implemented using the shared hidapi transport

## Implementation Roadmap

Status updated 2026-10-11. “Implemented” means code exists; it does not imply
that the device accepted or displayed the data. Hardware writes remain
experimental unless specifically described below.

### Phase 3: Core transport and protocol
- [x] Implement Windows, Linux, and macOS HID backends and shared HID framing.
- [x] Implement native registration, MSB-first bit-packing, checksumming, icon
  lookup, and offline builders for the proven selector-6 and later-day layouts.
- [x] Port the verified startup/recovery init-frame builder, 64-bit segment
  checksum finalization, periodic control-frame field layout, and bounded
  read-retry/recovery state transitions as offline native APIs.
- [x] Add native C and Python unit suites; the current Meson test run passes
  both suites.
- [x] Verify Linux HID discovery and a read-only device poll; the observed
  response contains sixteen `0x10` cells.
- [x] Cross-compile and run the documented pure-C tests with MinGW and Wine.
- [ ] Verify native build, discovery, and read-only I/O on Windows and macOS.
  The Windows HID backend and macOS backend have not been validated end to end.
- [ ] Integrate the recovered session primitives with the live transport:
  130 ms polling, 3-second recovery delay, `DeviceIni`, init-frame resend,
  periodic control scheduling, and request dispatch. Keep this gated until
  selector handlers use real parsed records instead of fixture values.
- [ ] Replace the fixture-only listener with the recovered request-driven
  sender. Cover selector 1 and every supported weather selector, including
  dynamic record selection, chunk ordering, and timing. Keep any selector
  unsupported only if its executable behavior cannot be recovered.

### Phase 4: Weather and device integration
- [x] Implement the Open-Meteo provider and city geocoding in
  `python/weather_provider.py`.
- [x] Parse archived named-record city responses and map verified current
  numeric fields into the native `WeatherData` structure.
- [x] Keep candidate serialization separate from proven native layouts; the
  generic weather-frame builder and the selector-6 fixture values are not a
  production Open-Meteo-to-device conversion.
- [ ] Recover and port the original executable's conversions from named
  weather records into wire values, including weather/icon codes, units,
  rounding, sentinels, city data, and time fields. The source is the installed
  `usbwr.exe` (and its companion data files); use controlled archived fixtures
  and original-process traces to validate the recovered transformations.
- [ ] Connect the provider and parsed values to the recovered complete
  device-session update. Build golden tests against original executable
  outputs before using live weather data.
- [ ] Add persistent location/configuration and a unified refresh CLI. The
  provider has its own command-line options; the current native executable is
  a discovery/experiment tool, not an integrated weather application.
- [ ] Add systemd and macOS LaunchAgent integration if a continuously running
  application remains in scope.

### Phase 5: Validation and release
- [x] Add targeted native and Python unit tests.
- [ ] Add cross-platform build/test coverage and hardware acceptance checks.
  Transport success must not be reported as LCD acceptance.
- [ ] Refresh the user-facing README and write accurate installation and
  operation guides; existing project-level setup instructions are stale.
- [ ] Add CI for supported build and test targets.
- [ ] Define and produce installable release packages.

## Security Considerations

- **No authentication**: Original device has no security model
- **USB access**: Regular user (Linux group membership)
- **Weather data**: HTTPS only for API calls
- **Configuration**: User-owned files in home directory

## Limitations & Known Issues

1. **Host implementation**: The verified init/control frame builders,
   checksums, and read-retry/recovery transitions are ported offline. Live
   lifecycle integration, record conversions, and complete request dispatch
   remain unfinished.
2. **LCD acceptance**: The executables expose the host-to-HID behavior, not
   firmware rendering or a display-success response; physical acceptance
   remains a separate validation item.
3. **Device scope**: The replacement targets the confirmed Tenx VID/PID
   `0x1130:0x0202`; other variants are not currently supported.
4. **Original API**: `weather-life.com` is defunct; Open-Meteo is implemented
   as the replacement provider.
5. **Update cadence**: The decompiled host has a 130 ms poll and an
   approximately 57-minute idle time-sync interval; weather refresh behavior
   still depends on the original app/session triggers.

## Tools & Resources

- **Ghidra**: Binary analysis and decompilation
- **Binary Analysis Results**: C:\Users\rmonn\weatherlife.rep
- **Website Backup**: C:\Users\rmonn\OneDrive\Backup\www.weather-life.com\
- **Original Binaries**: C:\Program Files (x86)\Weather\
- **Python Scripts**: USB protocol extraction and testing

## Next Steps

1. **Finish executable-derived behavior recovery.** Resolve selector-1
   responses, dynamic record selection, provider-to-native conversions, and
   the complete session loop. The init/control frame layouts and bounded
   retry/recovery transitions now have offline implementations. Use original
   binaries and controlled captures rather than treating hardware display
   acceptance as a prerequisite.
2. **Integrate and verify the host sender offline.** Connect the recovered
   builders and state transitions to the USB callbacks after real record
   parsing and selector handling are available. Compare generated 16-cell
   writes, order, and timing with original-executable captures, and add those
   captures as fixtures/tests.
3. **Connect Open-Meteo to the recovered model.** Convert provider values
   using the proven original rules, then generate the same native records and
   session writes. Keep candidate encodings isolated until their output
   matches the original executable.
4. **Validate physical display behavior separately.** Host-side weather-to-
   HID bytes are recoverable from the executables and can be validated without
   a rendering result. Whether the firmware accepts or visibly renders those
   bytes remains a separate hardware/firmware validation question; do not
   conflate it with completing the host-side implementation.
5. **Complete cross-platform and release work.** Exercise native Windows,
   Linux, and macOS builds and discovery, then refresh setup documentation,
   CI, and packaging.

---

**Protocol confidence**: Transport framing, registration handshake, native
weather segment boundaries, bit-packing, checksums, and the selector-6 and
later-day field layouts and conversions for selectors 6 and 8..17 are
statically verified and implemented. The current-condition sender, selector 1
and 7 paths, startup control-data extraction, and device-side
LCD acceptance/rendering remain unresolved. LCD acceptance is a separate
unproven question and does not block reconstructing the host-side forecast
response byte stream.

**Implementation status**: Cross-platform HID backends, read-only discovery,
verified protocol primitives, offline native segment builders, selector
record-to-frame conversions, an opt-in forecast selector responder, and an
Open-Meteo provider exist. The general `usb_send_weather_data()` API
deliberately refuses transmission because it accepts only current
`WeatherData`, not the full selector-driven native forecast response. Do not
treat a successful HID write or transient response as proof that the display
changed.

### Icon Tables and Selector-1 Handler (2026-10-10)

- `usbwr.exe!FUN_00410950` builds two 79-entry stack arrays of 6-character
  icon names from the `.rdata` strings at `0x447358..0x447837` (copied in
  descending source order, so index 0 of the first array is `SO____` at
  `0x447830` and its index 78 is `WBGR2_`; index 0 of the second is `MO____`
  at `0x4475b8` and its index 78 is `MBGR2_`). It compares with the
  case-sensitive CRT `strcmp` (the service line is uppercased earlier),
  searches the first array, then the second, and returns the position or 79.
  Implemented in `src/weather_icons.c` and `python/weather_icons.py` with
  golden tests. The 157 files in `images/icons` match these names, which is
  independent corroboration that the names are real icon identifiers.
- The selector-1 handler (`0x004053f4`-`0x00405f3b`) is not a current-condition
  writer. It reads a 5-character code from the dongle response, looks it up in
  `reg.dat`, and answers with an 80-bit block (20+2+2+8+20 bits, two
  `CAL_USB_WRITE` chunks). Its field meanings are undecoded; it looks like a
  city/registration exchange.
- Still unproven: that the dongle LCD shows the icon for a given index, and
  what causes the dongle to request selector 6. No LCD mapping exists in the
  host binaries; it is firmware-side. No current-condition-specific block was
  found: the selector-6 Day-1 block carries the current temperature/icon
  fields.

## Full Headless Decompilation of onlywell.dll Transport (verified)

Whole-binary Ghidra dumps of `onlywell.dll`, `usbwr.dll` and `usbwr.exe` were
compared with `src/hid_transport.c`:

- Command header `55 53 42 43 | len_hi | len_lo | type | mode` (type 1 = read,
  2 = write, status = `00 00 01 00`); `mode` maps from selector chars
  `'0'/'1'/'2'`, otherwise stays 0. `usbwr.exe` passes numeric 1..4 (a retry
  counter), so mode is always 0 on the wire. Our framing is identical.
- The header goes out on the 8-byte-output interface (usage 3); payloads go out
  on the 16-byte-output interface (usage 0). `CAL_USB_WRITE` always sends 16 bytes;
  `CAL_USB_READ` issues the header, then `HidD_GetFeature` on the usage-0 handle
  with len+1 bytes. Matches the HID report descriptors read from sysfs.
- `usbwr.exe` startup: `DeviceIni`, `CAL_USB_READ`, retry selector 1..4 on
  failure, then re-send the nibble-packed registration (100 ms between 16-byte
  chunks) until a read returns byte0 = 0, byte1 = 9 and 10 nibbles equal the
  generated id. Weather chunks are written with 2 ms spacing.
- Conclusion: transport framing, sizes and timing are not the reason the display
  stayed unchanged; the sweep and fuzz writes produced no visible change.

## Post-Registration Session Loop in `usbwr.exe!FUN_00401400` (decompiled, not yet implemented)

Read from the headless dump (`usbwr.exe.c`, lines ~7636-8339). Confidence: high
for structure, unproven on hardware. Earlier sweep/fuzz writes (`--sweep-writes`,
`--fuzz-writes 150`) produced no visible display change; they never followed
this session order.

Original session order after registration:

1. **Init frame** (sent once after start and after every failed
   `CAL_USB_READ` -> `Sleep(3000)` -> `DeviceIni` recovery). A 100-byte buffer
   is filled with `0x10`; `byte[1] |= 0x0A`; cursor starts at byte 2, bit 4.
   `FUN_0040d340` packs 8 bits from the reg.dat-derived nibble data
   (`local_430+3`, starting at source bit 4) and 1 session-flag bit
   (`local_71c`, initially 1); declared length `0x40` bits;
   `FUN_0040bc70` finalizes (checksum/nibble pack); one `CAL_USB_WRITE`.
2. **Poll**: `Sleep(0x82)` (130 ms), `CAL_USB_READ`, up to 4 retries.
   `FUN_0040ce60` masks each response cell to its low nibble.
3. **Periodic control frame**: the host copies the current response prefix,
   sets `cell[1] |= 4`, fills cells 8 onward with `0x10`, and starts at
   cursor (7,7). It appends a 3-bit mode/flag value, parsed fields of widths
   4/3/3/2, and the 1-bit `local_71c` flag; checksum finalization produces a
   64-bit segment and one 16-cell write. This frame is emitted after a
   `weather.dat` modification and on an approximately 57-minute interval.
   The four parsed values' meanings and conversions remain unresolved; this
   is not established as a wall-clock time-sync frame.
4. **Selector 1 / 6 / 8**: city handshake, Day-1 and later-day answers
   (documented above).

The packer `FUN_0040d340` stores data in each byte's low nibble (`0x10`
filler kept in the high nibble); `FUN_0040cee0` is the nibble-array variant.

The init frame, generic 64-bit checksum placement, periodic control-frame
layout, and retry/recovery state transitions are implemented offline in
`src/weather_session.c`. The init builder takes the already-packed 8-bit reg.dat-derived field; the
periodic builder takes the field values after the original host's record
parsing. The periodic frame's cursor and field widths
are recovered, but those fields' meanings and source conversions are not.
Neither builder is connected to live I/O, and no frame should be described as
display-accepted. The next work is to port record parsing and selector
dispatch, then integrate the tested lifecycle with the USB callbacks.

### Registration Plus 60 s Serve Attempt (2026-10-11)

After a confirmed registration probe, `--serve-weather` ran for 60 s against the
installed `weather.dat` (Rotterdam; its first `Netherlands;Rotterdam` line had to
be stripped because the parser rejects it). The dongle's readback stayed idle
and it never requested a selector, so nothing was answered. The startup init
frame was not sent: its 8-bit field comes from an unresolved `reg.dat`
extraction (`reg.dat` holds `<c48eb> <0> <01> <0>`), and guessing it is not
permitted. Display acceptance remains unproven.

## Init-Field Derivation and Registered Serve Run

Decompiling `usbwr.exe` (`FUN_0040c720`, `FUN_0040cb50`, `FUN_0040d340`) shows
the init frame's control field is the last two hex digits of the `reg.dat`
code, packed as nibbles, with session flag 1. `--serve-weather <file> <secs>
[reg-code]` now registers, sends the init frame, then serves selectors.

Physical result: registration acknowledged, init frame written, but the dongle
still only requested selector 0 (not enabled) during a 60 s run. Display
acceptance remains unproven; no camera or user observation of the LCD exists.
Open items: `device.dat`-driven session flag, control-frame fields, and
visual confirmation.

## LCD Capabilities and Header Selector Experiment (2026-10-11)

User observations: the LCD has no city names; it only has fixed segments for
the current temperature and four forecast days, plus the clock and internal
sensor temperature (not host-controllable). Consequences:

- Header selectors 0-4 (ten 8-bit values each, `usbwr.exe` `FUN_00401400`)
  cannot carry display text for this model; their meaning is unproven.
- Only current temperature, four forecast days and (probably via selector 5)
  the clock are relevant; Days 5-7 are likely ignored by this LCD.
- `weather.exe` is ASPack packed (unpacked below, 2026-10-11); it needed
  unpacking before analysis.

Experiment: `weather_frame_build_native_header` answers selectors 0-4 with ten
zero bytes. On hardware the dongle kept re-requesting selector 0 (HID writes
accepted), so the neutral header does not advance the session. Display
acceptance remains unproven; next step is deriving the real selector-0 first
byte (`ebp-0x7a4`) and the table source.

### Selector 0-4 Header Source (2026-10-11, experimental)

`FUN_004105e0` takes the text after the first `;` of the `CITY_AND_WMO` record
and truncates it at the next `;`/CR; `FUN_00429e60` upper-cases it. This 40-byte
table feeds selectors 0-4 (selector 0: length + table[0..8]; selectors 1-4: ten
bytes from offsets 7/15/23/31). `weather_sender.c` now builds these from the
record. A 20 s physical run (HID writes accepted) still saw the dongle request
only selector 0, so acceptance remains unproven. The LCD has no text, so the
table's purpose is unknown.

### weather.exe Unpacked (2026-10-11)

`weather.exe` is a Delphi 32-bit program packed with ASPack (entry in
`.aspack`). It was unpacked offline by emulating the stub in Unicorn with fake
API thunks (no execution of the original program). The stub decompresses CODE
via `VirtualAlloc` buffers and finishes with a "call a patched `ret`" loop
(`call edi` at stub `+0x118`); the true OEP is RVA `0xe0818` (`55 8b ec ...`
Delphi `entry`). The dump then yields 4572 functions that Ghidra decompiles
with 0 failures. Imports are not rebuilt, so API calls show as unresolved
thunks; strings and control flow are intact.

Confirmed from the unpacked code:

- `weather.exe` is the UI/registration shell only. It loads `usbwr.dll` from
  its own directory, resolves `usbdeviceread` (`FUN_004df48c`) and calls it
  with five string arguments; a 5-character result means "device present"
  (`DAT_004e7625`), otherwise the UI shows "No USB Device".
- On registration (`FUN_004df1f8`, notices `Device Register...` /
  `Device Register End`) it writes `Device.dat` containing `device=ON`.
  `usbwr.exe` watches that file (see the `device.dat` mtime watch above).
- The UI persists country/city/area code in `images\cfg`/`reg.ini` and reads
  the city list from `weather.dat`/`weather2.dat`, downloaded from
  `http://www.weather-life.com/update/city/` (dead server). It also references
  `update\update.exe`.
- It contains no HID/USB frame or selector code. The display payload
  therefore remains solely in `usbwr.exe` (already ported), and unpacking
  `weather.exe` does not resolve why the dongle only requests selector 0.

### Request-Type Decode in usbwr.exe (2026-10-11)

In the `usbwr.exe` main loop the dongle's readback is parsed as
`request_type = cell[1] & 0xf`, and the loop only has branches for request
types 1, 6 and 8 (all others fall through with no write). The idle readback
(16 x `0x10`) therefore decodes to request type 0, which the original ignores.
The "selector 0" our serve loop logs is this idle state, not a request for
data. The original only answers when the dongle reports a non-idle request
type; the selector then derives from `((b0 & 3) << 4) | (b1 & 0xf)` of the
request cells. Hypothesis (unproven): the dongle raises a request only after a
physical user action (for example a sync button), so the serve loop should be
run while that action is performed, and answers should be sent only for
request types 1, 6 and 8.

### Live Serve Run After Registration (2026-10-11)

- With the registration code supplied, registration is acknowledged and the init
  frame is accepted; afterwards the dongle returns an all-`0x10` (idle) readback,
  which the serve loop now skips, matching the original ignoring request type 0.
- In one run the dongle then requested selector 11 (readback
  `10 1b 18 10 18 10 10 10 10 10 10 10 10 10 18 1a`). The sender rejected it
  because the legacy time records `<HH>hour,<MM>min` were stored with the minute
  dropped; the parser now normalises them to `<HH:MM>`. Selector 11 could not be
  re-triggered in later runs (the dongle stayed idle for 40 s), so no segment was
  answered live afterwards.
- LCD acceptance remains unproven; only a visual observation can confirm it.

### Fresh Startup Capture and Registered Serve Run (2026-10-18)

- `traces/startup-spawn-20261018.jsonl` (weather.exe spawned under the tracer): `weather.exe` itself sends one registration frame (cell 1 = `8`, CRC over cells 0..13 verified with `python/dongle_protocol.py`), then starts `usbwr.exe`.
- `traces/startup-attach-20261018.jsonl` (both processes): `usbwr.exe` sent one 16-cell frame with cell 1 = `4` (`10 14 10 10 10 10 10 11 10 1d 10 17 1a 16 17 14`), whose CRC verifies; this matches the documented periodic control-frame shape (prefix copied from the idle readback, `cell[1] |= 4`). Over the following ~40 s there were 272 polls, all idle (`0x10`).
- Registered `--serve-weather` run: registration acknowledged, init frame (field `0xeb`) accepted, and the dongle again requested selector 11 right after init (same readback as 2026-10-11, so it is reproducible, not a one-off). Several later readbacks were corrupt: they contained the literal bytes `55 53 42 43 00 10 01 00` (the USBC read header) after 8 cells, i.e. a read that did not return a clean feature report. The serve loop mis-treated them as selector 0 and wrote segments; it should reject any readback with a cell whose high nibble is not `1`.
- `CMakeLists.txt` was missing `weather_sender.c` and `weather_session.c`, so the CMake build of `weather-life-discover` failed to link; fixed.
- Display change was not observed/confirmed.

### Request-Type Gate Re-Read (2026-10-18, usbwr.exe disassembly)

In `FUN_00401400` the readback is decoded into a second buffer (`0x004010c3`, 16 cells); the request switch at `0x00404f9a` runs only when decoded cell 1 is non-zero (idle falls through to the weather.dat/periodic-control-frame code at `0x00403f80`) and the read returned > 0. Only types 1, 6 and 8 branch; every other value, including the `0x0b` seen after our init/weather writes, jumps to `0x004091a9` and is ignored. So the "selector 11" seen on 2026-10-11 and 2026-10-18 is the dongle's post-write state, not a data request, and the earlier answer to it was not original behavior. The serve loop now answers only types 1/6/8 and drops readbacks with a high nibble other than `1`. No type 1/6/8 request has been observed from hardware since registration.

### Gate Trace and 180 s Serve Run (2026-10-18)

- Static: the `[ebp-0xa44]`-adjacent gate `[ebp-0xa44]`/`0xfffff5bc` is the `LoadLibrary`-style handle for the DLL named at `0x4470a8` (retried with `Sleep` until non-null), so it is trivially true at runtime. The only data-dependent gate before the type switch is decoded cell 1 != 0, i.e. the request must originate from the dongle firmware; there is no host-side condition left to satisfy. `device.dat` (`0x447224`) is only watched for mtime changes that trigger the periodic control frame.
- Hardware (registered, init sent, no weather writes): over 180 s the dongle went idle -> `10 1b 18 10 18 10 10 10 10 10 10 10 10 10 18 1a` (cell 1 = `0x0b`) -> idle repeatedly, four times, with the identical readback each time. With no host write between them, `0x0b` is therefore a dongle-originated periodic state, not a write acknowledgment as previously assumed. The original ignores it. No type 1/6/8 appeared.

### Periodic 64-Bit Frame Decoded as Mode + UPD Timestamp (2026-10-18)

Every 17-byte write in `weather-native-*-20261009.jsonl`, `startup-attach-20261018.jsonl` and `startup-spawn-20261018.jsonl` after registration is the same ~58 s frame (stack return `0x00404b89`/`0x00404eef` is the `CAL_USB_WRITE` call in the loop that sends the 64-bit segment). Disassembly of `0x00404500..0x00404b89` and bit-for-bit comparison with the traces show its layout, starting at cell 7 bit 7 (cell 7 contributes 1 bit, later cells 4 bits each):

| Bits | Field | Source |
|---|---|---|
| 3 | mode = `(f90c ? 4 : 0) \| f908` | `f90c` is a boolean initialised to 1 (cleared at `0x403f5f`, set at `0x40421d`); `f908` is a single digit read from a text line at `0x403214` (value 0..3) |
| 4 / 5 / 5 / 6 | UPD month / day / hour / minute | the `UPD` record values of `weather.dat` (`<03>,<08>,<07>,<41>` = 3/8 07:41), extracted with `FUN_0040b9d0` (src bit offset, width) = (4,4), (3,5), (3,5), (2,6) |
| 1 | flag from `[ebp-0x718]` (`f8e8`) | set to 1 by the init path |

Check: the startup frame `10 14 10 10 10 10 10 11 10 1d 10 17 1a 16 17 14` unpacks to prefix `100`, then `0011 01000 00111 101001`, exactly the 3/8 07:41 stamp. Code at `0x004049b2..0x00404ad5` also derives the local-minus-UTC hour offset (`GetLocalTime`/`GetSystemTime`) and rolls the day/month/year counters (`f900`, `f904`, `f914`) with a days-per-month table.

Consequences:
- This frame carries no temperature, icon, or forecast data. It tells the dongle when the host data was last refreshed. The earlier "controlled TEMP / Day-1 TEMPH changes alter the first weather payload" conclusion is not supported: the changing cells were the 3-bit mode prefix (observed values 4,5,6,7 and 1,2) and its checksum, which vary with the text-derived setting and flag rather than with the weather records. Treat that earlier proof as retracted until a type 1/6/8 write is captured.
- Actual weather content (selector 6 current conditions, selectors 8..19 forecast pairs) is sent only in reply to outer request type 6. No such request has been observed on hardware since registration. Outer type 1 is the separate city/registration response, and outer type 8 is the control/timestamp path.
- Display model (user report: cloud/weather symbols, fixed temperature segments, five fixed forecast slots) fits the selector layouts: the Day-1 block carries `TEMPH/TEMPL` (9 bits each), a 7-bit morning icon and a 5-bit afternoon icon (79 icon names), plus wind/BFT/WW/HUM; later days use paired blocks. Which slot each selector fills on the LCD remains unobserved.

### Simulated Weather Update (2026-10-18)

`weather-life-discover --simulate-weather-update <legacy-file> <reg-code>` replaces the dead website refresh: register, init frame, a timestamp frame (`weather_session_build_timestamp_frame`, golden-tested against the captured `10 14 10 10 10 10 10 11 10 1d 10 17 1a 16 17 14`; its prefix cells keep the `0x10` high nibble, unlike the older `weather_session_build_control_frame`, which masks them), then selector 8..15 segments for days 2-5, reading back after each. On hardware all writes were accepted and every readback stayed idle (`0x10`); no LCD change was reported. Selector 6 (Day 1) was skipped because `build_day1` rejected a value in the installed Rotterdam `weather.dat` (not yet identified).

### No Dongle Request in Any Capture (2026-10-18)

Tallying cell 0/cell 1 of every `HidD_GetFeature` readback in all 23 traces (about 40,000 polls, original app and our tools): only `00` (idle) and `0b` (the dongle echoing the last written frame with cell 1 = `b`, cell 2 = `8`) occur. Request types 1, 6 and 8 never appear, so no capture contains a real weather-data exchange, and the simulated unsolicited writes (`--simulate-weather-update`) changed nothing on the LCD. Hypothesis (unproven): the LCD is a separate wireless unit with its own sensor, and the dongle only relays its RF request (sync button or periodic) to the host, so data is accepted only as an answer to such a request.

### Host Session Now Matches the Original on the Wire (2026-10-18)

`--serve-weather <file> <secs> <reg-code>` now follows `usbwr.exe`: registration, init frame, 130 ms polls, and a UPD timestamp frame (taken from the file's raw `UPD` line, since the record parser keeps only the first value) every 58 s. A 5-minute hardware run produced the same dongle behavior as the original traces: after each timestamp write the readback becomes `10 1b 18 10 10 10 10 11 10 1d 10 17 1a 16 17 14`, i.e. the written frame with cell 1 = `b` and cell 2 = `8`, then returns to idle. No type 1/6/8 request occurred, so the host side is no longer a differentiator from the original; the missing trigger is on the dongle/LCD side.

### weather.exe Refresh Replacement and Day-1 Mismatch (2026-10-18)

`python/legacy_weather_writer.py --city <name> --output <weather.dat>` replaces the dead download: it queries Open-Meteo (current plus six days) and writes the legacy named-record format using the archive's bit-length column. Icons (WMO code -> legacy icon name), 8-point Dutch compass tokens and the morning/afternoon/night split are approximations. Sending its output with `--simulate-weather-update` now answers selectors 8..15 (days 2-5); all were accepted and read back idle.

Selector 6 (Day 1) is still rejected for real data: `build_day1` parses `WW` and `AFWW` as integers and `AFTERNOON_N` as a compass, but the archive and generated files carry compass letters in `WW`/`AFWW` and an icon name in `AFTERNOON_N` (the later-day builders use compass/icon respectively). Replacing the two compass tokens with `0` makes selector 6 send. Either the Day-1 slot-to-field mapping in `docs` above is mis-assigned or the original converts text with an atoi-like fallback; this needs a `FUN_0040b9d0` call-site check before changing the builder.

### Correction: Selector 6 Is the Current-Conditions Block; Selectors 8..17 Are DAY1..DAY5 (2026-10-18)

Re-reading the selector-6 slot table against the archived records resolves the Day-1 rejection. Its slot widths/conversions are 9 temperature, 9 temperature, 7 icon, 11 integer (`.` discarded), 2 sentinel integer, 8 integer, 0 consumed, 5 compass, 7 sentinel integer, 8 `int*10+suffix`, 5 sentinel integer, 8 signed-humidity. These match the *current* records in order, not DAY1:

| Slot | Current record | Evidence |
| ---: | --- | --- |
| 0 / 1 | `TEMP` / `BTEMP` | 9-bit temperatures; `N/A` -> `0xff` (BTEMP is `N/A` in the archive) |
| 2 | `WEA` | 79-name icon index |
| 3 | `PRE` | 11 bits, `1030.8` -> 1030 (hPa) |
| 4 | `TREND` | 2 bits |
| 5 / 6 | `WS` / `BFT` | integer / consumed (0 bits) |
| 7 | `WW` | compass index (the archive holds `N`, `NO`, `NW`) |
| 8 | `HUM` | 7 bits |
| 9 | `VIS` | `int*10+suffix`, clamped to 254 |
| 10 / 11 | `UVI` / `DEWP` | 5-bit sentinel integer / signed 8-bit |

The previously labelled "unresolved current-condition group" and the experimental `weather_frame_build` input (current temperature, background temperature, pressure trend, visibility, UV, dew point) agree with this. Consequently the pair `8/9` encodes DAY1, `10/11` DAY2, ... `16/17` DAY5, and `18/19` DAY6. `weather_sender.c` implements this mapping (`build_current`, `day = 1 + (selector - 8) / 2`) and the native tests cover all six day pairs. Earlier statements in this file that call selector 6 the Day-1 block or selectors 8..17 days 2-6 predate this correction. `BTEMP` is parsed from the file text; the original's computed value (`FUN_00412e60`) is not reproduced. A simulated update with Open-Meteo data (selectors 6, 8..17) was accepted by the dongle with idle readbacks; LCD effect unobserved.

### Removed Unproven Write Experiments (2026-10-18)

`weather-life-discover` no longer contains `--simulate-weather-update`, `--sweep-writes`, `--fuzz-writes`, `--send-selector6-fixture-test-once` or `--print-selector6-fixture-test`. They sent guessed or fixture-valued segments (some under the retracted Day-1 mapping) and none produced a dongle request or LCD change, so they proved nothing. References to them in earlier sections are historical. `--send-observed-weather-payload-once` replays a frame captured from the original.

### Outer request types and weather selectors (2026-10-18)

The `FUN_00401400` readback dispatcher has two distinct selector levels:

- The outer request type is `cell[1] & 0x0f`. Type 1 enters a separate city/registration response path; type 6 enters the weather-data path; type 8 enters a control/timestamp path. Type 8 is not weather selector 8.
- Within outer type 6, the weather selector is assembled from readback cells 7 and 8: `((cell[7] & 0x03) << 4) | (cell[8] & 0x0f)`. The handler also reads the requested five-digit city code from cells 2..6.
- The native selector switch includes header selectors 0..4, selector 6, and paired forecast selectors 8..19. Even selectors 8, 10, 12, 14, 16, 18 use the first daily record group; the following odd selector uses its second group. Thus 18/19 are Day 6, despite the LCD observations showing five fixed forecast slots.

`--serve-weather` now decodes those separate fields, verifies that a type-6 request's five-digit city code matches the loaded `CITY_AND_WMO` record, dispatches supported weather layouts only for outer type 6, and includes the Day-6 pair. It does not answer outer types 1 or 8 with a weather segment. This fixes the earlier mistaken dispatch of outer type 8 as forecast selector 8. The native registration/favorite-city lookup and date-based scheduling are not fully replicated, and no outer type-6 request has been observed in hardware traces; therefore this path is source-derived but not live-verified. Do not infer LCD acceptance from a successful HID write.
