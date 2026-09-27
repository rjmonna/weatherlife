# USB Protocol Notes: Weather-Life Dongle

**Device**: Tenx composite HID (`0x1130:0x0202`)  
**Status**: Partially reverse engineered  
**Confidence**: Discovery and registration framing are confirmed. The
selector-6 Day-1 field order, native widths, bit offsets, nibble-cell encoding,
and checksum boundary are statically proven; other weather selector semantics
remain incomplete.

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
- The repeated 17-byte report is the `usbwr.dll` registration handshake, not
   weather data. Its nibble packing and CRC-8 are documented in
   `docs/REVERSE_ENGINEERING.md` and implemented offline in
   `python/dongle_protocol.py`.
- The HID transport adds a report byte before a 16-byte serializer segment,
   producing a 17-byte data report. A second serializer segment is 8 bytes,
   but no standalone 16-byte or 8-byte weather report has yet been captured.

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
logical 40-bit prefix. The prefix semantics, full weather payload, and weather
acknowledgment remain unproven; no guessed weather packet should be sent.

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

Each logical nibble is stored in a separate physical byte with a `0x10`
prefix. Thus the 128-bit primary segment is 16 logical bytes / 32 physical
nibble cells, sent as two 16-byte `CAL_USB_WRITE` payloads. The 64-bit
secondary segment is 8 logical bytes / 16 nibble cells, sent once.

The record names are resolved dynamically. The native parser searches keys
with a seven-byte stride, reads five current sub-fields and four sub-fields
for each daily block, and can produce seven daily temperature-like values.
A 27-entry local selector table groups the current records and paired daily
records. Only `TEMPH` is embedded as a useful key string; the other names are
loaded or assembled at runtime. The initialized width tables are therefore
strong evidence for wire widths, but do not label slots by themselves. The
selector-6 Day-1 mapping below is established separately from its group-skip
logic and the ordered archived fixture; the other selector families remain
unmapped.

The selector-6 mapping is a static proof from the executable's group-skip
logic, ordered archived fixture, per-slot conversion and append calls, and
checksum boundary. The 40-bit prefix meaning, current-condition group, other
forecast selectors, and several helper conversions remain unproven. No
`CAL_USB_WRITE` weather buffer has been captured at runtime.

Do not use a byte-oriented command-plus-length payload as an implementation
contract. The original weather path is a bit-packed
serializer and must first be captured at the `CAL_USB_WRITE` call boundary.

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
