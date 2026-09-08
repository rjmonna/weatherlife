"""Summarize a passive Weather-Life JSONL trace without decoding unknown fields."""

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable


POLL_FRAME = "00 55 53 42 43 00 10 01 00"


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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="JSONL file produced by trace_weatherlife.py")
    args = parser.parse_args()
    summarize(load_events(args.trace))


if __name__ == "__main__":
    main()
