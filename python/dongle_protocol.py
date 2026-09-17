"""Offline registration-frame utilities for the Weather-Life HID path.

The CAL_USB_READ poll frame and the registration-handshake algorithm below
were confirmed by decompiling usbwr.dll!usbdeviceread with GhidraMCP on
2026-09-17 (see docs/REVERSE_ENGINEERING.md). onlywell.dll, which actually owns
the wire framing, has not been decompiled yet.
"""

import argparse
import random
import time
from typing import List


REGISTRATION_ID_NIBBLES = 10
REGISTRATION_FRAME_SIZE = 16


def _crc8_table(polynomial: int = 0x31) -> List[int]:
    """Standard CRC-8 table, MSB-first. Verified against usbwr.dll's embedded
    table (e.g. table[0x8c] == 0x07)."""
    table = []
    for value in range(256):
        for _ in range(8):
            value = ((value << 1) ^ polynomial) & 0xFF if value & 0x80 else (value << 1) & 0xFF
        table.append(value)
    return table


_CRC8_TABLE = _crc8_table()


def generate_registration_id() -> List[int]:
    """Mirror thunk_FUN_10001650: an ASCII "%x%04x" of (ms&0xf, rand()&0x7fff),
    nibble-expanded one hex character at a time."""
    code = f"{int(time.time() * 1000) & 0xf:x}{random.randint(0, 0x7fff):04x}"
    nibbles = []
    for char in code[:5]:
        byte = ord(char)
        nibbles.append((byte >> 4) & 0xF)
        nibbles.append(byte & 0xF)
    return nibbles


def build_registration_frame(id_nibbles: List[int]) -> bytes:
    """Mirror usbwr.dll!usbdeviceread's CAL_USB_WRITE payload construction."""
    if len(id_nibbles) != REGISTRATION_ID_NIBBLES:
        raise ValueError(f"expected {REGISTRATION_ID_NIBBLES} id nibbles")
    frame = bytearray([0x10] * REGISTRATION_FRAME_SIZE)
    frame[1] |= 0x08
    for i, nibble in enumerate(id_nibbles):
        frame[i + 2] |= nibble

    checksum = 0
    for i in range(7):
        reconstructed = ((frame[i * 2] << 4) | (frame[i * 2 + 1] & 0xF)) & 0xFF
        checksum ^= _CRC8_TABLE[reconstructed]
    frame[14] |= 0x10 | ((checksum >> 4) & 0xF)
    frame[15] |= checksum & 0xF
    return bytes(frame)


def response_matches(decoded: bytes, id_nibbles: List[int]) -> bool:
    """decoded must already be masked with & 0xf per byte (device echo)."""
    if decoded[0] != 0 or decoded[1] != 9:
        return False
    return list(decoded[2:2 + REGISTRATION_ID_NIBBLES]) == id_nibbles


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-registration-frame",
        action="store_true", required=True,
        help="generate and print a fresh registration frame without hardware",
    )
    args = parser.parse_args()
    id_nibbles = generate_registration_id()
    frame = build_registration_frame(id_nibbles)
    print("id nibbles:", " ".join(f"{n:x}" for n in id_nibbles))
    print("frame:", frame.hex(" "))


if __name__ == "__main__":
    main()
