"""Replacement for weather.exe's refresh: write the legacy weather.dat from Open-Meteo.

The original application downloaded update/city/<code>.csv from the now-offline
weather-life.com and saved it as weather.dat. This module builds the same named
record format (bit lengths copied from the archived Rotterdam response) so the
native sender can consume it. Icon, direction and morning/afternoon/night
choices are approximations: Open-Meteo has no per-period data and the legacy
icon semantics are not fully proven.
"""

import argparse
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from weather_icons import _TABLE_A, _TABLE_B  # noqa: E402
from weather_provider import (  # noqa: E402
    FORECAST_URL,
    Location,
    WeatherProviderError,
    _get_json,
    geocode_city,
)

DAYS = 6
_DIRECTIONS = ("N", "NO", "O", "ZO", "Z", "ZW", "W", "NW")
_BEAUFORT_LIMITS_KMH = (1, 5, 11, 19, 28, 38, 49, 61, 74, 88, 102, 117)
# WMO weather code -> day icon name (night names use the same table position).
_WMO_ICON = {
    0: "SO____", 1: "MS____", 2: "WB____", 3: "BD____", 45: "NB____", 48: "NB____",
    51: "BDR1__", 53: "BDR1__", 55: "BDR2__", 56: "BDR1__", 57: "BDR2__",
    61: "BDR1__", 63: "BDR2__", 65: "BDR3__", 66: "BDR2__", 67: "BDR3__",
    71: "BDS1__", 73: "BDS2__", 75: "BDS3__", 77: "BDS1__",
    80: "WBR1__", 81: "WBR2__", 82: "WBR3__", 85: "WBS1__", 86: "WBS2__",
    95: "BDG1__", 96: "BDG2__", 99: "BDG2__",
}

_CURRENT_VARS = (
    "temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,"
    "wind_speed_10m,pressure_msl,visibility,uv_index,dew_point_2m"
)
_DAILY_VARS = (
    "weather_code,temperature_2m_max,temperature_2m_min,wind_speed_10m_max,"
    "wind_direction_10m_dominant,relative_humidity_2m_mean,sunrise,sunset,"
    "precipitation_probability_max,precipitation_sum,sunshine_duration,"
    "uv_index_max,dew_point_2m_mean"
)


def beaufort(speed_kmh: float) -> int:
    for number, limit in enumerate(_BEAUFORT_LIMITS_KMH):
        if speed_kmh < limit:
            return number
    return 12


def direction(degrees: Optional[float]) -> str:
    if degrees is None:
        return "N"
    return _DIRECTIONS[int(((degrees % 360) + 22.5) // 45) % 8]


def icon_names(wmo_code: int) -> Dict[str, str]:
    """Return the day and night icon names for a WMO code."""
    day = _WMO_ICON.get(int(wmo_code), "BD____")
    return {"day": day.lower(), "night": _TABLE_B[_TABLE_A.index(day)].lower()}


def _num(value: Any, digits: int = 0) -> str:
    if value is None:
        return "N/A"
    rounded = round(float(value), digits)
    return f"{rounded:.{digits}f}" if digits else str(int(rounded))


def _record(key: str, bits: int, value: str, unit: str = "") -> str:
    return f"{key:<14}{'<%d>' % bits:<12}<{value}>{unit};"


def _clock_record(key: str, iso: Optional[str]) -> str:
    if not iso:
        return _record(key, 11, "N/A")
    stamp = datetime.fromisoformat(iso)
    return (f"{key:<14}{'<11>':<12}<{stamp.hour:02d}>hour,"
            f"<{stamp.minute:02d}>min;")


def build_legacy_text(location: Location, payload: Dict[str, Any],
                      now: datetime, city_line: bool = True) -> str:
    """Render an Open-Meteo forecast payload in the legacy named-record format."""
    current = payload["current"]
    daily = payload["daily"]
    number = location.city_number
    code = f"{number:05d}" if number is not None and number < 100000 else "00000"
    lines: List[str] = []
    if city_line:
        lines.append(f"{location.country or ''};{location.name}")
    lines += [
        "DES           BIT LENGTH    DATA",
        f"CITY_AND_WMO  <{location.country or ''};{location.name};{code}>;",
        f"{'UPD':<14}{'<20>':<12}<{now.month:02d}>month,<{now.day:02d}>day,"
        f"<{now.hour:02d}>hour,<{now.minute:02d}>minute;",
        "",
        _record("TEMP", 9, _num(current["temperature_2m"], 1), "c"),
        _record("BTEMP", 9, _num(current.get("apparent_temperature"), 1), "c"),
        _record("WEA", 6, icon_names(current["weather_code"])["day"]),
        _record("PRE", 11, _num(current.get("pressure_msl"), 1), "hpa"),
        _record("TREND", 2, "0") + "0:steady 1:rising  2:falling;",
        _record("WS", 6, _num(current["wind_speed_10m"]), "km/h"),
        _record("BFT", 6, str(beaufort(current["wind_speed_10m"]))),
        _record("WW", 4, direction(current.get("wind_direction_10m"))),
        _record("HUM", 7, _num(current["relative_humidity_2m"]), "%"),
        _record("VIS", 7, _num((current.get("visibility") or 0) / 1000), "km"),
        _record("UVI", 5, _num(current.get("uv_index"))),
        _record("DEWP", 9, _num(current.get("dew_point_2m"), 1), "c"),
    ]
    for index in range(min(DAYS, len(daily["time"]))):
        def day_value(name: str) -> Any:
            values = daily.get(name)
            return values[index] if values else None

        wmo = day_value("weather_code") or 0
        icons = icon_names(wmo)
        speed = day_value("wind_speed_10m_max") or 0
        wind_dir = direction(day_value("wind_direction_10m_dominant"))
        humidity = _num(day_value("relative_humidity_2m_mean"), 0)
        sunshine = day_value("sunshine_duration")
        lines += [
            "",
            f"DAY{index + 1} {daily['time'][index].replace('-', '')};",
            _record("TEMPH", 9, _num(day_value("temperature_2m_max")), "c"),
            _record("TEMPL", 9, _num(day_value("temperature_2m_min")), "c"),
            _record("MORING_ICON", 6, icons["day"]),
            _record("WS", 8, _num(speed), "km/h"),
            _record("WBFT", 8, str(beaufort(speed))),
            _record("WW", 5, wind_dir),
            _record("HUM", 7, humidity, "%"),
            _record("AFTERNOON_N", 6, icons["day"]),
            _record("AFWS", 8, _num(speed), "km/h"),
            _record("AFBFT", 8, str(beaufort(speed))),
            _record("AFWW", 5, wind_dir),
            _record("AFHUM", 7, humidity, "%"),
            _record("NIGHT_ICON", 6, icons["night"]),
            _record("NWS", 8, _num(speed), "km/h"),
            _record("NBFT", 8, str(beaufort(speed))),
            _record("NWW", 5, wind_dir),
            _record("NHUM", 7, humidity, "%"),
            _clock_record("SUNR", day_value("sunrise")),
            _clock_record("SUNS", day_value("sunset")),
            _record("PPCP", 7, _num(day_value("precipitation_probability_max")), "%"),
            _record("RAINMS", 8, _num(day_value("precipitation_sum"), 1), "mm"),
            _record("SUNTIME", 5, _num((sunshine or 0) / 3600, 1), "hour"),
            _record("UVI", 5, _num(day_value("uv_index_max"))),
            _record("DEWP", 9, _num(day_value("dew_point_2m_mean"), 1), "c"),
        ]
    return "\r\n".join(lines) + "\r\n"


def fetch_payload(location: Location) -> Dict[str, Any]:
    payload = _get_json(FORECAST_URL, {
        "latitude": location.latitude,
        "longitude": location.longitude,
        "current": _CURRENT_VARS,
        "daily": _DAILY_VARS,
        "forecast_days": DAYS,
        "timezone": "auto",
        "wind_speed_unit": "kmh",
    })
    if not payload.get("current") or not payload.get("daily"):
        raise WeatherProviderError("Open-Meteo returned an incomplete weather report")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--city", default="Rotterdam")
    parser.add_argument("--output", required=True, help="Path of the weather.dat to write")
    parser.add_argument("--no-city-line", action="store_true",
                        help="Omit the leading 'Country;City' line weather.dat carries")
    args = parser.parse_args()

    location = geocode_city(args.city)
    text = build_legacy_text(location, fetch_payload(location), datetime.now(),
                             city_line=not args.no_city_line)
    Path(args.output).write_text(text, encoding="ascii", newline="")
    print(f"Wrote {args.output} for {location.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
