import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "scripts"))

from dongle_protocol import build_registration_frame, response_matches  # noqa: E402
from experiment_tenx import build_feature_report, parse_hex_bytes  # noqa: E402
from legacy_weather_csv import parse_city_table, parse_legacy_weather  # noqa: E402
from weather_provider import (  # noqa: E402
    CurrentWeather,
    Location,
    WeatherProviderError,
    _country_key,
    fetch_weather,
    geocode_cities,
    weather_code_description,
)
from analyze_trace import load_events as load_trace_events  # noqa: E402
from prove_encoder import differing_bits, load_events as load_proof_events  # noqa: E402


class DongleProtocolTests(unittest.TestCase):
    def test_registration_frame_matches_confirmed_fixture(self):
        self.assertEqual(
            build_registration_frame(list(range(10))),
            bytes.fromhex("10 18 10 11 12 13 14 15 16 17 18 19 10 10 14 1b"),
        )

    def test_registration_frame_requires_exact_id_length(self):
        with self.assertRaises(ValueError):
            build_registration_frame([1, 2])

    def test_response_matches_validates_header_and_id(self):
        identifier = list(range(10))
        response = bytes([0, 9, *identifier, 0, 0, 0, 0])
        self.assertTrue(response_matches(response, identifier))
        self.assertFalse(response_matches(bytes([1, *response[1:]]), identifier))
        self.assertFalse(response_matches(response, identifier[:-1]))


class TenxExperimentTests(unittest.TestCase):
    def test_hex_payload_accepts_contiguous_and_spaced_bytes(self):
        expected = bytes.fromhex("55 53 42 43")
        self.assertEqual(parse_hex_bytes("55534243"), expected)
        self.assertEqual(parse_hex_bytes("55 53 42 43"), expected)

    def test_feature_report_adds_id_and_pads_to_requested_size(self):
        self.assertEqual(
            build_feature_report(bytes.fromhex("55 53"), 0, 5),
            bytes.fromhex("00 55 53 00 00"),
        )

    def test_feature_report_rejects_oversized_payload(self):
        with self.assertRaises(ValueError):
            build_feature_report(b"\x01\x02", 0, 2)

    def test_hex_payload_rejects_incomplete_or_invalid_bytes(self):
        for value in ("f", "gg", "123 45"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_hex_bytes(value)


class LegacyWeatherTests(unittest.TestCase):
    def test_city_table_supports_sections_and_wrapped_names(self):
        text = '[Netherlands]\nRotterdam=06344\n"The Hague\n=06345"\n'
        cities = parse_city_table(text)
        self.assertEqual([(city.name, city.city_code) for city in cities],
                         [("Rotterdam", "06344"), ("The Hague", "06345")])

    def test_city_table_rejects_records_before_country(self):
        with self.assertRaises(ValueError):
            parse_city_table("Rotterdam=06344\n")

    def test_weather_parser_preserves_current_and_daily_records(self):
        document = parse_legacy_weather(
            "DES           BIT LENGTH    DATA\n"
            "CITY_AND_WMO <0> <Netherlands>;<Rotterdam>;<06344>;\n"
            "TEMP <9> <21.5>;\n"
            "DAY1 20260918;\n"
            "HIGH <9> <25>;\n"
        )
        self.assertEqual(document.city_code, "06344")
        self.assertEqual(document.current["TEMP"], "<21.5>")
        self.assertEqual(document.daily["DAY1"]["HIGH"], "<25>")

    def test_weather_parser_requires_city_record(self):
        with self.assertRaises(ValueError):
            parse_legacy_weather("TEMP <9> <21.5>;\n")


class WeatherProviderTests(unittest.TestCase):
    def test_country_key_normalizes_articles_and_case(self):
        self.assertEqual(_country_key(" The Netherlands "), "netherlands")
        self.assertEqual(_country_key("NETHERLANDS"), "netherlands")

    def test_geocode_filters_country_and_converts_types(self):
        payload = {"results": [
            {"name": "Rotterdam", "latitude": "51.9", "longitude": "4.5",
             "country": "Netherlands", "country_code": "NL", "id": 123},
            {"name": "Rotterdam", "latitude": 40, "longitude": -74,
             "country": "United States", "country_code": "US", "id": 456},
        ]}
        with patch("weather_provider._get_json", return_value=payload):
            locations = geocode_cities(" Rotterdam ", country="nl")
        self.assertEqual(len(locations), 1)
        self.assertEqual(locations[0].city_number, 123)
        self.assertEqual(locations[0].latitude, 51.9)

    def test_geocode_rejects_empty_and_no_results(self):
        with self.assertRaises(ValueError):
            geocode_cities("   ")
        with patch("weather_provider._get_json", return_value={}):
            with self.assertRaises(WeatherProviderError):
                geocode_cities("Missing")

    def test_fetch_weather_builds_report_from_payload(self):
        payload = {
            "current": {"time": "2026-09-18T12:00", "temperature_2m": "21.5",
                        "relative_humidity_2m": 65, "weather_code": 2,
                        "wind_speed_10m": 12.3, "wind_direction_10m": 180,
                        "surface_pressure": 1013.2},
            "daily": {"time": ["2026-09-18"], "temperature_2m_max": [25],
                      "temperature_2m_min": [15], "weather_code": [2],
                      "precipitation_sum": [0.4]},
        }
        location = Location("Rotterdam", 51.9, 4.5)
        with patch("weather_provider._get_json", return_value=payload):
            report = fetch_weather(location)
        self.assertEqual(report.current.humidity_percent, 65)
        self.assertEqual(report.daily[0].precipitation_mm, 0.4)

    def test_fetch_weather_rejects_incomplete_payload(self):
        with patch("weather_provider._get_json", return_value={"current": {}}):
            with self.assertRaises(WeatherProviderError):
                fetch_weather(Location("x", 0, 0))

    def test_weather_code_description_has_known_and_unknown_values(self):
        self.assertEqual(weather_code_description(0), "Clear")
        self.assertEqual(weather_code_description(999), "Unknown (999)")


class TraceUtilityTests(unittest.TestCase):
    def test_trace_loaders_skip_blank_lines_and_reject_invalid_json(self):
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as trace:
            trace.write("\n{}\n")
            path = Path(trace.name)
        try:
            self.assertEqual(list(load_trace_events(path)), [{}])
            self.assertEqual(list(load_proof_events(path)), [{}])
            path.write_text("not json\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                list(load_trace_events(path))
        finally:
            path.unlink()

    def test_differing_bits_reports_msb_offsets(self):
        self.assertEqual(differing_bits(b"\x80\x01", b"\x00\x03"), [0, 14])


if __name__ == "__main__":
    unittest.main()
