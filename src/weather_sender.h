// Executable-derived named-record conversions and weather response builders.

#ifndef WEATHER_LIFE_WEATHER_SENDER_H
#define WEATHER_LIFE_WEATHER_SENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_response.h"
#include "weather_frame.h"

typedef enum {
    WEATHER_SENDER_OK,
    WEATHER_SENDER_UNSUPPORTED_SELECTOR,
    WEATHER_SENDER_MISSING_DAY,
    WEATHER_SENDER_MISSING_RECORD,
    WEATHER_SENDER_INVALID_VALUE
} WeatherSenderResult;

typedef struct {
    uint8_t request_type;
    uint8_t selector;
    char city_code[6];
} WeatherSenderRequest;

// Decode the outer request type from cell 1 and the inner selector from
// cells 7-8 of the native readback.
bool weather_sender_decode_request(const uint8_t* readback_cells,
                                   size_t readback_cell_count,
                                   WeatherSenderRequest* request);
bool weather_sender_request_matches_city(
    const WeatherSenderRequest* request,
    const WeatherLegacyResponse* weather);

// Build a selector-specific response from the parsed native named records.
// Selectors 0..4 encode city-header responses; selector 6 encodes Day 1 and
// selectors 8..17 encode the paired forecast fields for Days 2..6.
WeatherSenderResult weather_sender_build_weather_segment(
    unsigned selector, const WeatherLegacyResponse* weather,
    const uint8_t* readback_cells, size_t readback_cell_count,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

#endif  // WEATHER_LIFE_WEATHER_SENDER_H
