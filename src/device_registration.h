// Device registration handshake, decompiled from usbwr.dll!usbdeviceread via GhidraMCP.
// Confirmed 2026-09-17 against both usbwr.dll and onlywell.dll: onlywell.dll
// prepends the 8-byte USBC header and leading report-id 0x00, then passes the
// 16-byte usbwr.dll registration buffer through unchanged. The 16-byte payload
// itself is the byte-exact algorithm proven by the decompiled id/CRC logic.

#ifndef WEATHER_LIFE_DEVICE_REGISTRATION_H
#define WEATHER_LIFE_DEVICE_REGISTRATION_H

#include <stdint.h>
#include <stdbool.h>

#define WEATHER_REGISTRATION_ID_NIBBLES 10
#define WEATHER_REGISTRATION_FRAME_SIZE 16

typedef struct {
    uint8_t nibbles[WEATHER_REGISTRATION_ID_NIBBLES];
} WeatherRegistrationId;

// Generate a registration id: ASCII "%x%04x" of (ms&0xf, rand()&0x7fff),
// nibble-expanded one hex character at a time (matches thunk_FUN_10001650).
void weather_registration_generate_id(WeatherRegistrationId* id);

// Build the 16-byte usbwr.dll-side registration payload for the given id:
// every byte starts at 0x10, byte[1] adds bit 0x08, id nibbles are OR'd into
// bytes[2..11], and the CRC-8 (poly 0x31, MSB-first) is written as two nibbles
// at bytes[14..15]. onlywell.dll's transport layer adds the 8-byte USBC header
// and leading 0x00 report ID before the HID write; it does not change the
// usbwr.dll payload byte[13] (the byte remains 0x10 in the proven buffer).
void weather_registration_build_frame(const WeatherRegistrationId* id,
                                      uint8_t frame[WEATHER_REGISTRATION_FRAME_SIZE]);

// Undo the device's nibble-per-byte encoding (mask each byte with 0xf),
// matching the post-read decode path in onlywell.dll via CAL_USB_READ.
void weather_registration_decode_response(const uint8_t response[WEATHER_REGISTRATION_FRAME_SIZE],
                                          uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE]);

// True if a decoded response echoes the id: decoded[0]==0, decoded[1]==9,
// and decoded[2..11] equal id->nibbles.
bool weather_registration_response_matches(const uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE],
                                           const WeatherRegistrationId* id);

#endif  // WEATHER_LIFE_DEVICE_REGISTRATION_H
