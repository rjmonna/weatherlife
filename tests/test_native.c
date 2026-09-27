#include "bit_packer.h"
#include "device_registration.h"
#include "weather_frame.h"
#include "weather_response.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT(expected, actual) do { \
    int expected_value = (expected); \
    int actual_value = (actual); \
    if (expected_value != actual_value) { \
        fprintf(stderr, "%s:%d: expected %d, got %d\n", __FILE__, __LINE__, expected_value, actual_value); \
        return 1; \
    } \
} while (0)

static int test_bit_packer(void)
{
    uint8_t buffer[3] = {0};
    BitPacker packer;

    bit_packer_init(&packer, buffer, sizeof(buffer));
    ASSERT_INT(0, (int)packer.bit_position);
    bit_packer_append(&packer, 0x5, 3);
    bit_packer_append(&packer, 0x12, 5);
    bit_packer_append(&packer, 0x155, 9);
    ASSERT_INT(17, (int)packer.bit_position);
    ASSERT_INT(0xb2, buffer[0]);
    ASSERT_INT(0xaa, buffer[1]);
    ASSERT_INT(0x80, buffer[2]);

    memset(buffer, 0, sizeof(buffer));
    bit_packer_init_at(&packer, buffer, sizeof(buffer), 1, 4);
    bit_packer_append(&packer, 0x5, 3);
    ASSERT_INT(15, (int)packer.bit_position);
    ASSERT_INT(0x0a, buffer[1]);

    memset(buffer, 0, sizeof(buffer));
    bit_packer_init_at(&packer, buffer, sizeof(buffer), 1, 4);
    bit_packer_append(&packer, 0x1f, 5);
    ASSERT_INT(17, (int)packer.bit_position);
    ASSERT_INT(0x0f, buffer[1]);
    ASSERT_INT(0x08, buffer[2]);

    bit_packer_init(&packer, buffer, 1);
    buffer[0] = 0;
    bit_packer_append(&packer, 0xffff, 16);
    ASSERT_INT(8, (int)packer.bit_position);
    ASSERT_INT(0xff, buffer[0]);
    return 0;
}

static int test_registration(void)
{
    WeatherRegistrationId id = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
    uint8_t frame[WEATHER_REGISTRATION_FRAME_SIZE];
    uint8_t response[WEATHER_REGISTRATION_FRAME_SIZE] = {0};
    uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE];
    const uint8_t expected[] = {
        0x10, 0x18, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x16, 0x17, 0x18, 0x19, 0x10, 0x10, 0x14, 0x1b
    };

    weather_registration_build_frame(&id, frame);
    ASSERT_TRUE(memcmp(frame, expected, sizeof(expected)) == 0);

    response[0] = 0x10;
    response[1] = 0x19;
    for (int i = 0; i < WEATHER_REGISTRATION_ID_NIBBLES; i++) {
        response[i + 2] = (uint8_t)(0x20 | id.nibbles[i]);
    }
    weather_registration_decode_response(response, decoded);
    ASSERT_TRUE(weather_registration_response_matches(decoded, &id));
    decoded[5]++;
    ASSERT_TRUE(!weather_registration_response_matches(decoded, &id));
    decoded[5] = id.nibbles[3];
    decoded[0] = 1;
    ASSERT_TRUE(!weather_registration_response_matches(decoded, &id));
    return 0;
}

static int test_response_parser(void)
{
    const char response[] =
        "DES           BIT LENGTH    DATA\r\n"
        "; comment\n"
        "TEMP <9> <21.5>;\n"
        "HUM <7> <65>;\n"
        "WS <8> <36>;\n"
        "PRE <11> <1013>;\n"
        "RAINMS <8> <2.5>;\n"
        "DAY1 20260918;\n"
        "HIGH <9> <25>;\n";
    WeatherLegacyResponse parsed;
    WeatherData data;

    ASSERT_TRUE(weather_parse_legacy_records(response, strlen(response), &parsed));
    ASSERT_INT(5, (int)parsed.current_record_count);
    ASSERT_INT(1, (int)parsed.day_count);
    ASSERT_INT(1, (int)parsed.days[0].record_count);
    ASSERT_TRUE(strcmp(parsed.current[0].key, "TEMP") == 0);
    ASSERT_INT(9, parsed.current[0].bit_length);
    ASSERT_TRUE(strcmp(parsed.days[0].date, "20260918") == 0);

    ASSERT_TRUE(weather_parse_legacy_current(response, strlen(response), &data));
    ASSERT_TRUE(fabsf(data.temperature - 21.5f) < 0.001f);
    ASSERT_INT(65, data.humidity);
    ASSERT_TRUE(fabsf(data.wind_speed - 10.0f) < 0.001f);
    ASSERT_INT(1013, data.pressure);
    ASSERT_TRUE(fabsf(data.precipitation - 2.5f) < 0.001f);
    ASSERT_INT(0, data.weather_code);

    ASSERT_TRUE(!weather_parse_legacy_current("TEMP <9> <21>;\n", 16, &data));
    ASSERT_TRUE(!weather_parse_legacy_records("BROKEN\n", 7, &parsed));
    return 0;
}

static int test_weather_frame(void)
{
    uint8_t frame[WEATHER_FRAME_SIZE];
    BitPacker packer;
    WeatherFrameTime time = {12, 31, 23, 59};
    WeatherFrameInput input = {0};

    bit_packer_init(&packer, frame, sizeof(frame));
    ASSERT_TRUE(weather_frame_append_upd(&packer, &time));
    ASSERT_INT(20, (int)packer.bit_position);
    ASSERT_INT(0xcf, frame[0]);
    ASSERT_INT(0xdf, frame[1]);
    ASSERT_INT(0xfc, frame[2]);

    memset(frame, 0, sizeof(frame));
    ASSERT_TRUE(weather_frame_begin_native_primary(&packer, frame));
    ASSERT_INT(84, (int)packer.bit_position);
    ASSERT_INT(0x07, frame[1]);
    ASSERT_TRUE(weather_frame_append_upd(&packer, &time));
    ASSERT_INT(104, (int)packer.bit_position);

    time.month = 0;
    ASSERT_TRUE(!weather_frame_append_upd(&packer, &time));
    time.month = 12;
    input.time = time;
    input.current_temperature_c = -12.6f;
    input.background_temperature_c = 35.4f;
    input.weather_code = 63;
    input.pressure_hpa = 1013;
    input.pressure_trend = 2;
    input.wind_speed = 20;
    input.beaufort_scale = 5;
    input.wind_word = 15;
    input.humidity_percent = 100;
    input.visibility = 80;
    input.uv_index = 40;
    input.dew_point_c = -4.4f;
    input.high_temperature_c[0] = 26.4f;
    input.low_temperature_c[0] = -2.6f;
    ASSERT_TRUE(weather_frame_build(&input, frame));

    input.weather_code = 64;
    ASSERT_TRUE(!weather_frame_build(&input, frame));
    input.weather_code = 63;
    input.pressure_hpa = 2048;
    ASSERT_TRUE(!weather_frame_build(&input, frame));
    ASSERT_TRUE(!weather_frame_build(NULL, frame));
    ASSERT_TRUE(!weather_frame_build(&input, NULL));
    return 0;
}

int main(void)
{
    ASSERT_INT(0, test_bit_packer());
    ASSERT_INT(0, test_registration());
    ASSERT_INT(0, test_response_parser());
    ASSERT_INT(0, test_weather_frame());
    puts("native unit tests passed");
    return 0;
}
