// See weather_frame.h for the experimental status of this encoding.

#include "weather_frame.h"
#include "bit_packer.h"
#include <math.h>
#include <string.h>

// usbwr.exe rounds decimal temperatures to signed integer degrees in 9 bits.
static uint16_t encode_temperature(float temperature_c)
{
    long rounded = lroundf(temperature_c);
    if (rounded < -256) rounded = -256;
    if (rounded > 255) rounded = 255;
    if (rounded < 0) return (uint16_t)(0x200 + rounded);
    return (uint16_t)rounded;
}

static uint16_t encode_uv_index(int uv_index)
{
    if (uv_index < 0) uv_index = 0;
    if (uv_index > 31) uv_index = 31;
    return (uint16_t)uv_index;
}

static bool append_unsigned(BitPacker* packer, uint16_t value, int bit_count)
{
    if (!packer || bit_count < 1 || bit_count > 16) return false;
    if (bit_count < 16 && value >= (uint16_t)(1u << bit_count)) return false;
    bit_packer_append(packer, value, bit_count);
    return true;
}

bool weather_frame_append_upd(BitPacker* packer, const WeatherFrameTime* time)
{
    if (!packer || !time) return false;
    if (time->month < 1 || time->month > 12) return false;
    if (time->day < 1 || time->day > 31) return false;
    if (time->hour < 0 || time->hour > 23) return false;
    if (time->minute < 0 || time->minute > 59) return false;

    bit_packer_append(packer, (uint16_t)time->month, 4);
    bit_packer_append(packer, (uint16_t)time->day, 5);
    bit_packer_append(packer, (uint16_t)time->hour, 5);
    bit_packer_append(packer, (uint16_t)time->minute, 6);
    return true;
}

bool weather_frame_begin_native_primary(BitPacker* packer,
                                        uint8_t frame[WEATHER_FRAME_SIZE])
{
    if (!packer || !frame) return false;

    frame[1] |= 0x07;
        memset(frame + WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET, 0x10,
            WEATHER_FRAME_SIZE - WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET);
    bit_packer_init_at(packer, frame, WEATHER_FRAME_SIZE,
                       WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET,
                       WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET);
    return true;
}

bool weather_frame_begin_native_selector6(
    BitPacker* packer, const uint8_t* readback_cells, size_t readback_size,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    if (!packer || !readback_cells || !frame ||
        readback_size < WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS) {
        return false;
    }

    for (size_t i = 0; i < WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS; i++) {
        frame[i] = readback_cells[i] & 0x0f;
    }
    frame[1] |= 0x07;
    memset(frame + WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS, 0x10,
           WEATHER_FRAME_NATIVE_SEGMENT_CELLS -
               WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS);
    bit_packer_init_at(packer, frame, WEATHER_FRAME_NATIVE_SEGMENT_CELLS,
                       WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS,
                       WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET);
    return true;
}

bool weather_frame_append_native_selector6_fields(
    BitPacker* packer, const WeatherFrameNativeSelector6Fields* fields)
{
    if (!packer || !fields || !packer->native_cursor ||
        packer->native_byte_offset != WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET ||
        packer->native_bit_offset != WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET ||
        packer->buffer_size < WEATHER_FRAME_NATIVE_SEGMENT_CELLS) {
        return false;
    }

    bit_packer_append(packer, fields->temperature_high, 9);
    bit_packer_append(packer, fields->temperature_low, 9);
    bit_packer_append(packer, fields->morning_icon_index, 7);
    bit_packer_append(packer, fields->wind_speed, 11);
    bit_packer_append(packer, fields->beaufort, 2);
    bit_packer_append(packer, fields->weather_word, 8);
    bit_packer_append(packer, fields->afternoon_direction_index, 5);
    bit_packer_append(packer, fields->afternoon_wind_speed, 7);
    bit_packer_append(packer, fields->afternoon_beaufort, 8);
    bit_packer_append(packer, fields->afternoon_weather_word, 5);
    bit_packer_append(packer, fields->afternoon_humidity, 8);
    return packer->bit_position ==
           WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET * 8 +
               WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET +
               WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_LENGTH;
}

static uint8_t native_checksum_table_value(uint8_t value)
{
    for (int bit = 0; bit < 8; bit++) {
        value = (value & 0x80)
                    ? (uint8_t)((value << 1) ^ 0x31)
                    : (uint8_t)(value << 1);
    }
    return value;
}

bool weather_frame_finalize_native_segment(
    uint8_t* frame, size_t cell_count, size_t segment_bit_length)
{
    uint8_t checksum = 0;
    size_t logical_bytes;
    size_t checksum_cell;

    if (!frame || (segment_bit_length != 64 && segment_bit_length != 128) ||
        cell_count < segment_bit_length / 4) {
        return false;
    }

    logical_bytes = (segment_bit_length - 8) / 8;
    checksum_cell = (segment_bit_length - 8) / 4;
    for (size_t i = 0; i < logical_bytes; i++) {
        uint8_t value =
            (uint8_t)(((frame[i * 2] & 0x0f) << 4) |
                      (frame[i * 2 + 1] & 0x0f));
        checksum ^= native_checksum_table_value(value);
    }

    frame[checksum_cell] |= (uint8_t)(0x10 | (checksum >> 4));
    frame[checksum_cell + 1] |= (uint8_t)(checksum & 0x0f);
    return true;
}

static bool finalize_native_primary(BitPacker* packer,
                                    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS],
                                    size_t field_bit_length)
{
    const size_t expected_position =
        WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET * 8 +
        WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET +
        field_bit_length;
    const size_t data_end_bit =
        WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_OFFSET + field_bit_length;

    if (!packer || !frame || !packer->native_cursor ||
        packer->buffer != frame ||
        packer->buffer_size < WEATHER_FRAME_NATIVE_SEGMENT_CELLS ||
        data_end_bit > 120 ||
        packer->bit_position != expected_position) {
        return false;
    }

    for (size_t pad_bit = data_end_bit; pad_bit < 120; pad_bit++) {
        bit_packer_append(packer, 0, 1);
    }
    if (packer->bit_position != expected_position + 120 - data_end_bit) {
        return false;
    }

    return weather_frame_finalize_native_segment(
        frame, WEATHER_FRAME_NATIVE_SEGMENT_CELLS, 128);
}

bool weather_frame_finalize_native_selector6(
    BitPacker* packer, uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    return finalize_native_primary(
        packer, frame, WEATHER_FRAME_NATIVE_SELECTOR6_FIELDS_BIT_LENGTH);
}

bool weather_frame_build_native_header(
    const uint8_t* readback_cells, size_t readback_size,
    const uint8_t values[WEATHER_FRAME_NATIVE_HEADER_VALUES],
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    BitPacker packer;

    if (!values || !weather_frame_begin_native_selector6(
                       &packer, readback_cells, readback_size, frame)) {
        return false;
    }
    for (size_t i = 0; i < WEATHER_FRAME_NATIVE_HEADER_VALUES; i++) {
        bit_packer_append(&packer, values[i], 8);
    }
    return finalize_native_primary(&packer, frame,
                                   WEATHER_FRAME_NATIVE_HEADER_VALUES * 8);
}

bool weather_frame_build_native_selector6(
    const uint8_t* readback_cells, size_t readback_size,
    const WeatherFrameNativeSelector6Fields* fields,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    BitPacker packer;

    if (!frame ||
        !weather_frame_begin_native_selector6(&packer, readback_cells,
                                              readback_size, frame) ||
        !weather_frame_append_native_selector6_fields(&packer, fields) ||
        !weather_frame_finalize_native_selector6(&packer, frame)) {
        return false;
    }
    return true;
}

static bool native_forecast_cursor_ready(const BitPacker* packer,
                                        const uint8_t* frame)
{
    return packer && frame && packer->native_cursor &&
           packer->buffer == frame &&
           packer->native_byte_offset == WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET &&
           packer->native_bit_offset == WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET &&
           packer->buffer_size >= WEATHER_FRAME_NATIVE_SEGMENT_CELLS;
}

bool weather_frame_build_native_later_day_first(
    const uint8_t* readback_cells, size_t readback_size,
    const WeatherFrameNativeLaterDayFirstFields* fields,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    BitPacker packer;

    if (!fields || !weather_frame_begin_native_selector6(
                       &packer, readback_cells, readback_size, frame) ||
        !native_forecast_cursor_ready(&packer, frame)) {
        return false;
    }

    bit_packer_append(&packer, fields->temperature_high, 9);
    bit_packer_append(&packer, fields->temperature_low, 9);
    bit_packer_append(&packer, fields->morning_icon_index, 7);
    bit_packer_append(&packer, fields->wind_speed, 8);
    bit_packer_append(&packer, fields->weather_direction_index, 5);
    bit_packer_append(&packer, fields->humidity, 7);
    bit_packer_append(&packer, fields->afternoon_icon_index, 7);
    bit_packer_append(&packer, fields->afternoon_wind_speed, 8);
    bit_packer_append(&packer, fields->afternoon_direction_index, 5);
    bit_packer_append(&packer, fields->afternoon_humidity, 7);
    bit_packer_append(&packer, fields->night_icon_index, 7);

    if (packer.bit_position != WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET * 8 +
                                   WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET +
                                   WEATHER_FRAME_NATIVE_LATER_DAY_FIRST_BIT_LENGTH) {
        return false;
    }
    return finalize_native_primary(&packer, frame,
                                  WEATHER_FRAME_NATIVE_LATER_DAY_FIRST_BIT_LENGTH);
}

bool weather_frame_build_native_later_day_second(
    const uint8_t* readback_cells, size_t readback_size,
    const WeatherFrameNativeLaterDaySecondFields* fields,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    BitPacker packer;

    if (!fields || !weather_frame_begin_native_selector6(
                       &packer, readback_cells, readback_size, frame) ||
        !native_forecast_cursor_ready(&packer, frame)) {
        return false;
    }

    bit_packer_append(&packer, fields->night_wind_speed, 8);
    bit_packer_append(&packer, fields->night_direction_index, 5);
    bit_packer_append(&packer, fields->night_humidity, 7);
    bit_packer_append(&packer, fields->sunrise_hour, 5);
    bit_packer_append(&packer, fields->sunrise_minute, 6);
    bit_packer_append(&packer, fields->sunset_hour, 5);
    bit_packer_append(&packer, fields->sunset_minute, 6);
    bit_packer_append(&packer, fields->precipitation_percent, 7);
    bit_packer_append(&packer, fields->rainfall, 8);
    bit_packer_append(&packer, fields->sunshine_hours, 5);
    bit_packer_append(&packer, fields->uv_index, 5);
    bit_packer_append(&packer, fields->dew_point, 8);

    if (packer.bit_position != WEATHER_FRAME_NATIVE_PAYLOAD_OFFSET * 8 +
                                   WEATHER_FRAME_NATIVE_PAYLOAD_BIT_OFFSET +
                                   WEATHER_FRAME_NATIVE_LATER_DAY_SECOND_BIT_LENGTH) {
        return false;
    }
    return finalize_native_primary(&packer, frame,
                                  WEATHER_FRAME_NATIVE_LATER_DAY_SECOND_BIT_LENGTH);
}

bool weather_frame_build_proven_upd(const WeatherFrameTime* time,
                                    uint8_t frame[WEATHER_FRAME_SIZE])
{
    BitPacker packer;

    if (!time || !frame) return false;

    memset(frame, 0, WEATHER_FRAME_SIZE);
    if (!weather_frame_begin_native_primary(&packer, frame)) return false;
    return weather_frame_append_upd(&packer, time);
}

bool weather_frame_build(const WeatherFrameInput* input, uint8_t frame[WEATHER_FRAME_SIZE])
{
    if (!input || !frame) return false;

    memset(frame, 0, WEATHER_FRAME_SIZE);

    BitPacker packer;
    bit_packer_init(&packer, frame, WEATHER_FRAME_SIZE);

    if (!weather_frame_append_upd(&packer, &input->time)) return false;

    // Experimental schema order from the archived named-record response.
    bit_packer_append(&packer, encode_temperature(input->current_temperature_c), 9);
    bit_packer_append(&packer, encode_temperature(input->background_temperature_c), 9);
    if (!append_unsigned(&packer, input->weather_code, 6)) return false;
    if (!append_unsigned(&packer, input->pressure_hpa, 11)) return false;
    if (!append_unsigned(&packer, input->pressure_trend, 2)) return false;
    if (!append_unsigned(&packer, input->wind_speed, 6)) return false;
    if (!append_unsigned(&packer, input->beaufort_scale, 6)) return false;
    if (!append_unsigned(&packer, input->wind_word, 4)) return false;
    if (!append_unsigned(&packer, input->humidity_percent, 7)) return false;
    if (!append_unsigned(&packer, input->visibility, 7)) return false;
    if (!append_unsigned(&packer, encode_uv_index(input->uv_index), 5)) return false;
    bit_packer_append(&packer, encode_temperature(input->dew_point_c), 9);

    // UPD (20) + current fields (81) + today's high/low (18) = 119 bits.
    bit_packer_append(&packer, encode_temperature(input->high_temperature_c[0]), 9);
    bit_packer_append(&packer, encode_temperature(input->low_temperature_c[0]), 9);

    return true;
}
