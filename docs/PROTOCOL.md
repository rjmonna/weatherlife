# USB Protocol Notes: Weather-Life Dongle

**Device**: Tenx composite HID (`0x1130:0x0202`)  
**Status**: Partially reverse engineered  
**Confidence**: Discovery and registration framing are confirmed. The
selector-6 Day-1 field order and the paired later-day selector layouts for
Days2–6, including native widths, nibble-cell encoding, and checksum boundary,
are statically proven. The current-condition group and a Day7 source record
remain unresolved/unsupported. The archive and native parser stop at Day6, so
the Day7 source record is absent rather than merely an unmapped field.

## Overview

The original application communicates with a Tenx composite HID device. The
transport and registration handshake are known. The weather serializer is a
separate native bit-packing path and must not be confused with the captured
registration frames.

`onlywell.dll` headless decompilation confirms that this DLL is transport-only:
`CAL_USB_WRITE` copies the caller's 16-byte payload unchanged, sends an eight-
byte `USBC` write header through the command HID handle, and sends the payload
through the other HID handle with `WriteFile`. It contains no weather decoder,
display formatter, payload checksum validator, or display-acceptance result.
Consequently, a successful `WriteFile` or transient read response proves only
transport/device interaction, not that the LCD rendered the value.

Headless decompilation of the installed `usbwr.exe` now proves the sender-side
path as well. It resolves `CAL_USB_WRITE` from `onlywell.dll`, parses
`weather.dat`, packs fields with the native MSB-first variable-width bit
packer, and transmits the resulting 64-bit or 128-bit segments as 16-byte
chunks. This confirms that the original application sends the native weather
serializer output through the same transport boundary. It still cannot prove
that the device firmware accepted or rendered a segment; that result is not
reported by `usbwr.exe`.

### LCD mechanics boundary

Static string and call-path inspection of `onlywell.dll`, `usbwr.dll`, and
`usbwr.exe` found no LCD controller commands, character map, segment map,
framebuffer operation, or display-success response. The proven LCD-facing
mechanics stop at the 16-byte HID output reports described above. The
replacement therefore keeps general weather transmission refused instead of
inventing an LCD encoding or treating `WriteFile` success as rendering.

### Observed physical display layout (2026-10-10)

The user reports that the connected unit visibly shows sun/weather symbols,
segmented date and time, and a fixed view of approximately five forecast days.
This is useful hardware evidence for the display model, but it does not yet
map the visible slots to selectors or prove whether the native Day1--Day6
source range is all displayed simultaneously, rotated, or partially unused.
The existing `WEATHER_FRAME_FORECAST_DAYS` value of five means today plus four
forecast entries in the replacement data model; it is not a protocol proof.

The user additionally reports that, after an action from the tray icon, the
application appeared to send current conditions plus five days of weather over
USB/HID. This is a credible runtime hypothesis for a multi-selector update:
`weather.exe` can initiate the refresh while `usbwr.exe` parses and sends the
segments. It is not yet a byte-level capture of that complete sequence, and it
does not resolve whether the native Day1--Day6 source capacity includes one
non-visible or rotated day.

The separately decompiled `usbwr.dll` confirms the registration-side helpers:
five-character identifier generation, low-nibble response normalization, and
the table-driven CRC checksum written into two nibble cells. These mechanics
match the offline registration implementation and are distinct from the
weather serializer's variable-width bit packing.

## Physical Layer

### USB Interface
- **Device Class**: HID composite device
- **Vendor ID**: 0x1130 (Tenx)
- **Product ID**: 0x0202
- **HID Usage Page**: 1
- **Required Usages**: 0 and 3
- **Observed I/O**: HID feature reports through `HidD_GetFeature` for reads;
   output reports through `WriteFile` for control and payload writes
- **Endpoint layout**: Not treated as a protocol contract; the Windows HID
   API abstracts the underlying reports.

### Physical Connector
- **Type**: USB-A (standard)
- **Power**: Drawn from USB host (5V, limited current)
## Confirmed control traffic

The passive traces contain these facts only:

- `00 55 53 42 43 00 10 01 00` is a repeated 9-byte control report on the
   `CAL_USB_READ` path.
- `00 55 53 42 43 00 10 02 00` is the confirmed 9-byte write-control report;
   the following 17-byte report carries the 16-byte serializer buffer.
- A 16-byte logical buffer becomes a 17-byte HID report after adding report ID
   zero. Both registration and weather serialization use this transport, so
   report length alone cannot identify the payload. The registration builder's
   nibble packing and CRC-8 are documented in `docs/REVERSE_ENGINEERING.md` and
   implemented offline in `python/dongle_protocol.py`.
- The weather serializer also emits a 16-byte primary segment and an 8-byte
   second segment; exact field semantics remain partly unconfirmed.
- Ghidra decompilation of `onlywell.dll!CAL_USB_READ` shows a 16-byte read
   payload: it calls the HID helper with length `0x10`, which requests 17 bytes
   from `HidD_GetFeature` including the report ID, then copies 16 bytes to the
   caller. The Windows backend initially used the smaller
   `HIDP_CAPS.FeatureReportByteLength` value and returned only eight payload
   bytes. After matching the decompiled 17-byte request, a read-only probe of
   the connected Tenx device on 2026-10-09 returned sixteen `0x10` payload
   bytes, matching the original-app trace. This confirms the report-length
   discrepancy was in the replacement backend. In `usbwr.dll!usbdeviceread`,
   the response decoder masks each payload byte with `0x0f`; the observed
   all-`0x10` response therefore decodes to all zeroes. The registration check
   requires normalized nibble cells 0 and 1 to be `0` and `9`, followed by the
   ten generated ID nibbles in cells 2 through 11, so this report is rejected
   and the registration frame is retried. This establishes that the report is
   not a valid registration acknowledgment; it does not
   establish the dongle firmware's meaning for the all-`0x10` payload.
- Read-only probes using each normalized control-selector value (`0`, `1`,
   and `2`) all returned the same sixteen `0x10` bytes. The report is therefore
   not explained by the control header's selector field.
- A one-shot write built from the decompiled registration algorithm changed
   the all-`0x10` response into an acknowledgment that matched the generated
   ID. This shows the all-`0x10` report is the pre-request/no-ack response in
   this test, rather than a registration rejection.
- Two one-shot writes using distinct 16-byte weather payloads from the
   caller-identified `weather-fixture-trace.jsonl` also changed the response.
   Their normalized response cells 0 and 1 are `0` and `0x0b`, not the
   registration-ack cells `0` and `9`. The `usbwr.exe` dispatcher switches
   directly on nibble cell 1; it has explicit handlers for `1`, `6`, and `8`,
   while `0x0b` falls through to a shared continuation. It is not
   explicitly treated as success or error. Whether the display accepted or
   rendered either payload remains unconfirmed.
- The same normalized nibble-cell prefix `0, 0x0b` occurs in the controlled
   baseline, long-capture, and reconnect traces. In those traces it appears on reads
   after 17-byte writes (about 150-700 ms in the first observed transitions),
   while later reads can return to `0, 0`. This supports a transient,
   write-associated response state, but does not identify its status meaning.
- Decompilation of the `usbwr.exe` dispatcher (call at `0x00403dc8`, switch at
  `0x00404f9a`) shows the exchange is device-polled: the low nibble of response
  cell 1 is the selector the dongle requests (1 city/registration handler at
  `0x004053f4`, 6 Day-1 at `0x00405f3b`, 8 later days at `0x00404fc2`; others
  include selector families 8..19). The idle all-`0x10` and the transient
  `0, 0x0b` response trigger no send. The opt-in
  `weather-life-discover --serve-weather <legacy-weather-file> <seconds>` loop
  now uses parsed legacy named records to answer selector 6 and selectors
  8..17. It leaves selector 1, selector 7, current selectors, and selectors
  18/19 unanswered because their data/layout is unresolved or unavailable.
  The loop does not perform device registration or startup initialization and
  stops after four consecutive read failures rather than reconnecting with an
  unknown initialization value. What makes the dongle request a selector is
  still unknown, and previous exploratory writes produced no visible display
  change.
- On 2026-10-10, one exact 16-byte weather payload captured from the original
   application's `CAL_USB_WRITE` path was sent from an idle baseline. The first
   read after the write returned `10 1b 18 10 10 10 10 10 14 10 10 10 10 12
   19 1b`; its normalized prefix is `0, 0x0b`, not the registration
   acknowledgment prefix `0, 9`. A later read returned to sixteen `0x10` cells,
   and the user observed no visible display change. This disproves using that
   transient response as evidence of display acceptance; it does not establish
   whether the payload was rejected, incomplete, or accepted without rendering.
- On 2026-10-10, the explicitly authorized selector-6 exploratory segment was
   sent as two 16-cell writes. Six reads over 600 ms remained sixteen `0x10`
   cells. The test used the archived Rotterdam Day-1 numeric values, with
   documented unknown fallbacks for the icon and direction. This candidate
   caused no observed response transition; acceptance/display remains unknown.
- A subsequent read-only run of the rebuilt C client on 2026-10-10 opened the
   connected Usage Page 1 / Usage 0 interface (`/dev/hidraw2`) and again read
   sixteen `0x10` cells. This confirms the current Linux read path and idle
   marker, not weather-write acceptance or display behavior.

Static decompilation of `onlywell.dll` proves the control-report layout. Each
9-byte output report consists of a zero report ID followed by an 8-byte
`USBC` header:

```
[0]      Report ID: 0
[1..4]   ASCII "USBC"
[5..6]   Transfer length in bytes, unsigned 16-bit little-endian
[7]      Operation: 1 = read, 2 = write
[8]      Selector: ASCII '0'/'1'/'2' normalized to 0/1/2
```

Thus a 16-byte read is `00 55 53 42 43 00 10 01 00`, a 16-byte write is
`00 55 53 42 43 00 10 02 00`, and status polling is a zero-length read
`00 55 53 42 43 00 00 01 00`. The selector encoding is proven, but the
selector's device-level meaning is not. A write then sends a separate 17-byte
output report (report ID `00` plus 16 unchanged payload bytes); a read obtains
a 17-byte feature report and the exported wrapper removes its report-ID byte.
That wrapper truncates returned data at the first zero byte, so it is not a
binary-safe 16-byte read API.

This `USBC` control header is transport framing, not the weather serializer's
logical 40-bit prefix. The full weather payload and display/rendering result
remain unproven. Hardware experiments used exact 16-byte payloads from a
caller-identified original trace, not guessed encoder output.

## Device-button action capture

The passive capture `traces/device-registration-trace.jsonl` recorded the original
application's Device action for about 17 minutes. Analyze it with:

```powershell
.\.venv\Scripts\python.exe .\scripts\analyze_trace.py .\traces\device-registration-trace.jsonl
```

It contains 47 `CAL_USB_WRITE` calls and 47 successful 17-byte HID writes. The
device was polled 15,267 times with the repeated 9-byte read command, but every
`HidD_GetFeature-return` was the unchanged all-`0x10` report. The trace did not
hook `usbdeviceread`, and its captured write stacks omit the high-level caller;
these writes cannot be classified as registration or weather payloads from
this trace alone.

Because this trace attached to already-running processes, it does not contain
the earlier `usbdeviceread` entry or `GetProcAddress` export-resolution events.

## Weather serializer evidence

Static analysis located a native serializer in `usbwr.exe`:

- It parses named records from the legacy weather file.
- It uses an MSB-first, variable-width bit packer.
- The archived record widths include `TEMP<9>`, `BTEMP<9>`, `WEA<6>`,
   `PRE<11>`, `TREND<2>`, `WS<6>`, `BFT<6>`, `WW<4>`, `HUM`, and daily
   fields such as `TEMPH<9>` and `TEMPL<9>`.
- The `UPD` record is directly proven: month/day/hour/minute are packed as
   4/5/5/6 bits, totaling 20 bits.
- An archived `update/city/06344.csv` served locally through the original
   request path was accepted by `weather.exe`; the DeskWeather window displayed
   data after the refresh.
- Selector 6's exact Day-1 field order and logical offsets are statically
   proven below. The current-condition group, other selectors, response
   semantics, display formatting, and device acknowledgments remain
   unconfirmed.

### Dispatch and runtime record lookup

The native serializer is a dispatcher rather than one straight field list.
`FUN_00401400` bounds an operation selector to `0..19` and jumps through
`0x00409374`. Cases `0..4` build ten-byte header variants by packing ten
8-bit values. Case 5 appends local time and `UPD`; cases `6..9` process
record groups through nested jump tables and feed converted text to the same
MSB-first BitPacker.

Selector 6 is proven to encode the Day-1 block beginning at `TEMPH`. Its
decoded logical bit layout is `TEMPH(9), TEMPL(9), MORING_ICON(7), WS(11),
WBFT(2), WW(8), HUM(0), AFTERNOON_N(5), AFWS(7), AFBFT(8), AFWW(5),
AFHUM(8)`. These fields start at logical bit 40 and end at bit 119; bit 119
is padding and logical byte 15 is the checksum. See
`docs/REVERSE_ENGINEERING.md` for the selector pre-skip and per-slot encoding
evidence.

The first 40 bits are seeded from ten preceding `CAL_USB_READ` nibble cells;
selector 6 normalizes and preserves them, modifies cell 1 with `|= 0x07`, and
starts the weather fields at bit 40. The registration-ack response state has
a proven conditional interpretation: cells 0/1 are `0/9`, cells 2..11 carry
the ten ID nibbles, so the copied prefix contains the first eight ID nibbles
and emits cell 1 as `0x0f`. Other response-state cell meanings remain
unresolved. Selector-6 input conversions are statically resolved, including
the 9-bit signed temperature path, icon and direction ordinals,
field-specific missing-value handling, and the `AFBFT` scale/clamp; details
and exact edge behavior are recorded in `docs/REVERSE_ENGINEERING.md`.

The C API seeds the 32-cell buffer from readback, appends the proven 79-bit
field order from already-converted values, adds the pad and checksum, and
supports an explicitly named one-shot exploratory send. Source-text
conversions and icon-name lookup are not integrated, so this test sender does
not establish a general display-ready weather path. The application-level
weather sender remains refused.

Each logical nibble is stored in a separate physical byte with a `0x10`
prefix. Thus the 128-bit primary segment is 16 logical bytes / 32 physical
nibble cells, sent as two 16-byte `CAL_USB_WRITE` payloads. The 64-bit
secondary segment is 8 logical bytes / 16 nibble cells, sent once.

The record names are resolved dynamically. The native parser searches keys
with a seven-byte stride, reads five current inputs and four inputs for each
daily temperature calculation, and produces one current plus at most six
daily temperature-like values. A 27-entry local selector table groups the
current records and paired daily records. Only `TEMPH` is embedded as a useful
key string; the other names are loaded or assembled at runtime. The initialized
width tables provide the wire widths; selector 6 and later-day selectors
8–17 are mapped against the ordered archived fixture below. The current-
condition group remains unmapped.

The daily mappings are static proofs from the executable's group/record skip
logic, ordered archived fixture, per-slot conversion and append calls, and
checksum boundary. The 40-bit prefix meaning, current-condition group, other
forecast selectors, and several helper conversions remain unproven. Two exact
16-byte reports from the caller-identified weather trace have been replayed
once each; they produced `0x0B`-prefixed responses, but the display result was
not observed.

A controlled original-app refresh provides a separate end-to-end data-path
proof. The corrected tracer redirected the original URL to the archived
5,642-byte Rotterdam CSV, and `InternetOpenUrlA` succeeded. The native app
consumed the fixture and emitted 64-bit weather segments at `0x00404b89` and
`0x00404eef`; each captured segment used `0x10`-prefixed nibble cells and
passed the native checksum. A second fixture changed only Day-1 `TEMPH` from
`6` to `12`. The first post-fetch `0x00404b89` payload changed physical cell 8
from `0x14` to `0x18` and recomputed checksum cells 14 and 15; all other cells
were unchanged. Both writes were followed by the same decoded `00 0b` response.
This proves that a Day-1 forecast input affects checksum-valid weather data
written to the dongle. It does not prove the display accepted or rendered it.
An independent fixture changed only current `TEMP` from `-1.0 C` to `10.0 C`.
The first post-fetch `0x00404b89` payload changed cell 8 from `0x14` to `0x1c`
and recomputed checksum cells 14 and 15; all other cells were unchanged. Its
first response also decoded to `00 0b`. Together these controlled changes
prove that current weather and Day-1 forecast inputs both affect weather
payloads written to the dongle, but do not prove on-screen rendering.

Selectors 18/19 have code branches, but the native parser and archived format
stop at Day6, so they do not establish Day7 data. The exploratory replacement
write was observed, but no corresponding original-app `CAL_USB_WRITE` capture
is available.

### Hardware registration experiment (2026-10-10)

- `--probe-registration-once` sent one generated registration frame to the
  Tenx dongle; the follow-up read decoded to the same id (acknowledgment
  matched). This is the first device response to a host write, so the
  registration handshake is confirmed on hardware.
- A 30 s `--listen-and-answer` right after registration saw one non-idle
  response (`10 19 13 13 13 17 16 15 13 15 13 11 18 10 1f 17`, cell 1 low
  nibble 9, not selector 1/6/8), then only idle cells. Without registration
  a 90 s listen saw only idle cells.
- Registration followed by `--send-selector6-fixture-test-once` produced only
  idle post-write reads and no visible display change. The dongle never
  requested selector 1/6/8 in any run, so the trigger for polling and
  display acceptance remain unproven.

Do not use a byte-oriented command-plus-length payload as an implementation
contract. The original weather path is a bit-packed serializer; the captured
replays do not validate the incomplete replacement encoder.

## Evidence standard

Treat mappings established by static control/dataflow and fixture alignment as
confirmed only for the traced selector and records. Use controlled one-record
changes plus a native `CAL_USB_WRITE` capture to validate value semantics and
other selectors. The native client therefore continues to refuse weather
transmission; the proven selector-6 layout does not establish a complete
display-ready packet or header.

**Last Updated**: 2026-09-18
**Reverse Engineering Status**: 70% Confidence  
**Implementation Status**: Ready for Phase 3
