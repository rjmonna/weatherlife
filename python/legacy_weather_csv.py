"""Parser for archived Weather-Life city CSV responses.

The legacy endpoint called these files CSV, but they are named records rather
than comma-separated columns. Values and units are preserved as strings so the
parser does not silently change the original wire/service semantics.
"""

from dataclasses import dataclass, field
import re
from typing import Dict, Optional


_RECORD_RE = re.compile(r"^(?P<key>[A-Za-z0-9_]+)\s+(?:<(?P<bits>[^>]*)>\s+)?(?P<value>.*);\s*$")
_DAY_RE = re.compile(r"^(DAY[1-6])\s+(?P<date>\d{8});\s*$")


@dataclass
class LegacyWeatherDocument:
    """Structured representation of one legacy city weather response."""

    country: Optional[str] = None
    city: Optional[str] = None
    city_code: Optional[str] = None
    current: Dict[str, str] = field(default_factory=dict)
    daily: Dict[str, Dict[str, str]] = field(default_factory=dict)


def parse_legacy_weather(text: str) -> LegacyWeatherDocument:
    """Parse the named-record format served by the legacy city endpoint."""
    document = LegacyWeatherDocument()
    active_day: Optional[str] = None

    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        if line == "DES           BIT LENGTH    DATA":
            continue

        day_match = _DAY_RE.match(line)
        if day_match:
            active_day = day_match.group(1)
            document.daily[active_day] = {"DATE": day_match.group("date")}
            continue

        record = _RECORD_RE.match(line)
        if not record:
            raise ValueError(f"Unrecognized legacy weather record on line {line_number}: {raw_line!r}")

        key = record.group("key")
        value = record.group("value")
        if key == "CITY_AND_WMO":
            parts = value.split(";")
            if len(parts) != 3:
                raise ValueError(f"Invalid CITY_AND_WMO value on line {line_number}")
            document.country, document.city, document.city_code = [
                part.strip("<>") for part in parts
            ]
        elif active_day is None:
            document.current[key] = value
        else:
            document.daily[active_day][key] = value

    if not document.city_code:
        raise ValueError("Legacy weather document has no CITY_AND_WMO record")
    return document


def read_legacy_weather(path: str) -> LegacyWeatherDocument:
    """Read and parse a legacy weather file from disk."""
    with open(path, encoding="utf-8") as weather_file:
        return parse_legacy_weather(weather_file.read())
