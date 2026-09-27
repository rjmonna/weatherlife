// Parse verified fields from the legacy Weather-Life city response.

#include "weather_response.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char* skip_space(char* text)
{
    while (*text && isspace((unsigned char)*text)) text++;
    return text;
}

static bool parse_day_header(const char* line, int* day_number, char date[9])
{
    const char* cursor = line;
    char* date_end;
    long number;

    if (cursor[0] != 'D' || cursor[1] != 'A' || cursor[2] != 'Y') return false;
    errno = 0;
    number = strtol(cursor + 3, &date_end, 10);
    if (errno || number < 1 || number > 6 || date_end == cursor + 3 ||
        !isspace((unsigned char)*date_end)) return false;
    *day_number = (int)number;
    cursor = skip_space(date_end);
    for (int i = 0; i < 8; i++) {
        if (cursor[i] < '0' || cursor[i] > '9') return false;
        date[i] = cursor[i];
    }
    date[8] = '\0';
    cursor += 8;
    cursor = skip_space((char*)cursor);
    return *cursor == ';' && *skip_space((char*)(cursor + 1)) == '\0';
}

static bool append_record(const char* line, WeatherLegacyRecord* record)
{
    const char* cursor = line;
    const char* value_open;
    const char* value_close;
    char* key_end;
    char* bit_end;
    long bit_length = 0;
    size_t key_length;
    size_t value_length;

    cursor = skip_space((char*)cursor);
    key_end = (char*)cursor;
    while (*key_end && !isspace((unsigned char)*key_end)) key_end++;
    key_length = (size_t)(key_end - cursor);
    if (key_length == 0 || key_length >= sizeof(record->key)) return false;
    memcpy(record->key, cursor, key_length);
    record->key[key_length] = '\0';

    cursor = skip_space(key_end);
    if (*cursor == '<' && strcmp(record->key, "CITY_AND_WMO") != 0) {
        const char* bit_open = cursor;
        const char* bit_close = strchr(bit_open + 1, '>');
        if (!bit_close) return false;
        errno = 0;
        bit_length = strtol(bit_open + 1, &bit_end, 10);
        if (errno || bit_end != bit_close || bit_length < 0 || bit_length > 255) {
            return false;
        }
        cursor = skip_space((char*)(bit_close + 1));
    }

    value_open = strchr(cursor, '<');
    if (!value_open) return false;
    value_close = strchr(value_open + 1, '>');
    if (!value_close) return false;
    value_length = (size_t)(value_close - value_open + 1);
    if (value_length >= sizeof(record->value)) return false;
    memcpy(record->value, value_open, value_length);
    record->value[value_length] = '\0';
    record->bit_length = (int)bit_length;
    return true;
}

bool weather_parse_legacy_records(const char* response, size_t response_length,
                                  WeatherLegacyResponse* parsed)
{
    const char* cursor;
    const char* end;
    char line[512];
    int active_day = -1;

    if (!response || !parsed) return false;
    memset(parsed, 0, sizeof(*parsed));
    cursor = response;
    end = response + response_length;
    while (cursor < end) {
        const char* line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        const char* limit = line_end ? line_end : end;
        size_t line_length = (size_t)(limit - cursor);
        char* trimmed;

        if (line_length > 0 && cursor[line_length - 1] == '\r') line_length--;
        if (line_length >= sizeof(line)) return false;
        memcpy(line, cursor, line_length);
        line[line_length] = '\0';
        trimmed = skip_space(line);
        if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#' ||
            strcmp(trimmed, "DES           BIT LENGTH    DATA") == 0) {
            cursor = line_end ? line_end + 1 : end;
            continue;
        }

        if (strncmp(trimmed, "DAY", 3) == 0) {
            WeatherLegacyDay* day;
            if (parsed->day_count >= WEATHER_RESPONSE_MAX_DAYS ||
                !parse_day_header(trimmed, &active_day, line + 1)) return false;
            day = &parsed->days[parsed->day_count++];
            day->day_number = active_day;
            memcpy(day->date, line + 1, sizeof(day->date));
            cursor = line_end ? line_end + 1 : end;
            continue;
        }

        if (active_day < 0) {
            if (parsed->current_record_count >= WEATHER_RESPONSE_MAX_CURRENT_RECORDS ||
                !append_record(trimmed, &parsed->current[parsed->current_record_count++])) {
                return false;
            }
        } else {
            WeatherLegacyDay* day = &parsed->days[parsed->day_count - 1];
            if (day->record_count >= WEATHER_RESPONSE_MAX_DAILY_RECORDS ||
                !append_record(trimmed, &day->records[day->record_count++])) return false;
        }
        cursor = line_end ? line_end + 1 : end;
    }
    return parsed->current_record_count > 0;
}

static const char* find_record(const char* response, size_t response_length,
                               const char* key)
{
    size_t key_length = strlen(key);
    const char* cursor = response;
    const char* end = response + response_length;

    while (cursor < end) {
        const char* line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        const char* limit = line_end ? line_end : end;
        if ((size_t)(limit - cursor) > key_length &&
            memcmp(cursor, key, key_length) == 0 &&
            (cursor[key_length] == ' ' || cursor[key_length] == '\t')) {
            return cursor;
        }
        cursor = line_end ? line_end + 1 : end;
    }
    return NULL;
}

static bool parse_numeric_value(const char* record, double* value)
{
    const char* first_open = strchr(record, '<');
    const char* first_close;
    const char* value_open;
    char* value_end;

    if (!first_open) return false;
    first_close = strchr(first_open + 1, '>');
    if (!first_close) return false;
    value_open = strchr(first_close + 1, '<');
    if (!value_open) return false;

    *value = strtod(value_open + 1, &value_end);
    return value_end != value_open + 1;
}

bool weather_parse_legacy_current(const char* response, size_t response_length,
                                  WeatherData* data)
{
    const char* record;
    double value;
    bool have_temperature = false;
    bool have_humidity = false;

    if (!response || !data) return false;
    memset(data, 0, sizeof(*data));

    record = find_record(response, response_length, "TEMP");
    if (record && parse_numeric_value(record, &value)) {
        data->temperature = (float)value;
        have_temperature = true;
    }

    record = find_record(response, response_length, "HUM");
    if (record && parse_numeric_value(record, &value)) {
        data->humidity = (int)value;
        have_humidity = true;
    }

    record = find_record(response, response_length, "WS");
    if (record && parse_numeric_value(record, &value)) {
        data->wind_speed = (float)(value / 3.6);
    }

    record = find_record(response, response_length, "PRE");
    if (record && parse_numeric_value(record, &value)) {
        data->pressure = (int)value;
    }

    record = find_record(response, response_length, "RAINMS");
    if (record && parse_numeric_value(record, &value)) {
        data->precipitation = (float)value;
    }

    // WEA/icon and forecast fields require a separately verified mapping.
    data->weather_code = 0;
    return have_temperature && have_humidity;
}
