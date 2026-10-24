// Weather-frame primitives and offline builders. The original serializer
// stores one data nibble per byte; its 128-bit primary segment is 32 cells,
// sent as two 16-byte CAL_USB_WRITE reports. The selector-6 Day-1 builder
// reaches the proven cell layout/checksum but requires preconverted field
// values and does not imply that the complete display protocol is understood.

#ifndef WEATHER_LIFE_WEATHER_FRAME_H
#define WEATHER_LIFE_WEATHER_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include "bit_packer.h"

#define WEATHER_FRAME_FORECAST_DAYS 5  // today + 4 more
#define WEATHER_FRAME_SIZE 16          // working-buffer size; not the full native segment
#define WEATHER_FRAME_NATIVE_SEGMENT_LOGICAL_BYTES 16
#define WEATHER_FRAME_NATIVE_SEGMENT_CELLS 32
#define WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS 10
#define WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_OFFSET 40
#define WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_LENGTH 79
#define WEATHER_FRAME_NATIVE_HEADER_VALUES 10

// Experimental: selectors 0-4 pack ten 8-bit values (usbwr.exe FUN_00401400).
// The LCD has no text segments, so their meaning is unproven; callers pass
// neutral values only to let the dongle's request sequence advance.
bool weather_frame_build_native_header(
    const uint8_t* readback_cells, size_t readback_size,
    const uint8_t values[WEATHER_FRAME_NATIVE_HEADER_VALUES],
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);
#define WEATHER_FRAME_NATIVE_LATER_DAY_FIRST_BIT_LENGTH 79
#define WEATHER_FRAME_NATIVE_LATER_DAY_SECOND_BIT_LENGTH 75
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

// Seed selector 6's 32-cell working segment from CAL_USB_READ payload cells.
// The first ten cells are low-nibble normalized and preserved except cell 1,
// where the native path ORs 0x07. Cells 10..31 are initialized as empty cells.
bool weather_frame_begin_native_selector6(
    BitPacker* packer, const uint8_t* readback_cells, size_t readback_size,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

typedef struct {
    uint16_t temperature_high;
    uint16_t temperature_low;
    uint16_t morning_icon_index;
    uint16_t wind_speed;
    uint16_t beaufort;
    uint16_t weather_word;
    uint16_t afternoon_direction_index;
    uint16_t afternoon_wind_speed;
    uint16_t afternoon_beaufort;
    uint16_t afternoon_weather_word;
    uint16_t afternoon_humidity;
} WeatherFrameNativeSelector6Fields;

// Append the proven 79-bit selector-6 Day-1 field sequence at its native
// cursor. Values must already use the original per-field conversions; excess
// high bits are discarded as in the native bit appender. HUM is consumed but
// has no field.
bool weather_frame_append_native_selector6_fields(
    BitPacker* packer, const WeatherFrameNativeSelector6Fields* fields);

// Append the native zero pad bit and finalize the 128-bit segment with the
// proven XOR-of-table checksum in nibble cells 30 and 31.
bool weather_frame_finalize_native_selector6(
    BitPacker* packer, uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

// Append the native XOR-of-table checksum for a 64- or 128-bit segment.
// Cells are nibble-expanded bytes and the caller must initialize the unused
// payload cells to 0x10 before packing.
bool weather_frame_finalize_native_segment(
    uint8_t* frame, size_t cell_count, size_t segment_bit_length);

// Build a complete checksummed selector-6 Day-1 segment from readback cells
// and already-converted field values. This does not infer unknown conversions.
bool weather_frame_build_native_selector6(
    const uint8_t* readback_cells, size_t readback_size,
    const WeatherFrameNativeSelector6Fields* fields,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

typedef struct {
    uint16_t temperature_high;
    uint16_t temperature_low;
    uint16_t morning_icon_index;
    uint16_t wind_speed;
    uint16_t weather_direction_index;
    uint16_t humidity;
    uint16_t afternoon_icon_index;
    uint16_t afternoon_wind_speed;
    uint16_t afternoon_direction_index;
    uint16_t afternoon_humidity;
    uint16_t night_icon_index;
} WeatherFrameNativeLaterDayFirstFields;

typedef struct {
    uint16_t night_wind_speed;
    uint16_t night_direction_index;
    uint16_t night_humidity;
    uint16_t sunrise_hour;
    uint16_t sunrise_minute;
    uint16_t sunset_hour;
    uint16_t sunset_minute;
    uint16_t precipitation_percent;
    uint16_t rainfall;
    uint16_t sunshine_hours;
    uint16_t uv_index;
    uint16_t dew_point;
} WeatherFrameNativeLaterDaySecondFields;

// Selectors 8/10/12/14/16: append the later-day records through NIGHT_ICON.
// Zero-width WBFT and AFBFT records are consumed by the native parser but are
// intentionally absent from this wire-value structure.
bool weather_frame_build_native_later_day_first(
    const uint8_t* readback_cells, size_t readback_size,
    const WeatherFrameNativeLaterDayFirstFields* fields,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

// Selectors 9/11/13/15/17: append the records from NWS through DEWP.
// Zero-width NBFT is consumed but not emitted. Sunrise/sunset components are
// already converted to their native 5-bit hour and 6-bit minute fields.
bool weather_frame_build_native_later_day_second(
    const uint8_t* readback_cells, size_t readback_size,
    const WeatherFrameNativeLaterDaySecondFields* fields,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

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
