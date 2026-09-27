// Weather-frame primitives and the current unverified composite builder.
//
// The confirmed field widths are taken from the archived legacy CSV's
// "BIT LENGTH" column, proven authoritative for UPD (month/day/hour/minute =
// 20 bits, byte-exact match to usbwr.exe!FUN_00401400). The temperature
// signed temperature encoding is confirmed as rounded integer degrees in a
// 9-bit two's-complement field. The composite frame builder below remains
// unverified because field order, header bytes, and segment semantics remain
// unresolved.

#ifndef WEATHER_LIFE_WEATHER_FRAME_H
#define WEATHER_LIFE_WEATHER_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include "bit_packer.h"

#define WEATHER_FRAME_FORECAST_DAYS 5  // today + 4 more
#define WEATHER_FRAME_SIZE 16          // matches the confirmed 128-bit "today" segment
#define WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET 10
#define WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET 4

// usbwr.exe dispatch records: two fields per daily forecast block.
#define WEATHER_FRAME_DAY_DISPATCH_FIRST 6
#define WEATHER_FRAME_DAY_DISPATCH_STRIDE 2

typedef struct {
    int month;    // 1-12
    int day;      // 1-31
    int hour;     // 0-23
    int minute;   // 0-59
} WeatherFrameTime;

// Append the proven UPD record: month(4), day(5), hour(5), minute(6).
// Returns false when a component is outside the native field range.
bool weather_frame_append_upd(BitPacker* packer, const WeatherFrameTime* time);

// Prepare an existing native working buffer for the confirmed case-5 cursor.
// Header bytes other than the proven control bits must be supplied by the caller.
bool weather_frame_begin_native_primary(BitPacker* packer,
                                        uint8_t frame[WEATHER_FRAME_SIZE]);

// Build only the proven native frame shell: zeroed working bytes, the case-5
// control bits, and UPD at the confirmed byte-10/bit-4 cursor. This is not a
// display-ready weather packet because the header and record fields remain
// unresolved.
bool weather_frame_build_proven_upd(const WeatherFrameTime* time,
                                    uint8_t frame[WEATHER_FRAME_SIZE]);

typedef struct {
    WeatherFrameTime time;
    float current_temperature_c;
    float background_temperature_c;
    uint16_t weather_code;
    uint16_t pressure_hpa;
    uint16_t pressure_trend;
    uint16_t wind_speed;
    uint16_t beaufort_scale;
    uint16_t wind_word;
    uint16_t humidity_percent;
    uint16_t visibility;
    int uv_index;
    float dew_point_c;
    // [0] is today, [1..4] are the next four days.
    float high_temperature_c[WEATHER_FRAME_FORECAST_DAYS];
    float low_temperature_c[WEATHER_FRAME_FORECAST_DAYS];
} WeatherFrameInput;

// Packs a currently experimental candidate primary segment: UPD, current conditions, and
// today's high/low values. Later forecast segments are not included because
// their native boundary has not been proven.
// Returns false if a value cannot be encoded.
bool weather_frame_build(const WeatherFrameInput* input, uint8_t frame[WEATHER_FRAME_SIZE]);

#endif  // WEATHER_LIFE_WEATHER_FRAME_H
