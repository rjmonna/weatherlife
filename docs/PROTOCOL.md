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
- **Observed I/O**: HID feature reports through `HidD_GetFeature` and
   `HidD_SetFeature`
- **Endpoint layout**: Not treated as a protocol contract; the Windows HID
   API abstracts the underlying reports.

### Physical Connector
- **Type**: USB-A (standard)
- **Power**: Drawn from USB host (5V, limited current)
## Confirmed control traffic

The passive traces contain these facts only:

- `00 55 53 42 43 00 10 01 00` is a repeated 9-byte control report on the
   `CAL_USB_READ` path.
- A related `... 02 00` report is observed, but its meaning is unassigned.
- The repeated 17-byte report is the `usbwr.dll` registration handshake, not
   weather data. Its nibble packing and CRC-8 are documented in
   `docs/REVERSE_ENGINEERING.md` and implemented offline in
   `python/dongle_protocol.py`.
- The HID transport adds a report byte before a 16-byte serializer segment,
   producing a 17-byte data report. A second serializer segment is 8 bytes,
   but no standalone 16-byte or 8-byte weather report has yet been captured.

## Device-button registration capture

The passive capture `traces/device-registration-trace.jsonl` recorded the original
application's Device action for about 17 minutes. Analyze it with:

```powershell
.\.venv\Scripts\python.exe .\scripts\analyze_trace.py .\traces\device-registration-trace.jsonl
```

It contains 19 `CAL_USB_WRITE` calls and 19 successful 17-byte HID writes. The
same registration payload was retransmitted 18 times. The device was polled
6,624 times with the repeated 9-byte read command, but every
`HidD_GetFeature-return` was the unchanged all-`0x10` report. No registration
echo was observed, so the missing “Device registration end” indicates a
registration timeout/retry condition, not success.

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
