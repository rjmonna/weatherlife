"""Observed serial operations for the Weather-Life dongle.

Only byte sequences captured from the original application are implemented here.
The display frame is preserved as an observed transaction, not decoded weather
payload.
"""

from contextlib import AbstractContextManager
import argparse
import time
from typing import Optional

import serial


DEFAULT_PORT = "COM4"
DEFAULT_BAUDRATE = 115200

# Captured from usbwr.exe while polling the connected device.
CAL_USB_READ_FRAME = bytes.fromhex("00 55 53 42 43 00 10 01 00")

# Captured next to a refresh operation. Field meanings are unknown.
OBSERVED_DISPLAY_FRAME = bytes.fromhex(
    "00 10 14 10 10 10 10 10 10 14 10 10 10 10 16 15 1f"
)

# Captured alongside the display frame. Meaning is not yet assigned.
OBSERVED_WRITE_CONTROL_FRAME = bytes.fromhex("00 55 53 42 43 00 10 02 00")


class DongleProtocol(AbstractContextManager):
    """Access the observed CP2102 serial protocol without guessing fields."""

    def __init__(
        self,
        port: str = DEFAULT_PORT,
        baudrate: int = DEFAULT_BAUDRATE,
        timeout: float = 1.0,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._serial: Optional[serial.Serial] = None

    def open(self) -> None:
        """Open the dongle using the captured 115200 8N1 settings."""
        if self._serial is None:
            self._serial = serial.Serial(
                self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout,
                write_timeout=self.timeout,
            )

    def close(self) -> None:
        """Close the serial port if it is open."""
        if self._serial is not None:
            self._serial.close()
            self._serial = None

    def __exit__(self, exception_type, exception_value, traceback) -> None:
        self.close()

    def _write(self, frame: bytes) -> None:
        if self._serial is None:
            raise RuntimeError("dongle is not open")
        self._serial.write(frame)
        self._serial.flush()

    def read_poll(self, settle_time: float = 0.1) -> bytes:
        """Send the confirmed CAL_USB_READ frame and return available bytes."""
        self._write(CAL_USB_READ_FRAME)
        time.sleep(settle_time)
        return self.read_available()

    def send_observed_display_frame(self) -> None:
        """Replay the captured 17-byte frame once.

        This does not claim to encode weather data. It is intended only for
        reproducing the captured original-application transaction.
        """
        self._write(OBSERVED_DISPLAY_FRAME)

    def send_observed_control_frame(self) -> None:
        """Replay the captured adjacent 9-byte control frame once."""
        self._write(OBSERVED_WRITE_CONTROL_FRAME)

    def read_available(self) -> bytes:
        """Read bytes currently available without issuing another command."""
        if self._serial is None:
            raise RuntimeError("dongle is not open")
        waiting = self._serial.in_waiting
        return self._serial.read(waiting) if waiting else b""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--poll", action="store_true", help="send the observed CAL_USB_READ frame")
    parser.add_argument(
        "--send-observed-frame",
        action="store_true",
        help="replay the captured 17-byte display transaction once",
    )
    parser.add_argument(
        "--send-observed-control",
        action="store_true",
        help="replay the captured adjacent control frame once",
    )
    args = parser.parse_args()
    actions = sum((args.poll, args.send_observed_frame, args.send_observed_control))
    if actions != 1:
        parser.error("choose exactly one operation")

    with DongleProtocol(port=args.port) as dongle:
        dongle.open()
        if args.poll:
            response = dongle.read_poll()
            print(response.hex(" ") or "<no response>")
        elif args.send_observed_frame:
            dongle.send_observed_display_frame()
            print(OBSERVED_DISPLAY_FRAME.hex(" "))
        else:
            dongle.send_observed_control_frame()
            print(OBSERVED_WRITE_CONTROL_FRAME.hex(" "))


if __name__ == "__main__":
    main()
