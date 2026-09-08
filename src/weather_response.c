// Parse verified fields from the legacy Weather-Life city response.

#include "weather_response.h"
#include <stdlib.h>
#include <string.h>

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

    // WEA/icon and forecast fields require a separately verified mapping.
    data->weather_code = 0;
    return have_temperature && have_humidity;
}
