"""Create a legacy weather fixture with exactly one record changed."""

import argparse
import re
import sys
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

from legacy_weather_csv import parse_legacy_weather


_VALUE_RE = re.compile(
    r"^(?P<prefix>\s*[A-Za-z0-9_]+\s+(?:<[^>]*>\s+)?<)"
    r"(?P<value>[^>]*)"
    r"(?P<suffix>>[^;\r\n]*;\s*)$"
)
_DAY_RE = re.compile(r"^\s*(DAY[1-6])\s+\d{8};\s*$")


def mutate_record(text: str, key: str, value: str, day: Optional[str] = None,
                  occurrence: int = 1) -> str:
    """Replace one record value in the selected current/day block."""
    if key == "CITY_AND_WMO":
        raise ValueError("CITY_AND_WMO is identity metadata and cannot be mutated")
    if day is not None and not re.fullmatch(r"DAY[1-6]", day):
        raise ValueError("day must be DAY1 through DAY6")
    if occurrence < 1:
        raise ValueError("occurrence must be at least 1")

    lines = text.splitlines(keepends=True)
    active_day = None
    matches = []
    for index, line in enumerate(lines):
        content = line.rstrip("\r\n")
        day_match = _DAY_RE.match(content)
        if day_match:
            active_day = day_match.group(1)
            continue
        record_match = _VALUE_RE.match(content)
        if not record_match:
            continue
        record_key = content.strip().split(None, 1)[0]
        if record_key != key:
            continue
        if (day is None and active_day is None) or active_day == day:
            matches.append((index, record_match))

    if occurrence > len(matches):
        scope = day or "current"
        raise ValueError(f"{key} occurrence {occurrence} not found in {scope}; found {len(matches)}")

    index, match = matches[occurrence - 1]
    lines[index] = (
        f"{match.group('prefix')}{value}{match.group('suffix')}"
        + lines[index][len(lines[index].rstrip("\r\n")):]
    )
    mutated = "".join(lines)
    parse_legacy_weather(mutated)
    return mutated


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Source legacy weather fixture")
    parser.add_argument("output", type=Path, help="Output fixture path")
    parser.add_argument("--key", required=True, help="One record key, such as TEMP or HUM")
    parser.add_argument("--value", required=True, help="Replacement value without angle brackets")
    parser.add_argument("--day", help="Restrict mutation to DAY1 through DAY6")
    parser.add_argument("--occurrence", type=int, default=1,
                        help="One-based occurrence within the selected block (default: 1)")
    args = parser.parse_args()

    source = args.input.read_text(encoding="utf-8")
    mutated = mutate_record(source, args.key, args.value, args.day, args.occurrence)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(mutated, encoding="utf-8", newline="")
    print(f"Wrote one-record mutation: {args.key}={args.value} -> {args.output}")


if __name__ == "__main__":
    main()
