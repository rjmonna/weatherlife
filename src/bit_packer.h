// Confirmed via GhidraMCP decompilation of usbwr.exe!FUN_0040d340 (2026-09-17).
// MSB-first, variable-width bit packer used by the native weather-payload
// serializer (usbwr.exe!FUN_00401400). Field widths match the "BIT LENGTH"
// column in the archived legacy CSV format (see legacy_weather_csv.py);
// verified byte-exact for month(4)/day(5)/hour(5)/minute(6) = 20 bits,
// matching the archived UPD<20> record exactly.

#ifndef WEATHER_LIFE_BIT_PACKER_H
#define WEATHER_LIFE_BIT_PACKER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t* buffer;
    size_t buffer_size;
    size_t bit_position;
    size_t native_byte_offset;
    int native_bit_offset;
    int native_cursor;
} BitPacker;

void bit_packer_init(BitPacker* packer, uint8_t* buffer, size_t buffer_size);

// Starts at an existing MSB-first byte/bit cursor, matching FUN_0040d340.
void bit_packer_init_at(BitPacker* packer, uint8_t* buffer, size_t buffer_size,
                        size_t byte_offset, int bit_offset);

// Appends the low `bit_count` bits of `value` (bit_count <= 16), MSB-first.
void bit_packer_append(BitPacker* packer, uint16_t value, int bit_count);

#endif  // WEATHER_LIFE_BIT_PACKER_H
