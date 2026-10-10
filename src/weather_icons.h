// Icon-name to wire index, recovered from usbwr.exe FUN_00410950.
#ifndef WEATHER_LIFE_WEATHER_ICONS_H
#define WEATHER_LIFE_WEATHER_ICONS_H

#define WEATHER_ICON_COUNT 79
#define WEATHER_ICON_UNKNOWN 79

// Case-sensitive (the original parser uppercases the service line first).
// Returns the position in either of the two 79-entry tables, or 79.
int weather_icon_index(const char* name);

#endif  // WEATHER_LIFE_WEATHER_ICONS_H
