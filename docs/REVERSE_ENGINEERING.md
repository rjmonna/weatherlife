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
experimental `weather_frame_build()` remains unproven and must not be sent to
the physical device until controlled one-field fixture captures establish its
field order and offsets.

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

### Device-button registration capture (2026-09-17)

The long passive capture in `traces/device-registration-trace.jsonl` was started
after `weather.exe` and `usbwr.exe` were already running, then the original
application's **Device** action was used. It contains 27,067 events over about
17 minutes. The capture shows:

- one initial 17-byte HID report followed by 18 retransmissions of the same
  16-byte registration payload;
- 19 `CAL_USB_WRITE` calls and 19 matching command-2 writes;
- 6,624 repeated read-poll writes of
  `00 55 53 42 43 00 10 01 00`;
- every `HidD_GetFeature-return` report was
  `00 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10`;
- no `usbdeviceread` or `GetProcAddress` events, because the tracer attached
  to already-loaded processes and therefore missed the earlier export
  resolution and DLL entry call.

The registration attempt therefore reached the low-level write path and the
OS accepted the writes, but the device never returned the expected decoded
echo (`decoded[0] == 0`, `decoded[1] == 9`, and matching id nibbles). The
missing “Device registration end” is consistent with the native retry loop
waiting for that response; it is not evidence that registration succeeded.
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

The branch then sets a packed length of `0x80` bits, copies/sends that segment in 16-byte
chunks, and conditionally constructs a separate `0x40`-bit segment. This is
why the append sequence must not be treated as a simple 16-byte field list:
the cursor is nibble-oriented and the branch contains headers, conditional
year pieces, local date/time fields, and parser-derived `UPD` values. The exact
current-condition and forecast field order remains unresolved; the archive
record order must not be substituted for the native case-5 order.

The transport offset is proven, but this does not yet prove the byte offsets
of individual weather fields within the 16-byte and 8-byte serializer payloads
or the exact order of the current-condition fields. The signed temperature
encoding is now confirmed: the original handler parses the
integer part, rounds up when the first fractional digit is at least 5, and
stores negative values as 9-bit two's-complement values (`0x200 - magnitude`).
The two daily temperature fields are each appended as 9 bits. `src/weather_frame.h`
exposes the verified dispatch constants, while `src/weather_frame.c` remains an
experimental candidate encoder until absolute offsets and segmentation are
traced.

**Still open**: the exact byte offsets of every remaining field within the
16-byte "today" and 8-byte second segment (high-confidence but not yet
individually assembly-verified beyond UPD), and the other ~19 dispatch cases
in `FUN_00401400`. End-to-end replay now proves that the archived weather
response reaches the DeskWeather display through the original parser and
serializer, but it does not by itself identify each field's bit offset.

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

1. **Map remaining fields**: Assign byte/bit offsets for `TEMP`, `HUM`, `WEA`,
   `PRE`, and the per-day forecast fields within the 16-byte "today" segment
   and 8-byte second segment, using the CSV bit-length table as a
   high-confidence guide and spot-checking a few more fields in
   `FUN_00401400`'s disassembly the same way `UPD` was verified.
2. **Correlate Frames**: Use the handle-correlated tracer during a live
  weather refresh to classify Tenx HID writes and feature reports.
3. **Connect Display**: Wire `src/bit_packer.c` and the mapped field table
   into a real weather-frame builder, then convert an Open-Meteo report only
   after the display frame format and its real target device are both
   confirmed.
5. **Cross-Platform Test**: Verify discovery and display updates on Windows,
   Linux, and macOS.

---

**Reverse Engineering Confidence Level**: 85%
**Protocol Confidence**: 50% (registration handshake fully decoded; the
weather-payload serializer's structure, total size, bit-packing primitive,
and one field group (UPD) are confirmed byte-exact via disassembly and the
CSV bit-length table is proven authoritative; remaining field offsets are
unlocated)
**Implementation Status**: Confirmed Tenx HID discovery, the registration
handshake algorithm (id generation, nibble packing, CRC-8), and an explicit
refusal to send the unconfirmed byte-oriented weather packet are implemented
in `src/`. The native weather-field encoder remains experimental.
