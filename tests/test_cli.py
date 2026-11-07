"""Verify command-line argument handling without accessing hardware."""

import subprocess
import sys
import unittest


EXECUTABLE = sys.argv.pop(1)


class DiscoverCliTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run(
            [EXECUTABLE, *args], capture_output=True, text=True, check=False
        )

    def test_help_does_not_open_device(self):
        result = self.run_cli("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage:", result.stdout)
        self.assertNotIn("Attempting to locate", result.stdout)

    def test_bad_arguments_do_not_open_device(self):
        for args in (
            ("--unknown",),
            ("--help", "extra"),
            ("--read-once", "extra"),
            ("--probe-registration-once", "extra"),
            ("--send-observed-weather-payload-once",),
            ("--serve-weather",),
            ("--send-weather-unsolicited",),
        ):
            with self.subTest(args=args):
                result = self.run_cli(*args)
                self.assertEqual(result.returncode, 2)
                self.assertIn("Usage:", result.stderr)
                self.assertNotIn("Attempting to locate", result.stdout)

    def test_invalid_payload_does_not_open_device(self):
        for payload in ("00", " " * 32, "+0" * 16, "gg" * 16):
            with self.subTest(payload=payload):
                result = self.run_cli("--send-observed-weather-payload-once", payload)
                self.assertEqual(result.returncode, 2)
                self.assertIn("32 hex digits", result.stderr)
                self.assertNotIn("Attempting to locate", result.stdout)

    def test_unsolicited_weather_is_refused_before_opening_device(self):
        result = self.run_cli("--send-weather-unsolicited", "weather.dat", "12345")
        self.assertEqual(result.returncode, 1)
        self.assertIn("disabled", result.stderr)
        self.assertNotIn("Attempting to locate", result.stdout)


if __name__ == "__main__":
    unittest.main()
