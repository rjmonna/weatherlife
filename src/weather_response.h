// Parse the archived Weather-Life named-record weather response.

#ifndef WEATHER_LIFE_WEATHER_RESPONSE_H
#define WEATHER_LIFE_WEATHER_RESPONSE_H

#include "usb_device.h"
#include <stddef.h>
#include <stdbool.h>

// Parse verified current fields from a legacy city response.
// Weather code and forecast icon mappings remain intentionally unassigned.
bool weather_parse_legacy_current(const char* response, size_t response_length,
                                  WeatherData* data);

#endif  // WEATHER_LIFE_WEATHER_RESPONSE_H
