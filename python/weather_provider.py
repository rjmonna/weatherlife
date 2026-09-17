"""Free weather data client for the Weather-Life replacement."""

from dataclasses import dataclass
import argparse
import json
import os
from typing import List, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search"
DEFAULT_CITY_TABLE_PATH = r"C:\Program Files (x86)\Weather\images\cty"


class WeatherProviderError(RuntimeError):
    """Raised when Open-Meteo cannot provide valid weather data."""


@dataclass(frozen=True)
class Location:
    """Resolved location coordinates."""

    name: str
    latitude: float
    longitude: float
    country: Optional[str] = None
    timezone: Optional[str] = None
    city_number: Optional[int] = None
    country_code: Optional[str] = None


@dataclass(frozen=True)
class CurrentWeather:
    """Current weather values used by the display layer."""

    time: str
    temperature_c: float
    humidity_percent: int
    weather_code: int
    wind_speed_kmh: float
    wind_direction_degrees: int
    pressure_hpa: float


@dataclass(frozen=True)
class DailyForecast:
    """One forecast day."""

    date: str
    temperature_max_c: float
    temperature_min_c: float
    weather_code: int
    precipitation_mm: float = 0.0


@dataclass(frozen=True)
class WeatherReport:
    """Location, current conditions, and a five-day forecast."""

    location: Location
    current: CurrentWeather
    daily: List[DailyForecast]


def _country_key(value: str) -> str:
    """Normalize common country-name differences between data sources."""
    key = " ".join(value.casefold().split())
    return key[4:] if key.startswith("the ") else key


def _get_json(url: str, params: dict) -> dict:
    request = Request(
        f"{url}?{urlencode(params)}",
        headers={"User-Agent": "weather-life-replacement/0.1"},
    )
    try:
        with urlopen(request, timeout=15) as response:
            return json.load(response)
    except (HTTPError, URLError, TimeoutError, ValueError) as exc:
        raise WeatherProviderError(f"Open-Meteo request failed: {exc}") from exc


def geocode_cities(
    city: str,
    country: Optional[str] = None,
    language: str = "en",
) -> List[Location]:
    """Return matching cities, optionally restricted to a country or code."""
    if not city.strip():
        raise ValueError("city must not be empty")

    payload = _get_json(
        GEOCODING_URL,
        {"name": city.strip(), "count": 100, "language": language, "format": "json"},
    )
    results = payload.get("results") or []
    if not results:
        raise WeatherProviderError(f"No location found for {city!r}")

    country_filter = _country_key(country) if country else None
    locations = []
    for result in results:
        result_country = str(result.get("country", ""))
        result_country_code = str(result.get("country_code", ""))
        if country_filter and country_filter not in {
            _country_key(result_country),
            result_country_code.casefold(),
        }:
            continue
        locations.append(
            Location(
                name=result["name"],
                latitude=float(result["latitude"]),
                longitude=float(result["longitude"]),
                country=result.get("country"),
                timezone=result.get("timezone"),
                city_number=(int(result["id"]) if result.get("id") is not None else None),
                country_code=result.get("country_code"),
            )
        )
    return locations


def select_city(
    city: str,
    country: Optional[str] = None,
    city_number: Optional[int] = None,
    language: str = "en",
) -> Location:
    """Select one city by country and, when supplied, its geocoding number."""
    locations = geocode_cities(city, country=country, language=language)
    if city_number is not None:
        locations = [location for location in locations if location.city_number == city_number]
    if not locations:
        qualifier = f" in {country!r}" if country else ""
        raise WeatherProviderError(
            f"No city found for {city!r}{qualifier} with number {city_number!r}"
        )
    return locations[0]


def geocode_city(city: str, language: str = "en") -> Location:
    """Resolve the first city match using Open-Meteo's free API."""
    return select_city(city, language=language)


def fetch_weather(location: Location) -> WeatherReport:
    """Fetch current conditions and five forecast days for a location."""
    payload = _get_json(
        FORECAST_URL,
        {
            "latitude": location.latitude,
            "longitude": location.longitude,
            "current": (
                "temperature_2m,relative_humidity_2m,weather_code,"
                "wind_speed_10m,wind_direction_10m,surface_pressure"
            ),
            "daily": (
                "weather_code,temperature_2m_max,temperature_2m_min,"
                "precipitation_sum"
            ),
            "forecast_days": 5,
            "temperature_unit": "celsius",
            "wind_speed_unit": "kmh",
            "timezone": "auto",
        },
    )
    current = payload.get("current")
    daily = payload.get("daily")
    if not current or not daily:
        raise WeatherProviderError("Open-Meteo returned an incomplete weather report")

    daily_forecast = [
        DailyForecast(
            date=date,
            temperature_max_c=float(maximum),
            temperature_min_c=float(minimum),
            weather_code=int(code),
            precipitation_mm=float(precipitation),
        )
        for date, maximum, minimum, code, precipitation in zip(
            daily["time"],
            daily["temperature_2m_max"],
            daily["temperature_2m_min"],
            daily["weather_code"],
            daily["precipitation_sum"],
        )
    ]
    return WeatherReport(
        location=location,
        current=CurrentWeather(
            time=current["time"],
            temperature_c=float(current["temperature_2m"]),
            humidity_percent=int(current["relative_humidity_2m"]),
            weather_code=int(current["weather_code"]),
            wind_speed_kmh=float(current["wind_speed_10m"]),
            wind_direction_degrees=int(current["wind_direction_10m"]),
            pressure_hpa=float(current["surface_pressure"]),
        ),
        daily=daily_forecast,
    )


def fetch_city_weather(city: str) -> WeatherReport:
    """Resolve a city and fetch its current conditions and forecast."""
    return fetch_weather(geocode_city(city))


def fetch_selected_city_weather(
    country: str,
    city: str,
    city_code: Optional[str] = None,
    city_table_path: str = DEFAULT_CITY_TABLE_PATH,
) -> WeatherReport:
    """Resolve a table-selected city and fetch its Open-Meteo weather."""
    if city_code is not None or os.path.isfile(city_table_path):
        from legacy_weather_csv import read_city_table

        candidates = [
            entry
            for entry in read_city_table(city_table_path)
            if _country_key(entry.country) == _country_key(country)
            and entry.name.casefold() == city.casefold()
            and (city_code is None or entry.city_code == city_code)
        ]
        if not candidates:
            raise WeatherProviderError(
                f"City selection not found: {country!r}, {city!r}, {city_code!r}"
            )
        city = candidates[0].name
        country = candidates[0].country
    return fetch_weather(select_city(city, country=country))


def weather_code_description(code: int) -> str:
    """Return a concise description for an Open-Meteo WMO weather code."""
    descriptions = {
        0: "Clear",
        1: "Mainly clear",
        2: "Partly cloudy",
        3: "Overcast",
        45: "Fog",
        48: "Rime fog",
        51: "Light drizzle",
        53: "Drizzle",
        55: "Heavy drizzle",
        61: "Light rain",
        63: "Rain",
        65: "Heavy rain",
        71: "Light snow",
        73: "Snow",
        75: "Heavy snow",
        80: "Rain showers",
        81: "Rain showers",
        82: "Heavy rain showers",
        95: "Thunderstorm",
        96: "Thunderstorm with hail",
        99: "Thunderstorm with hail",
    }
    return descriptions.get(code, f"Unknown ({code})")


def _parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fetch Weather-Life replacement weather")
    parser.add_argument("--country", default="Netherlands")
    parser.add_argument("--city", default="Rotterdam")
    parser.add_argument("--city-code", help="Legacy Weather-Life code, e.g. 06344")
    parser.add_argument("--city-table", default=DEFAULT_CITY_TABLE_PATH)
    return parser.parse_args()


if __name__ == "__main__":
    arguments = _parse_arguments()
    report = fetch_selected_city_weather(
        arguments.country,
        arguments.city,
        city_code=arguments.city_code,
        city_table_path=arguments.city_table,
    )
    if arguments.city_code:
        print(f"{arguments.country}, {arguments.city} ({arguments.city_code})")
    print(report.location.name)
    print(report.current)
    for day in report.daily:
        print(day)
