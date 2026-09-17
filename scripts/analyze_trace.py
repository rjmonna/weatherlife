"""Summarize a passive Weather-Life JSONL trace without decoding unknown fields."""

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable


POLL_FRAME = "00 55 53 42 43 00 10 01 00"
WRITE_COMMAND_FRAME = "00 55 53 42 43 00 10 02 00"
DEFAULT_FEATURE_REPORT = "00" + " 10" * 16


def load_events(path: Path) -> Iterable[Dict[str, Any]]:
    """Yield valid JSON events from a tracer output file."""
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


def summarize(events: Iterable[Dict[str, Any]]) -> None:
    """Print observed APIs and exact frame frequencies."""
    event_list = list(events)
    print(f"Events: {len(event_list)}")

    api_counts = Counter(event.get("api", "<unknown>") for event in event_list)
    print("APIs:")
    for api, count in api_counts.most_common():
        print(f"  {count:5d} {api}")

    urls = sorted({event["url"] for event in event_list if event.get("url")})
    if urls:
        print("URLs:")
        for url in urls:
            print(f"  {url}")

    writes = Counter(
        event.get("buffer", "")
        for event in event_list
        if event.get("api") == "WriteFile" and event.get("buffer")
    )
    if writes:
        print("Serial writes:")
        for frame, count in writes.most_common():
            label = "polling frame" if frame == POLL_FRAME else "unclassified frame"
            print(f"  {count:5d} {label}: {frame}")

    reports = Counter(
        event.get("report", "")
        for event in event_list
        if event.get("api") == "HidD_GetFeature-return" and event.get("report")
    )
    if reports:
        print("HID feature reports:")
        for report, count in reports.most_common():
            print(f"  {count:5d} {report}")

    registration_writes = [
        event for event in event_list
        if event.get("api") == "WriteFile"
        and event.get("length") == 17
        and "CAL_USB_WRITE" in " ".join(event.get("stack", []))
    ]
    registration_calls = [
        event for event in event_list
        if event.get("api") == "CAL_USB_WRITE-call"
    ]
    registration_returns = [
        event for event in event_list
        if event.get("api") == "CAL_USB_WRITE-return"
    ]
    polls = [
        event for event in event_list
        if event.get("api") == "WriteFile"
        and event.get("buffer") == POLL_FRAME
    ]
    command_writes = [
        event for event in event_list
        if event.get("api") == "WriteFile"
        and event.get("buffer") == WRITE_COMMAND_FRAME
    ]
    if registration_writes or registration_calls:
        print("Registration analysis:")
        print(f"  CAL_USB_WRITE calls: {len(registration_calls)}")
        print(f"  CAL_USB_WRITE returns: {len(registration_returns)}")
        print(f"  17-byte HID writes: {len(registration_writes)}")
        print(f"  command-2 writes: {len(command_writes)}")
        print(f"  read-poll writes: {len(polls)}")

        payloads = Counter(event.get("buffer", "") for event in registration_writes)
        for payload, count in payloads.most_common():
            print(f"  registration report ({count}): {payload}")

        if polls and reports:
            nonzero_reports = [
                report for report in reports
                if report != DEFAULT_FEATURE_REPORT
            ]
            if not nonzero_reports:
                print("  result: no registration echo; feature reads stayed at all-0x10")
            else:
                print("  result: non-default feature response observed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="JSONL file produced by trace_weatherlife.py")
    args = parser.parse_args()
    summarize(load_events(args.trace))


if __name__ == "__main__":
    main()
