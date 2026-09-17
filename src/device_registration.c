// Device registration handshake, decompiled from usbwr.dll!usbdeviceread via GhidraMCP.

#include "device_registration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
#endif

static uint8_t crc8_table[256];
static bool crc8_table_ready = false;

// Standard CRC-8, polynomial 0x31, MSB-first. Verified against the decompiled
// table (e.g. table[0x8c] == 0x07) rather than assumed.
static void ensure_crc8_table(void)
{
    if (crc8_table_ready) return;
    for (int i = 0; i < 256; i++) {
        uint8_t value = (uint8_t)i;
        for (int bit = 0; bit < 8; bit++) {
            value = (value & 0x80) ? (uint8_t)((value << 1) ^ 0x31) : (uint8_t)(value << 1);
        }
        crc8_table[i] = value;
    }
    crc8_table_ready = true;
}

static unsigned current_millisecond(void)
{
#ifdef _WIN32
    SYSTEMTIME now;
    GetLocalTime(&now);
    return now.wMilliseconds;
#else
    return (unsigned)(clock() % 1000);
#endif
}

void weather_registration_generate_id(WeatherRegistrationId* id)
{
    char code[6];
    snprintf(code, sizeof(code), "%x%04x", current_millisecond() & 0xf,
             (unsigned)(rand() & 0x7fff));
    for (int i = 0; i < 5; i++) {
        unsigned char byte = (unsigned char)code[i];
        id->nibbles[i * 2] = (uint8_t)((byte >> 4) & 0xf);
        id->nibbles[i * 2 + 1] = (uint8_t)(byte & 0xf);
    }
}

void weather_registration_build_frame(const WeatherRegistrationId* id,
                                      uint8_t frame[WEATHER_REGISTRATION_FRAME_SIZE])
{
    memset(frame, 0x10, WEATHER_REGISTRATION_FRAME_SIZE);
    frame[1] |= 0x08;
    for (int i = 0; i < WEATHER_REGISTRATION_ID_NIBBLES; i++) {
        frame[i + 2] |= id->nibbles[i];
    }

    ensure_crc8_table();
    uint8_t checksum = 0;
    for (int i = 0; i < 7; i++) {
        uint8_t reconstructed = (uint8_t)((frame[i * 2] << 4) | (frame[i * 2 + 1] & 0xf));
        checksum ^= crc8_table[reconstructed];
    }
    frame[14] |= (uint8_t)(0x10 | ((checksum >> 4) & 0xf));
    frame[15] |= (uint8_t)(checksum & 0xf);
}

void weather_registration_decode_response(const uint8_t response[WEATHER_REGISTRATION_FRAME_SIZE],
                                          uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE])
{
    for (int i = 0; i < WEATHER_REGISTRATION_FRAME_SIZE; i++) {
        decoded[i] = response[i] & 0xf;
    }
}

bool weather_registration_response_matches(const uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE],
                                           const WeatherRegistrationId* id)
{
    if (decoded[0] != 0 || decoded[1] != 9) return false;
    for (int i = 0; i < WEATHER_REGISTRATION_ID_NIBBLES; i++) {
        if (decoded[i + 2] != id->nibbles[i]) return false;
    }
    return true;
}
