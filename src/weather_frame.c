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
