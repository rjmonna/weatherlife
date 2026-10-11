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

// Build a selector-specific weather response from the parsed native named
// records. The result is a 32-cell segment for selectors 6 and 8..17.
WeatherSenderResult weather_sender_build_weather_segment(
    unsigned selector, const WeatherLegacyResponse* weather,
    const uint8_t* readback_cells, size_t readback_cell_count,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

#endif  // WEATHER_LIFE_WEATHER_SENDER_H
