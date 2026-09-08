"""Free weather data client for the Weather-Life replacement."""

from dataclasses import dataclass
import json
from typing import List, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search"


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


@dataclass(frozen=True)
class WeatherReport:
    """Location, current conditions, and a five-day forecast."""

    location: Location
    current: CurrentWeather
    daily: List[DailyForecast]


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


def geocode_city(city: str, language: str = "en") -> Location:
    """Resolve a city name to coordinates using Open-Meteo's free API."""
    if not city.strip():
        raise ValueError("city must not be empty")

    payload = _get_json(
        GEOCODING_URL,
        {"name": city.strip(), "count": 1, "language": language, "format": "json"},
    )
    results = payload.get("results") or []
    if not results:
        raise WeatherProviderError(f"No location found for {city!r}")

    result = results[0]
    return Location(
        name=result["name"],
        latitude=float(result["latitude"]),
        longitude=float(result["longitude"]),
        country=result.get("country"),
        timezone=result.get("timezone"),
    )


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
            "daily": "weather_code,temperature_2m_max,temperature_2m_min",
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
        )
        for date, maximum, minimum, code in zip(
            daily["time"],
            daily["temperature_2m_max"],
            daily["temperature_2m_min"],
            daily["weather_code"],
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


if __name__ == "__main__":
    report = fetch_city_weather("Rotterdam")
    print(report.location.name)
    print(report.current)
    for day in report.daily:
        print(day)
