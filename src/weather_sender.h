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
    char device_id[6];
    uint8_t city_slot;
} WeatherSenderRequest;

// Decode the outer request type, hexadecimal device ID, city slot and
// inner selector from the native readback. The ID is not a weather city code.
bool weather_sender_decode_request(const uint8_t* readback_cells,
                                   size_t readback_cell_count,
                                   WeatherSenderRequest* request);

// Build a selector-specific response from the parsed native named records.
// Selectors 0..3 encode city-header responses; selector 6 combines Day 1
// records with a derived current value, and selectors 8..17 encode the paired
// forecast fields for Days 2..6. Selectors 4 and 7 remain unsupported because
// the original reads values or widths from unrelated stack locations.
WeatherSenderResult weather_sender_build_weather_segment(
    unsigned selector, const WeatherLegacyResponse* weather,
    const uint8_t* readback_cells, size_t readback_cell_count,
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS]);

#endif  // WEATHER_LIFE_WEATHER_SENDER_H
