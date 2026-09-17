// Parse the archived Weather-Life named-record weather response.

#ifndef WEATHER_LIFE_WEATHER_RESPONSE_H
#define WEATHER_LIFE_WEATHER_RESPONSE_H

#include "usb_device.h"
#include <stddef.h>
#include <stdbool.h>

// Lossless representation of the legacy named-record response. Values remain
// text because their device units and wire encodings are not all verified.
#define WEATHER_RESPONSE_KEY_SIZE 16
#define WEATHER_RESPONSE_VALUE_SIZE 128
#define WEATHER_RESPONSE_MAX_CURRENT_RECORDS 32
#define WEATHER_RESPONSE_MAX_DAYS 8
#define WEATHER_RESPONSE_MAX_DAILY_RECORDS 16

typedef struct {
    char key[WEATHER_RESPONSE_KEY_SIZE];
    char value[WEATHER_RESPONSE_VALUE_SIZE];
    int bit_length;
} WeatherLegacyRecord;

typedef struct {
    int day_number;
    char date[9];
    size_t record_count;
    WeatherLegacyRecord records[WEATHER_RESPONSE_MAX_DAILY_RECORDS];
} WeatherLegacyDay;

typedef struct {
    size_t current_record_count;
    WeatherLegacyRecord current[WEATHER_RESPONSE_MAX_CURRENT_RECORDS];
    size_t day_count;
    WeatherLegacyDay days[WEATHER_RESPONSE_MAX_DAYS];
} WeatherLegacyResponse;

// Preserve every supported named record without assigning unverified
// semantics to it. Returns false for malformed input or capacity overflow.
bool weather_parse_legacy_records(const char* response, size_t response_length,
                                  WeatherLegacyResponse* parsed);

// Parse verified current fields from a legacy city response.
// Weather code and forecast icon mappings remain intentionally unassigned.
bool weather_parse_legacy_current(const char* response, size_t response_length,
                                  WeatherData* data);

#endif  // WEATHER_LIFE_WEATHER_RESPONSE_H
