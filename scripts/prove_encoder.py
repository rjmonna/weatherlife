"""Compare original-app weather reports from passive JSONL traces."""

import argparse
import json
from pathlib import Path
from typing import Any, Iterable, List, Tuple


def load_events(path: Path) -> Iterable[dict[str, Any]]:
    """Yield valid JSON objects from a tracer output file."""
    with path.open(encoding="utf-8") as trace:
        for line_number, line in enumerate(trace, 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"Invalid JSON on line {line_number}: {exc}") from exc
            if isinstance(event, dict):
                yield event


def parse_bytes(value: str) -> bytes:
    """Parse the tracer's space-separated hexadecimal byte representation."""
    return bytes.fromhex(value)


def weather_reports(events: Iterable[dict[str, Any]]) -> List[Tuple[int, bytes]]:
    """Return native 16-byte weather payloads and their trace timestamps."""
    reports = []
    for event in events:
        stack = " ".join(event.get("stack", []))
        if (
            event.get("api") == "WriteFile"
            and event.get("length") == 17
            and "CAL_USB_WRITE" in stack
            and event.get("buffer")
        ):
            report = parse_bytes(event["buffer"])
            if len(report) == 17 and report[0] == 0:
                reports.append((int(event.get("time", 0)), report[1:]))
    return reports


def differing_bits(left: bytes, right: bytes) -> List[int]:
    """Return MSB-first bit offsets that differ between equal-length payloads."""
    differences = []
    for byte_index, (left_byte, right_byte) in enumerate(zip(left, right)):
        changed = left_byte ^ right_byte
        for bit_index in range(8):
            if changed & (0x80 >> bit_index):
                differences.append(byte_index * 8 + bit_index)
    return differences


def prove(paths: Iterable[Path]) -> int:
    """Print observed payloads and differential bit evidence."""
    all_reports = []
    for path in paths:
        reports = weather_reports(load_events(path))
        print(f"{path}: {len(reports)} accepted weather report(s)")
        for timestamp, report in reports:
            print(f"  {timestamp}: {report.hex(' ')}")
        all_reports.extend((path, report) for _, report in reports)

    distinct = []
    for _, report in all_reports:
        if report not in distinct:
            distinct.append(report)
    if len(distinct) < 2:
        print("Proof status: insufficient distinct accepted weather reports")
        return 2

    left, right = distinct[:2]
    bits = differing_bits(left, right)
    print(f"Differential: {len(bits)} changed bit(s)")
    print("Bit offsets: " + ", ".join(str(bit) for bit in bits))
    print("Proof status: native transport output is reproducible and differentially observable")
    print("Semantic field mapping: not proven by this comparison alone")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, nargs="+", help="Passive tracer JSONL file(s)")
    args = parser.parse_args()
    raise SystemExit(prove(args.trace))


if __name__ == "__main__":
    main()