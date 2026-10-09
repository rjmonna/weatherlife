// Weather-frame primitives and an offline candidate builder. The original
// serializer stores one data nibble per byte; its 128-bit primary segment is
// 32 such cells, sent as two 16-byte CAL_USB_WRITE reports. The candidate
// builder is not an implementation of that native wire representation.

#ifndef WEATHER_LIFE_WEATHER_FRAME_H
#define WEATHER_LIFE_WEATHER_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include "bit_packer.h"

#define WEATHER_FRAME_FORECAST_DAYS 5  // today + 4 more
#define WEATHER_FRAME_SIZE 16          // working-buffer size; not the full native segment
#define WEATHER_FRAME_NATIVE_SEGMENT_LOGICAL_BYTES 16
#define WEATHER_FRAME_NATIVE_SEGMENT_CELLS 32
#define WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_OFFSET 40
#define WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_LENGTH 79
#define WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET 10 // nibble-cell index in the native buffer
#define WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET 4 // first low-nibble bit mask index

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

// Prepare the first 16 native nibble cells for the proven case-5 cursor.
// Header cells other than the known control bits must be supplied by the caller.
bool weather_frame_begin_native_primary(BitPacker* packer,
                                        uint8_t frame[WEATHER_FRAME_SIZE]);

// Build an UPD-only first-report working buffer. This omits the remaining
// native cells, checksum, and undecoded header, so it is not sendable.
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

// Packs a candidate using the archived service-record schema. Its order and
// encoding differ from the proven native selector-6 layout; it is offline only.
// Returns false if a value cannot be encoded.
bool weather_frame_build(const WeatherFrameInput* input, uint8_t frame[WEATHER_FRAME_SIZE]);

#endif  // WEATHER_LIFE_WEATHER_FRAME_H
