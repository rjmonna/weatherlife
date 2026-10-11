#include "bit_packer.h"
#include "device_registration.h"
#include "weather_frame.h"
#include "weather_icons.h"
#include "weather_response.h"
#include "weather_sender.h"
#include "weather_session.h"

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

static int test_weather_sender(void)
{
    const char response[] =
        "TEMP <9> <12.0>;\n"
        "DAY1 20260918;\n"
        "TEMPH <9> <6.9>;\n"
        "TEMPL <9> <-3.8>;\n"
        "MORING_ICON <7> <SO____>;\n"
        "WS <11> <-2.5>;\n"
        "WBFT <2> <N/A>;\n"
        "WW <8> <7.9>;\n"
        "HUM <0> <0>;\n"
        "AFTERNOON_N <5> <NW>;\n"
        "AFWS <7> <N/A>;\n"
        "AFBFT <8> <3.4>;\n"
        "AFWW <5> <-1>;\n"
        "AFHUM <8> <-2.6>;\n"
        "DAY2 20260919;\n"
        "TEMPH <9> <20.4>;\n"
        "TEMPL <9> <10.8>;\n"
        "MORING_ICON <7> <MO____>;\n"
        "WS <8> <6>;\n"
        "WBFT <0> <0>;\n"
        "WW <5> <NW>;\n"
        "HUM <7> <N/A>;\n"
        "AFTERNOON_N <7> <SO____>;\n"
        "AFWS <8> <N/A>;\n"
        "AFBFT <0> <0>;\n"
        "AFWW <5> <WN>;\n"
        "AFHUM <7> <45>;\n"
        "NIGHT_ICON <7> <MBGR2_>;\n"
        "NWS <8> <3.9>;\n"
        "NBFT <0> <0>;\n"
        "NWW <5> <ON>;\n"
        "NHUM <7> <N/A>;\n"
        "SUNR <11> <06:30>;\n"
        "SUNS <11> <20:15>;\n"
        "PPCP <7> <40>;\n"
        "RAINMS <8> <2.5>;\n"
        "SUNTIME <5> <5>;\n"
        "UVI <5> <4>;\n"
        "DEWP <8> <N/A>;\n";
    WeatherLegacyResponse parsed;
    uint8_t prefix[WEATHER_FRAME_SIZE];
    uint8_t actual[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
    uint8_t expected[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
    WeatherFrameNativeSelector6Fields selector6 = {
        .temperature_high = 7,
        .temperature_low = (uint16_t)-3 & 0x1ff,
        .morning_icon_index = 0,
        .wind_speed = (uint16_t)-2 & 0x7ff,
        .beaufort = 3,
        .weather_word = 7,
        .afternoon_direction_index = 1,
        .afternoon_wind_speed = 0x7f,
        .afternoon_beaufort = 34,
        .afternoon_weather_word = 0x1f,
        .afternoon_humidity = 0x83
    };
    WeatherFrameNativeLaterDayFirstFields day2_first = {
        .temperature_high = 20,
        .temperature_low = 10,
        .morning_icon_index = 0,
        .wind_speed = 6,
        .weather_direction_index = 1,
        .humidity = 0x7f,
        .afternoon_icon_index = 0,
        .afternoon_wind_speed = 0xff,
        .afternoon_direction_index = 1,
        .afternoon_humidity = 45,
        .night_icon_index = 78
    };
    WeatherFrameNativeLaterDaySecondFields day2_second = {
        .night_wind_speed = 3,
        .night_direction_index = 7,
        .night_humidity = 0x7f,
        .sunrise_hour = 6,
        .sunrise_minute = 30,
        .sunset_hour = 20,
        .sunset_minute = 15,
        .precipitation_percent = 40,
        .rainfall = 3,
        .sunshine_hours = 5,
        .uv_index = 4,
        .dew_point = 0xff
    };

    memset(prefix, 0x10, sizeof(prefix));
    ASSERT_TRUE(weather_parse_legacy_records(response, strlen(response), &parsed));

    ASSERT_INT(WEATHER_SENDER_OK, weather_sender_build_weather_segment(
        6, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_TRUE(weather_frame_build_native_selector6(
        prefix, sizeof(prefix), &selector6, expected));
    ASSERT_TRUE(memcmp(actual, expected, sizeof(actual)) == 0);

    ASSERT_INT(WEATHER_SENDER_OK, weather_sender_build_weather_segment(
        8, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_TRUE(weather_frame_build_native_later_day_first(
        prefix, sizeof(prefix), &day2_first, expected));
    ASSERT_TRUE(memcmp(actual, expected, sizeof(actual)) == 0);

    ASSERT_INT(WEATHER_SENDER_OK, weather_sender_build_weather_segment(
        9, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_TRUE(weather_frame_build_native_later_day_second(
        prefix, sizeof(prefix), &day2_second, expected));
    ASSERT_TRUE(memcmp(actual, expected, sizeof(actual)) == 0);

    ASSERT_INT(WEATHER_SENDER_UNSUPPORTED_SELECTOR,
               weather_sender_build_weather_segment(
                   1, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_INT(WEATHER_SENDER_UNSUPPORTED_SELECTOR,
               weather_sender_build_weather_segment(
                   7, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_INT(WEATHER_SENDER_UNSUPPORTED_SELECTOR,
               weather_sender_build_weather_segment(
                   18, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_INT(WEATHER_SENDER_MISSING_DAY,
               weather_sender_build_weather_segment(
                   10, &parsed, prefix, sizeof(prefix), actual));

    memcpy(parsed.days[0].records[0].value, "<bad>", sizeof("<bad>"));
    ASSERT_INT(WEATHER_SENDER_INVALID_VALUE,
               weather_sender_build_weather_segment(
                   6, &parsed, prefix, sizeof(prefix), actual));
    ASSERT_INT(WEATHER_SENDER_INVALID_VALUE,
               weather_sender_build_weather_segment(
                   6, &parsed, prefix,
                   WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS - 1, actual));
    return 0;
}

static int test_weather_frame(void)
{
    uint8_t frame[WEATHER_FRAME_SIZE];
    BitPacker packer;
    WeatherFrameTime time = {12, 31, 23, 59};
    WeatherFrameInput input = {0};

    memset(frame, 0, sizeof(frame));
    bit_packer_init(&packer, frame, sizeof(frame));
    ASSERT_TRUE(weather_frame_append_upd(&packer, &time));
    ASSERT_INT(20, (int)packer.bit_position);
    ASSERT_INT(0xcf, frame[0]);
    ASSERT_INT(0xdf, frame[1]);
    ASSERT_INT(0xb0, frame[2]);

    memset(frame, 0, sizeof(frame));
    ASSERT_TRUE(weather_frame_begin_native_primary(&packer, frame));
    ASSERT_INT(84, (int)packer.bit_position);
    ASSERT_INT(0x07, frame[1]);
    ASSERT_INT(0x10, frame[10]);
    ASSERT_TRUE(weather_frame_append_upd(&packer, &time));
    ASSERT_INT(104, (int)packer.bit_position);
    ASSERT_INT(0x1c, frame[10]);
    ASSERT_INT(0x1f, frame[11]);
    ASSERT_INT(0x1d, frame[12]);
    ASSERT_INT(0x1f, frame[13]);
    ASSERT_INT(0x1b, frame[14]);
    ASSERT_INT(0x10, frame[15]);

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

static int test_selector6_prefix(void)
{
    uint8_t readback[WEATHER_FRAME_SIZE];
    uint8_t cells[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
    BitPacker packer;
    WeatherFrameNativeSelector6Fields fixture_fields = {
        .temperature_high = 6,
        .temperature_low = 0x1fc,
        .morning_icon_index = 79,
        .wind_speed = 9,
        .beaufort = 2,
        .weather_word = 0,
        .afternoon_direction_index = 8,
        .afternoon_wind_speed = 20,
        .afternoon_beaufort = 34,
        .afternoon_weather_word = 0,
        .afternoon_humidity = 82
    };
    WeatherFrameNativeLaterDayFirstFields day2_first = {
        .temperature_high = 5,
        .temperature_low = 0x1fc,
        .morning_icon_index = 79,
        .wind_speed = 10,
        .weather_direction_index = 7,
        .humidity = 97,
        .afternoon_icon_index = 79,
        .afternoon_wind_speed = 15,
        .afternoon_direction_index = 7,
        .afternoon_humidity = 66,
        .night_icon_index = 79
    };
    WeatherFrameNativeLaterDaySecondFields day2_second = {
        .night_wind_speed = 9,
        .night_direction_index = 7,
        .night_humidity = 84,
        .sunrise_hour = 7,
        .sunrise_minute = 11,
        .sunset_hour = 18,
        .sunset_minute = 36,
        .precipitation_percent = 5,
        .rainfall = 0,
        .sunshine_hours = 7,
        .uv_index = 3,
        .dew_point = 0xff
    };
    const uint8_t expected_fixture_segment[WEATHER_FRAME_NATIVE_SEGMENT_CELLS] = {
        0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x13, 0x17, 0x1f, 0x12, 0x17,
        0x18, 0x10, 0x19, 0x18, 0x10, 0x11, 0x10, 0x15,
        0x10, 0x18, 0x18, 0x10, 0x1a, 0x14, 0x1c, 0x16
    };
    const uint8_t expected_day2_first[WEATHER_FRAME_NATIVE_SEGMENT_CELLS] = {
        0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x12, 0x1f, 0x1f, 0x12, 0x17,
        0x18, 0x15, 0x11, 0x1f, 0x10, 0x1c, 0x1f, 0x10,
        0x1f, 0x13, 0x1c, 0x12, 0x19, 0x1e, 0x18, 0x17
    };
    const uint8_t expected_day2_second[WEATHER_FRAME_NATIVE_SEGMENT_CELLS] = {
        0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x19, 0x13, 0x1d, 0x14, 0x13,
        0x19, 0x17, 0x12, 0x19, 0x10, 0x12, 0x18, 0x10,
        0x11, 0x1c, 0x17, 0x1f, 0x1e, 0x10, 0x16, 0x19
    };
    WeatherFrameNativeSelector6Fields fields = {
        .temperature_high = 0x1ff,
        .temperature_low = 0x1ff,
        .morning_icon_index = 0x7f,
        .wind_speed = 0x7ff,
        .beaufort = 0x03,
        .weather_word = 0xff,
        .afternoon_direction_index = 0x1f,
        .afternoon_wind_speed = 0x7f,
        .afternoon_beaufort = 0xff,
        .afternoon_weather_word = 0x1f,
        .afternoon_humidity = 0xff
    };

    memset(readback, 0x10, sizeof(readback));
    ASSERT_TRUE(weather_frame_begin_native_selector6(&packer, readback,
                                                       sizeof(readback), cells));
    ASSERT_INT(0x00, cells[0]);
    ASSERT_INT(0x07, cells[1]);
    for (int i = 2; i < WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS; i++) {
        ASSERT_INT(0x00, cells[i]);
    }
    for (int i = WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS;
         i < WEATHER_FRAME_NATIVE_SEGMENT_CELLS; i++) {
        ASSERT_INT(0x10, cells[i]);
    }
    ASSERT_INT(84, (int)packer.bit_position);

    readback[1] = 0x19;
    for (int i = 0; i < 10; i++) readback[i + 2] = (uint8_t)(0x20 | i);
    ASSERT_TRUE(weather_frame_begin_native_selector6(&packer, readback,
                                                       sizeof(readback), cells));
    ASSERT_INT(0x00, cells[0]);
    ASSERT_INT(0x0f, cells[1]);
    for (int i = 0; i < 8; i++) ASSERT_INT(i, cells[i + 2]);
    ASSERT_TRUE(weather_frame_append_native_selector6_fields(&packer, &fields));
    ASSERT_INT(163, (int)packer.bit_position);
    for (int i = WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS; i < 29; i++) {
        ASSERT_INT(0x1f, cells[i]);
    }
    ASSERT_INT(0x1e, cells[29]);
    ASSERT_TRUE(weather_frame_finalize_native_selector6(&packer, cells));
    ASSERT_INT(164, (int)packer.bit_position);
    ASSERT_TRUE((cells[30] & 0xf0) == 0x10);
    ASSERT_TRUE((cells[31] & 0xf0) == 0x10);
    ASSERT_TRUE(!weather_frame_finalize_native_selector6(&packer, cells));

    readback[1] = 0x1b;
    ASSERT_TRUE(weather_frame_begin_native_selector6(&packer, readback,
                                                       sizeof(readback), cells));
    ASSERT_INT(0x0f, cells[1]);
    ASSERT_INT(0x00, cells[2]);
    ASSERT_TRUE(!weather_frame_begin_native_selector6(&packer, readback, 9,
                                                        cells));
    ASSERT_TRUE(!weather_frame_begin_native_selector6(NULL, readback,
                                                        sizeof(readback), cells));
    ASSERT_TRUE(weather_frame_build_native_selector6(readback, sizeof(readback),
                                                       &fields, cells));
    ASSERT_TRUE((cells[30] & 0xf0) == 0x10);
    ASSERT_TRUE((cells[31] & 0xf0) == 0x10);
    ASSERT_TRUE(!weather_frame_build_native_selector6(readback, 9, &fields,
                                                        cells));
    ASSERT_TRUE(!weather_frame_build_native_selector6(readback,
                                                        sizeof(readback), NULL,
                                                        cells));

    memset(readback, 0x10, sizeof(readback));
    ASSERT_TRUE(weather_frame_build_native_selector6(readback, sizeof(readback),
                                                       &fixture_fields, cells));
    ASSERT_TRUE(memcmp(cells, expected_fixture_segment,
                       sizeof(expected_fixture_segment)) == 0);
    ASSERT_TRUE(weather_frame_build_native_later_day_first(
        readback, sizeof(readback), &day2_first, cells));
    ASSERT_TRUE(memcmp(cells, expected_day2_first,
                       sizeof(expected_day2_first)) == 0);
    ASSERT_TRUE(weather_frame_build_native_later_day_second(
        readback, sizeof(readback), &day2_second, cells));
    ASSERT_TRUE(memcmp(cells, expected_day2_second,
                       sizeof(expected_day2_second)) == 0);
    return 0;
}

static uint8_t reference_checksum_table_value(uint8_t value)
{
    for (int bit = 0; bit < 8; bit++) {
        value = (value & 0x80)
                    ? (uint8_t)((value << 1) ^ 0x31)
                    : (uint8_t)(value << 1);
    }
    return value;
}

static int test_native_segment_checksum(const uint8_t* cells, size_t segment_bits)
{
    uint8_t checksum = 0;
    size_t logical_bytes = (segment_bits - 8) / 8;
    size_t checksum_cell = (segment_bits - 8) / 4;

    for (size_t i = 0; i < logical_bytes; i++) {
        uint8_t value =
            (uint8_t)(((cells[i * 2] & 0x0f) << 4) |
                      (cells[i * 2 + 1] & 0x0f));
        checksum ^= reference_checksum_table_value(value);
    }
    return cells[checksum_cell] == (uint8_t)(0x10 | (checksum >> 4)) &&
           cells[checksum_cell + 1] ==
               (uint8_t)(0x10 | (checksum & 0x0f));
}

static int test_weather_session(void)
{
    uint8_t init_frame[WEATHER_FRAME_SIZE];
    uint8_t response[WEATHER_SESSION_CONTROL_CELLS];
    uint8_t control_frame[WEATHER_SESSION_CONTROL_CELLS];
    WeatherSessionControlFields fields = {
        .mode_flags = 5,
        .field_a = 10,
        .field_b = 5,
        .field_c = 3,
        .field_d = 2,
        .session_flag = true
    };
    WeatherSessionState state;

    ASSERT_TRUE(weather_session_build_init_frame(0xa5, true, init_frame));
    ASSERT_INT(0x10, init_frame[0]);
    ASSERT_INT(0x1a, init_frame[1]);
    ASSERT_INT(0x1a, init_frame[2]);
    ASSERT_INT(0x15, init_frame[3]);
    ASSERT_INT(0x18, init_frame[4]);
    ASSERT_TRUE(test_native_segment_checksum(init_frame, 64));
    ASSERT_TRUE(!weather_frame_finalize_native_segment(init_frame,
                                                        sizeof(init_frame), 80));

    memset(response, 0x10, sizeof(response));
    response[1] = 0x19;
    ASSERT_TRUE(weather_session_build_control_frame(
        response, sizeof(response), &fields, control_frame));
    ASSERT_INT(0x0d, control_frame[1]);
    ASSERT_INT(0x01, control_frame[7]);
    ASSERT_INT(0x16, control_frame[8]);
    ASSERT_INT(0x1a, control_frame[9]);
    ASSERT_INT(0x1b, control_frame[10]);
    ASSERT_INT(0x1a, control_frame[11]);
    ASSERT_TRUE(test_native_segment_checksum(control_frame, 64));
    ASSERT_TRUE(!weather_session_build_control_frame(
        response, WEATHER_SESSION_CONTROL_PREFIX_CELLS - 1, &fields,
        control_frame));

    weather_session_state_init(&state);
    ASSERT_INT(WEATHER_SESSION_RETRY_READ,
               weather_session_on_read(&state, false));
    ASSERT_INT(WEATHER_SESSION_RETRY_READ,
               weather_session_on_read(&state, false));
    ASSERT_INT(WEATHER_SESSION_RETRY_READ,
               weather_session_on_read(&state, false));
    ASSERT_INT(WEATHER_SESSION_RECOVER_DEVICE,
               weather_session_on_read(&state, false));
    ASSERT_INT(WEATHER_SESSION_SEND_INIT_FRAME,
               weather_session_on_read(&state, true));
    ASSERT_INT(WEATHER_SESSION_PROCESS_RESPONSE,
               weather_session_on_read(&state, true));
    ASSERT_INT(WEATHER_SESSION_INVALID_STATE,
               weather_session_on_read(NULL, true));
    return 0;
}

static int test_icons(void)
{
    ASSERT_INT(0, weather_icon_index("SO____"));
    ASSERT_INT(3, weather_icon_index("BD____"));
    ASSERT_INT(78, weather_icon_index("WBGR2_"));
    ASSERT_INT(0, weather_icon_index("MO____"));
    ASSERT_INT(78, weather_icon_index("MBGR2_"));
    ASSERT_INT(79, weather_icon_index("bd____"));
    ASSERT_INT(79, weather_icon_index(NULL));
    return 0;
}

int main(void)
{
    ASSERT_INT(0, test_bit_packer());
    ASSERT_INT(0, test_registration());
    ASSERT_INT(0, test_response_parser());
    ASSERT_INT(0, test_weather_sender());
    ASSERT_INT(0, test_weather_frame());
    ASSERT_INT(0, test_selector6_prefix());
    ASSERT_INT(0, test_weather_session());
    ASSERT_INT(0, test_icons());
    puts("native unit tests passed");
    return 0;
}
