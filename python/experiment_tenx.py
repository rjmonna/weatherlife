#!/usr/bin/env python3
"""Send explicitly supplied HID feature-report bytes to the Tenx device.

This tool makes no claims about command or weather-payload meaning. Without
--send it only validates and displays the report. A real send requires an
explicit report size so the HID report can be padded correctly.
"""

import argparse
import sys
from typing import Optional, Sequence


TENX_VID = 0x1130
TENX_PID = 0x0202
TENX_USAGE_PAGE = 1
TENX_USAGE = 0
MAX_REPORT_SIZE = 4096


def parse_hex_bytes(value: str) -> bytes:
    """Parse a contiguous or whitespace-separated sequence of hex bytes."""
    parts = value.split()
    if not parts:
        raise ValueError("provide at least one hex byte")

    if len(parts) == 1:
        digits = parts[0]
        if len(digits) % 2:
            raise ValueError("contiguous hex input must contain complete bytes")
        tokens = [digits[index:index + 2] for index in range(0, len(digits), 2)]
    else:
        tokens = parts

    try:
        if any(len(token) not in (1, 2) for token in tokens):
            raise ValueError
        return bytes(int(token, 16) for token in tokens)
    except ValueError as error:
        raise ValueError("expected hex bytes, e.g. '55 53 42' or '555342'") from error


def build_feature_report(payload: bytes, report_id: int, report_size: int) -> bytes:
    """Prefix the HID report ID and zero-pad to the explicit report size."""
    if not 0 <= report_id <= 0xFF:
        raise ValueError("report ID must be between 0 and 255")
    if not 1 <= report_size <= MAX_REPORT_SIZE:
        raise ValueError(f"report size must be between 1 and {MAX_REPORT_SIZE}")
    if len(payload) + 1 > report_size:
        raise ValueError("payload and report ID exceed the feature report size")
    return bytes([report_id]) + payload + bytes(report_size - len(payload) - 1)


def _find_usage0_device(hid_module):
    candidates = [
        item for item in hid_module.enumerate(TENX_VID, TENX_PID)
        if item.get("usage_page") == TENX_USAGE_PAGE
        and item.get("usage") == TENX_USAGE
    ]
    if len(candidates) != 1:
        raise RuntimeError(
            "expected exactly one Tenx Usage Page 1 / Usage 0 interface; "
            f"found {len(candidates)}"
        )
    return candidates[0]


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--payload", required=True,
        help="hex payload bytes, contiguous or space-separated (excluding report ID)",
    )
    parser.add_argument(
        "--report-id", type=lambda value: int(value, 0), default=0,
        help="HID feature report ID (default: 0)",
    )
    parser.add_argument(
        "--report-size", type=int,
        help="full feature-report size including report ID; required with --send",
    )
    parser.add_argument(
        "--send", action="store_true",
        help="send to the Tenx Usage 0 HID interface (otherwise dry-run)",
    )
    args = parser.parse_args(argv)

    try:
        payload = parse_hex_bytes(args.payload)
        if args.report_size is not None:
            report = build_feature_report(payload, args.report_id, args.report_size)
        else:
            if not 0 <= args.report_id <= 0xFF:
                raise ValueError("report ID must be between 0 and 255")
            report = bytes([args.report_id]) + payload
        if args.send and args.report_size is None:
            raise ValueError("--report-size is required with --send")
    except ValueError as error:
        parser.error(str(error))

    print("Target: Tenx HID 1130:0202, Usage Page 1 / Usage 0")
    print("Feature report:", report.hex(" "))
    if not args.send:
        print("Dry run only. Add --send to transmit; no protocol meaning is assumed.")
        return 0

    try:
        import hid
    except ImportError:
        print("Sending requires the hidapi Python package: python -m pip install hidapi",
              file=sys.stderr)
        return 2

    device = None
    try:
        interface = _find_usage0_device(hid)
        device = hid.device()
        device.open_path(interface["path"])
        written = device.send_feature_report(list(report))
        if written != len(report):
            print(f"Short feature-report write: {written} of {len(report)} bytes",
                  file=sys.stderr)
            return 1
        print(f"Sent {written} feature-report bytes.")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Tenx feature-report send failed: {error}", file=sys.stderr)
        return 1
    finally:
        if device is not None:
            device.close()


if __name__ == "__main__":
    raise SystemExit(main())