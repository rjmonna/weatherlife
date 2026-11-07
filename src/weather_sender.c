#include "weather_sender.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "weather_icons.h"

typedef struct {
    const WeatherLegacyRecord* records;
    size_t count;
} RecordSet;

static const char* value_text(const WeatherLegacyRecord* record)
{
    const char* value;
    size_t length;

    if (!record) return NULL;
    value = record->value;
    length = strlen(value);
    if (length >= 2 && value[0] == '<' && value[length - 1] == '>') {
        return value + 1;
    }
    return value;
}

static bool copy_value(const WeatherLegacyRecord* record, char* output,
                       size_t output_size)
{
    const char* value = value_text(record);
    size_t length;

    if (!value || !output || output_size == 0) return false;
    length = strlen(value);
    if (length >= output_size) return false;
    memcpy(output, value, length);
    output[length] = '\0';
    if (value != record->value) output[length - 1] = '\0';
    return true;
}

static const WeatherLegacyRecord* find_record(const RecordSet* set,
                                               const char* key)
{
    if (!set || !key) return NULL;
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->records[i].key, key) == 0) return &set->records[i];
    }
    return NULL;
}

static bool read_record(const RecordSet* set, const char* key,
                        char output[WEATHER_RESPONSE_VALUE_SIZE])
{
    return copy_value(find_record(set, key), output,
                      WEATHER_RESPONSE_VALUE_SIZE);
}

static bool parse_integer_prefix(const char* text, long* result)
{
    char* end;

    if (!text || !*text || !result) return false;
    errno = 0;
    *result = strtol(text, &end, 10);
    return end != text && *end == '\0' && errno != ERANGE;
}

static bool parse_integer_value(const char* text, long* result)
{
    char* end;

    if (!text || !result) return false;
    errno = 0;
    *result = strtol(text, &end, 10);
    if (end == text) {
        *result = 0;
        return true;
    }
    return errno != ERANGE;
}

static bool parse_rounded_signed_value(const char* text, long* result)
{
    const char* magnitude_text = text;
    bool negative = false;
    long magnitude;
    char whole[WEATHER_RESPONSE_VALUE_SIZE];
    const char* dot;
    size_t whole_length;

    if (!text || !result) return false;
    if (*magnitude_text == '-') {
        negative = true;
        magnitude_text++;
    } else if (*magnitude_text == '+') {
        magnitude_text++;
    }
    dot = strchr(magnitude_text, '.');
    whole_length = dot ? (size_t)(dot - magnitude_text) : strlen(magnitude_text);
    if (whole_length == 0 || whole_length >= sizeof(whole)) return false;
    memcpy(whole, magnitude_text, whole_length);
    whole[whole_length] = '\0';
    if (!parse_integer_prefix(whole, &magnitude) ||
        magnitude < 0 || magnitude > LONG_MAX - 1) {
        return false;
    }
    if (dot && dot[1] != '\0') {
        long suffix;
        if (!parse_integer_prefix(dot + 1, &suffix) || suffix < 0) return false;
        if (suffix >= 5) magnitude++;
    }
    *result = negative ? -magnitude : magnitude;
    return true;
}

static bool encode_native_temperature(long value, uint16_t* output)
{
    unsigned long magnitude;

    if (!output || value == LONG_MIN) return false;
    if (value < 0) {
        magnitude = (unsigned long)(-value);
        *output = magnitude == 0
                      ? 0
                      : (uint16_t)(0x100u | ((0x100u - magnitude) & 0xffu));
    } else {
        *output = (uint16_t)((unsigned long)value & 0xffu);
    }
    return true;
}

static bool is_not_available(const char* text)
{
    return text && (text[0] == '-' ||
                    strncmp(text, "N/A", 3) == 0);
}

static bool parse_temperature(const char* text, uint16_t* output)
{
    long value;

    if (!text || !output) return false;
    if (text[0] == 'N') {
        *output = 0xff;
        return true;
    }
    return parse_rounded_signed_value(text, &value) &&
           encode_native_temperature(value, output);
}

static bool read_derived_input(const RecordSet* set, const char* key,
                               size_t* next_record, float* output)
{
    char text[WEATHER_RESPONSE_VALUE_SIZE];
    char* end;

    if (!next_record || !output) return false;
    while (*next_record < set->count &&
           strcmp(set->records[*next_record].key, key) != 0) {
        (*next_record)++;
    }
    if (*next_record == set->count ||
        !copy_value(&set->records[(*next_record)++], text, sizeof(text))) {
        return false;
    }
    if (strcmp(text, "N/A") == 0) {
        *output = 255.0f;
        return true;
    }
    errno = 0;
    *output = strtof(text, &end);
    return end != text && *end == '\0' && errno != ERANGE && isfinite(*output);
}

static bool derive_current_temperature(const WeatherLegacyResponse* weather,
                                       const WeatherLegacyDay* day,
                                       uint16_t* output)
{
    RecordSet current = {weather->current, weather->current_record_count};
    RecordSet daily = {day->records, day->record_count};
    const RecordSet* sets[] = {&current, &current, &current, &daily, &daily};
    const char* keys[] = {"TEMP", "WS", "HUM", "TEMPH", "TEMPL"};
    float values[5];
    float result;
    long rounded;
    long double humidity, wind, value;
    size_t next_record = 0;

    for (size_t i = 0; i < 5; i++) {
        if (i == 3) next_record = 0;
        if (!read_derived_input(sets[i], keys[i], &next_record, &values[i])) {
            return false;
        }
        if (values[i] >= 255.0f) {
            return encode_native_temperature(255, output);
        }
    }

    humidity = ((long double)values[2] - 50.0L) / 15.0L;
    wind = ((long double)values[1] * 1000.0L / 3600.0L - 2.5L) / 3.0L;
    value = values[0];
    if (value > (long double)(double)33.6) {
        long double range = (long double)values[3] - values[4];
        if (range == 0.0L) return false;
        value += 9.0L / range + humidity;
    } else if (value < (long double)(double)12.1) {
        value -= humidity;
    } else {
        value += humidity;
    }
    result = (float)(value - wind);
    if (!isfinite(result) || result < -2147483648.0 ||
        result >= 2147483648.0) {
        return false;
    }
    rounded = (long)result;
    if (result - (float)rounded >= 0.5f) rounded++;
    return encode_native_temperature(rounded, output);
}

static bool parse_integer_bits(const char* text, unsigned bits,
                               bool unavailable_sentinel, uint16_t* output)
{
    long value;

    if (!text || !output || bits == 0 || bits > 16) return false;
    if (unavailable_sentinel && is_not_available(text)) {
        *output = 0xff;
        return true;
    }
    if (!parse_integer_value(text, &value)) return false;
    *output = (uint16_t)value;
    if (bits < 16) *output &= (uint16_t)((1u << bits) - 1u);
    return true;
}

static bool parse_icon(const char* text, uint16_t* output)
{
    char name[WEATHER_RESPONSE_VALUE_SIZE];
    size_t length;

    if (!text || !output) return false;
    length = strlen(text);
    if (length == 0 || length >= sizeof(name)) return false;
    for (size_t i = 0; i < length; i++) {
        name[i] = (char)toupper((unsigned char)text[i]);
    }
    name[length] = '\0';
    *output = (uint16_t)weather_icon_index(name);
    return true;
}

static bool parse_compass(const char* text, uint16_t* output)
{
    static const char* const aliases_a[8] = {
        "N", "WN", "W", "SW", "S", "OS", "O", "NO"
    };
    static const char* const aliases_b[8] = {
        "N", "NW", "W", "WS", "S", "SO", "O", "ON"
    };

    char alias[WEATHER_RESPONSE_VALUE_SIZE];
    size_t length;

    if (!text || !output) return false;
    length = strlen(text);
    if (length >= sizeof(alias)) return false;
    for (size_t i = 0; i < length; i++) {
        alias[i] = (char)toupper((unsigned char)text[i]);
    }
    alias[length] = '\0';
    for (uint16_t i = 0; i < 8; i++) {
        if (strcmp(alias, aliases_a[i]) == 0 ||
            strcmp(alias, aliases_b[i]) == 0) {
            *output = i;
            return true;
        }
    }
    *output = 8;
    return true;
}

static bool parse_split_time(const char* text, uint16_t* hour,
                             uint16_t* minute)
{
    long parsed_hour;
    long parsed_minute;
    const char* separator;
    char hour_text[8];

    if (!text || !hour || !minute) return false;
    separator = strchr(text, ':');
    if (!separator) return false;
    size_t hour_length = (size_t)(separator - text);
    if (hour_length == 0 || hour_length >= sizeof(hour_text)) return false;
    memcpy(hour_text, text, hour_length);
    hour_text[hour_length] = '\0';
    if (!parse_integer_prefix(hour_text, &parsed_hour) ||
        !parse_integer_prefix(separator + 1, &parsed_minute)) {
        return false;
    }
    if (parsed_hour < 0 || parsed_hour > 23 ||
        parsed_minute < 0 || parsed_minute > 59) {
        return false;
    }
    *hour = (uint16_t)parsed_hour;
    *minute = (uint16_t)parsed_minute;
    return true;
}

static bool parse_afternoon_beaufort(const char* text, uint16_t* output)
{
    long integer_part;
    long fractional_part = 0;
    char integer[WEATHER_RESPONSE_VALUE_SIZE];
    const char* dot;
    size_t length;

    if (!text || !output) return false;
    length = strlen(text);
    if (length >= sizeof(integer)) return false;
    memcpy(integer, text, length + 1);
    dot = strchr(integer, '.');
    if (dot) {
        integer[(size_t)(dot - integer)] = '\0';
        if (dot[1] && !parse_integer_value(dot + 1, &fractional_part)) {
            return false;
        }
    }
    if (!parse_integer_value(integer, &integer_part) ||
        integer_part > (LONG_MAX - fractional_part) / 10 ||
        integer_part < (LONG_MIN + fractional_part) / 10) {
        return false;
    }
    long combined = integer_part * 10 + fractional_part;
    if (combined > 254) combined = 254;
    *output = (uint16_t)combined & 0xff;
    return true;
}

static bool parse_signed_humidity(const char* text, uint16_t* output)
{
    long value;

    if (!text || !output || !parse_rounded_signed_value(text, &value)) {
        return false;
    }
    if (value == LONG_MIN) return false;
    if (value < 0) {
        long magnitude = -value;
        *output = (uint16_t)(0x80 | (magnitude & 0x7f));
    } else {
        if (value > 127) value = 127;
        *output = (uint16_t)value;
    }
    return true;
}

static bool get_required(const RecordSet* set, const char* key,
                         char value[WEATHER_RESPONSE_VALUE_SIZE])
{
    return read_record(set, key, value);
}

// Selector 6 reads Day 1 records but replaces slot 1 with the derived current
// value. Nonnumeric direction tokens in the raw WW fields convert to zero.
static WeatherSenderResult build_day1(
    const WeatherLegacyResponse* weather, const WeatherLegacyDay* day,
    const uint8_t* readback,
    size_t readback_count, uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    RecordSet set = {day->records, day->record_count};
    char value[WEATHER_RESPONSE_VALUE_SIZE];
    WeatherFrameNativeSelector6Fields fields;

#define REQUIRE_RECORD(key) \
    do { if (!get_required(&set, key, value)) return WEATHER_SENDER_MISSING_RECORD; } while (0)
#define PARSE_RECORD(key, parser, destination) \
    do { REQUIRE_RECORD(key); if (!(parser)(value, &(destination))) return WEATHER_SENDER_INVALID_VALUE; } while (0)

    PARSE_RECORD("TEMPH", parse_temperature, fields.temperature_high);
    REQUIRE_RECORD("TEMPL");
    if (!derive_current_temperature(weather, day, &fields.temperature_low)) {
        return WEATHER_SENDER_INVALID_VALUE;
    }
    PARSE_RECORD("MORING_ICON", parse_icon, fields.morning_icon_index);
    REQUIRE_RECORD("WS");
    if (!parse_integer_bits(value, 11, false, &fields.wind_speed)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("WBFT");
    if (!parse_integer_bits(value, 2, true, &fields.beaufort)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("WW");
    if (!parse_integer_bits(value, 8, false, &fields.weather_word)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("HUM");
    PARSE_RECORD("AFTERNOON_N", parse_compass, fields.afternoon_direction_index);
    REQUIRE_RECORD("AFWS");
    if (!parse_integer_bits(value, 7, true, &fields.afternoon_wind_speed)) return WEATHER_SENDER_INVALID_VALUE;
    PARSE_RECORD("AFBFT", parse_afternoon_beaufort, fields.afternoon_beaufort);
    REQUIRE_RECORD("AFWW");
    if (!parse_integer_bits(value, 5, true, &fields.afternoon_weather_word)) return WEATHER_SENDER_INVALID_VALUE;
    PARSE_RECORD("AFHUM", parse_signed_humidity, fields.afternoon_humidity);

#undef REQUIRE_RECORD
#undef PARSE_RECORD

    if (!weather_frame_build_native_selector6(
            readback, readback_count, &fields, frame)) {
        return WEATHER_SENDER_INVALID_VALUE;
    }
    return WEATHER_SENDER_OK;
}

static WeatherSenderResult build_later_first(
    const WeatherLegacyDay* day, const uint8_t* readback,
    size_t readback_count, uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    RecordSet set = {day->records, day->record_count};
    char value[WEATHER_RESPONSE_VALUE_SIZE];
    WeatherFrameNativeLaterDayFirstFields fields;

#define REQUIRE_RECORD(key) \
    do { if (!get_required(&set, key, value)) return WEATHER_SENDER_MISSING_RECORD; } while (0)
#define PARSE_RECORD(key, parser, destination) \
    do { REQUIRE_RECORD(key); if (!(parser)(value, &(destination))) return WEATHER_SENDER_INVALID_VALUE; } while (0)

    PARSE_RECORD("TEMPH", parse_temperature, fields.temperature_high);
    PARSE_RECORD("TEMPL", parse_temperature, fields.temperature_low);
    PARSE_RECORD("MORING_ICON", parse_icon, fields.morning_icon_index);
    REQUIRE_RECORD("WS");
    if (!parse_integer_bits(value, 8, false, &fields.wind_speed)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("WBFT");
    PARSE_RECORD("WW", parse_compass, fields.weather_direction_index);
    REQUIRE_RECORD("HUM");
    if (!parse_integer_bits(value, 7, true, &fields.humidity)) return WEATHER_SENDER_INVALID_VALUE;
    PARSE_RECORD("AFTERNOON_N", parse_icon, fields.afternoon_icon_index);
    REQUIRE_RECORD("AFWS");
    if (!parse_integer_bits(value, 8, true, &fields.afternoon_wind_speed)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("AFBFT");
    PARSE_RECORD("AFWW", parse_compass, fields.afternoon_direction_index);
    REQUIRE_RECORD("AFHUM");
    if (!parse_integer_bits(value, 7, true, &fields.afternoon_humidity)) return WEATHER_SENDER_INVALID_VALUE;
    PARSE_RECORD("NIGHT_ICON", parse_icon, fields.night_icon_index);

#undef REQUIRE_RECORD
#undef PARSE_RECORD

    if (!weather_frame_build_native_later_day_first(
            readback, readback_count, &fields, frame)) {
        return WEATHER_SENDER_INVALID_VALUE;
    }
    return WEATHER_SENDER_OK;
}

static WeatherSenderResult build_later_second(
    const WeatherLegacyDay* day, const uint8_t* readback,
    size_t readback_count, uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    RecordSet set = {day->records, day->record_count};
    char value[WEATHER_RESPONSE_VALUE_SIZE];
    WeatherFrameNativeLaterDaySecondFields fields;
    long rainfall;

#define REQUIRE_RECORD(key) \
    do { if (!get_required(&set, key, value)) return WEATHER_SENDER_MISSING_RECORD; } while (0)
#define PARSE_RECORD(key, parser, destination) \
    do { REQUIRE_RECORD(key); if (!(parser)(value, &(destination))) return WEATHER_SENDER_INVALID_VALUE; } while (0)

    REQUIRE_RECORD("NWS");
    if (!parse_integer_bits(value, 8, false, &fields.night_wind_speed)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("NBFT");
    PARSE_RECORD("NWW", parse_compass, fields.night_direction_index);
    REQUIRE_RECORD("NHUM");
    if (!parse_integer_bits(value, 7, true, &fields.night_humidity)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("SUNR");
    if (!parse_split_time(value, &fields.sunrise_hour, &fields.sunrise_minute)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("SUNS");
    if (!parse_split_time(value, &fields.sunset_hour, &fields.sunset_minute)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("PPCP");
    if (!parse_integer_bits(value, 7, false, &fields.precipitation_percent)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("RAINMS");
    if (!parse_rounded_signed_value(value, &rainfall)) return WEATHER_SENDER_INVALID_VALUE;
    fields.rainfall = (uint16_t)rainfall;
    REQUIRE_RECORD("SUNTIME");
    if (!parse_integer_bits(value, 5, false, &fields.sunshine_hours)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("UVI");
    if (!parse_integer_bits(value, 5, false, &fields.uv_index)) return WEATHER_SENDER_INVALID_VALUE;
    REQUIRE_RECORD("DEWP");
    if (!parse_integer_bits(value, 8, true, &fields.dew_point)) return WEATHER_SENDER_INVALID_VALUE;

#undef REQUIRE_RECORD
#undef PARSE_RECORD

    if (!weather_frame_build_native_later_day_second(
            readback, readback_count, &fields, frame)) {
        return WEATHER_SENDER_INVALID_VALUE;
    }
    return WEATHER_SENDER_OK;
}

// Selectors 0-3 use initialized bytes of the upper-cased city header table.
// Selector 4 also reads a stack byte outside that table and is not supported.
static WeatherSenderResult build_header(
    unsigned selector, const WeatherLegacyResponse* weather,
    const uint8_t* readback_cells, size_t readback_cell_count,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    uint8_t table[40] = {0};
    uint8_t values[WEATHER_FRAME_NATIVE_HEADER_VALUES] = {0};
    const char* text = NULL;
    size_t length = 0;

    for (size_t i = 0; i < weather->current_record_count; i++) {
        if (strcmp(weather->current[i].key, "CITY_AND_WMO") == 0) {
            text = value_text(&weather->current[i]);
            break;
        }
    }
    if (!text) return WEATHER_SENDER_MISSING_RECORD;
    text = strchr(text, ';');
    if (!text) return WEATHER_SENDER_INVALID_VALUE;
    for (text++; text[length] && text[length] != ';' &&
                 text[length] != '\r' && length < 39; length++) {
        char c = text[length];
        table[length] = (uint8_t)((c >= 'a' && c <= 'z') ? c - 0x20 : c);
    }
    if (selector == 0) {
        values[0] = (uint8_t)length;
        memcpy(values + 1, table, 9);
    } else {
        memcpy(values, table + 7 + 8 * (selector - 1), 10);
    }
    return weather_frame_build_native_header(readback_cells,
                                             readback_cell_count, values, frame)
               ? WEATHER_SENDER_OK
               : WEATHER_SENDER_INVALID_VALUE;
}

WeatherSenderResult weather_sender_build_weather_segment(
    unsigned selector, const WeatherLegacyResponse* weather,
    const uint8_t* readback_cells, size_t readback_cell_count,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    unsigned day_number;
    const WeatherLegacyDay* day = NULL;
    WeatherSenderResult result;

    if (!weather || !readback_cells || !frame) {
        return WEATHER_SENDER_INVALID_VALUE;
    }
    if (selector <= 3) {
        return build_header(selector, weather, readback_cells,
                            readback_cell_count, frame);
    }
    if (selector == 6) {
        for (size_t i = 0; i < weather->day_count; i++) {
            if (weather->days[i].day_number == 1) {
                return build_day1(weather, &weather->days[i], readback_cells,
                                  readback_cell_count, frame);
            }
        }
        return WEATHER_SENDER_MISSING_DAY;
    }
    if (selector >= 8 && selector <= 17) {
        day_number = 2 + (selector - 8) / 2;
    } else {
        return WEATHER_SENDER_UNSUPPORTED_SELECTOR;
    }
    for (size_t i = 0; i < weather->day_count; i++) {
        if (weather->days[i].day_number == (int)day_number) {
            day = &weather->days[i];
            break;
        }
    }
    if (!day) return WEATHER_SENDER_MISSING_DAY;

    if ((selector & 1u) == 0) {
        result = build_later_first(day, readback_cells,
                                   readback_cell_count, frame);
    } else {
        result = build_later_second(day, readback_cells,
                                    readback_cell_count, frame);
    }
    return result;
}

bool weather_sender_decode_request(const uint8_t* readback_cells,
                                   size_t readback_cell_count,
                                   WeatherSenderRequest* request)
{
    if (!readback_cells || !request ||
        readback_cell_count <= WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS - 2) {
        return false;
    }
    if ((readback_cells[1] & 0xf0) != 0x10 ||
        (readback_cells[7] & 0xf0) != 0x10 ||
        (readback_cells[8] & 0xf0) != 0x10) {
        return false;
    }

    request->request_type = readback_cells[1] & 0x0f;
    request->selector = (uint8_t)(((readback_cells[7] & 0x03) << 4) |
                                  (readback_cells[8] & 0x0f));
    request->device_id[0] = '\0';
    request->city_slot = 0;
    if (request->request_type == 6) {
        static const char digits[] = "0123456789ABCDEF";
        for (size_t i = 0; i < 5; i++) {
            uint8_t digit = readback_cells[i + 2] & 0x0f;
            if ((readback_cells[i + 2] & 0xf0) != 0x10) {
                return false;
            }
            request->device_id[i] = digits[digit];
        }
        request->device_id[5] = '\0';
        request->city_slot = (uint8_t)((readback_cells[7] & 0x0c) >> 2);
    }
    return true;
}
