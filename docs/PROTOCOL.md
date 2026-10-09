# USB Protocol Notes: Weather-Life Dongle

**Device**: Tenx composite HID (`0x1130:0x0202`)  
**Status**: Partially reverse engineered  
**Confidence**: Discovery, registration framing, and end-to-end archived
weather replay are confirmed; individual weather field semantics remain
unconfirmed.

## Overview

The original application communicates with a Tenx composite HID device. The
transport and registration handshake are known. The weather serializer is a
separate native bit-packing path and must not be confused with the captured
registration frames.

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
   requires decoded bytes `0, 9`, followed by the generated ID nibbles, so this
   report is rejected and the registration frame is retried. This establishes
   that the report is not a valid registration acknowledgment; it does not
   establish the dongle firmware's meaning for the all-`0x10` payload.
- Read-only probes using each transport-mapped device index (`0`, `1`, and
   `2`) all returned the same sixteen `0x10` bytes. The report is therefore not
   explained by the control header's device-index field.
- A one-shot write built from the decompiled registration algorithm changed
   the all-`0x10` response into an acknowledgment that matched the generated
   ID. This shows the all-`0x10` report is the pre-request/no-ack response in
   this test, rather than a registration rejection.
- Two one-shot writes using distinct 16-byte weather payloads from the
   caller-identified `weather-fixture-trace.jsonl` also changed the response.
   Their response prefixes decode to `0, 0x0b`, not the registration `0, 9`
   prefix. Ghidra analysis of the `usbwr.exe` read caller shows decoded byte 1
   is a dispatch selector; this path has explicit handlers for `1`, `6`, and
   `8`, while `0x0b` falls through to a shared continuation. It is not
   explicitly treated as success or error. Whether the display accepted or
   rendered either payload remains unconfirmed.
- The same decoded prefix `0, 0x0b` occurs in the controlled baseline,
   long-capture, and reconnect traces. In those traces it appears on reads
   after 17-byte writes (about 150-700 ms in the first observed transitions),
   while later reads can return to `0, 0`. This supports a transient,
   write-associated response state, but does not identify its status meaning.

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
   data after the refresh. This proves the parser-to-serializer-to-device path,
   but does not identify individual weather-field offsets.
- Field order, absolute offsets, response semantics, condition-code values,
   display formatting, and device acknowledgments remain unconfirmed.

### Dispatch and runtime record lookup

The native serializer is a dispatcher rather than one straight field list.
`FUN_00401400` bounds an operation selector to `0..19` and jumps through
`0x00409374`. Cases `0..4` build ten-byte header variants by packing ten
8-bit values. Case 5 appends local time and `UPD`; cases `6..9` process
record groups through nested jump tables and feed converted text to the same
MSB-first BitPacker.

The record names are resolved dynamically. The native parser searches keys
with a seven-byte stride, reads five current sub-fields and four sub-fields
for each daily block, and can produce seven daily temperature-like values.
A 27-entry local selector table groups the current records and paired daily
records. Only `TEMPH` is embedded as a useful key string; the other names are
loaded or assembled at runtime. The initialized width tables are therefore
strong evidence for wire widths, but are not sufficient to assign every slot
to `TEMP`, `HUM`, `WEA`, or a forecast field.

The exact semantic mapping still requires controlled legacy fixtures that
change one named record at a time and capture the corresponding native
`CAL_USB_WRITE` buffer.

Do not use a byte-oriented command-plus-length payload as an implementation
contract. The original weather path is a bit-packed
serializer and must first be captured at the `CAL_USB_WRITE` call boundary.

## Evidence standard

Treat a field mapping as confirmed only when a controlled fixture changes one
legacy record at a time and the corresponding serializer bits change at the
expected location. Keep raw traces and the fixture used for each comparison.
The native client therefore refuses to send the old guessed byte-oriented
weather packet. Keep replay and differential tracing passive until field
offsets are mapped.

**Last Updated**: 2026-09-18
**Reverse Engineering Status**: 70% Confidence  
**Implementation Status**: Ready for Phase 3
