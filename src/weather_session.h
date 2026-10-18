// Recovered host-side session lifecycle primitives from usbwr.exe.

#ifndef WEATHER_LIFE_WEATHER_SESSION_H
#define WEATHER_LIFE_WEATHER_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_frame.h"

#define WEATHER_SESSION_POLL_INTERVAL_MS 130
#define WEATHER_SESSION_READ_ATTEMPTS 4
#define WEATHER_SESSION_RECOVERY_DELAY_MS 3000
#define WEATHER_SESSION_CONTROL_PREFIX_CELLS 8
#define WEATHER_SESSION_CONTROL_CELLS 16

typedef struct {
    uint8_t mode_flags;       // 3 bits
    uint8_t field_a;          // 4 bits; source meaning unresolved
    uint8_t field_b;          // 3 bits; source meaning unresolved
    uint8_t field_c;          // 3 bits; source meaning unresolved
    uint8_t field_d;          // 2 bits; source meaning unresolved
    bool session_flag;        // 1 bit
} WeatherSessionControlFields;

typedef struct {
    unsigned failed_reads;
    bool reinitialize_after_recovery;
} WeatherSessionState;

typedef enum {
    WEATHER_SESSION_PROCESS_RESPONSE,
    WEATHER_SESSION_RETRY_READ,
    WEATHER_SESSION_RECOVER_DEVICE,
    WEATHER_SESSION_SEND_INIT_FRAME,
    WEATHER_SESSION_INVALID_STATE
} WeatherSessionAction;

// Build usbwr.exe's 64-bit startup/recovery initialization segment. The
// packed_control_field is the 8-bit result of its reg.dat nibble extraction.
bool weather_session_build_init_frame(uint8_t packed_control_field,
                                      bool session_flag,
                                      uint8_t frame[WEATHER_FRAME_SIZE]);

// Build the 64-bit periodic control segment from a response prefix and opaque
// values extracted by the original host from its local control records.
bool weather_session_build_control_frame(
    const uint8_t* response_cells, size_t response_cell_count,
    const WeatherSessionControlFields* fields,
    uint8_t frame[WEATHER_SESSION_CONTROL_CELLS]);

// Build the periodic 64-bit frame as decoded from usbwr.exe and traces:
// mode(3) + UPD month(4)/day(5)/hour(5)/minute(6) + flag(1) at cell 7 bit 7.
bool weather_session_build_timestamp_frame(
    const uint8_t* response_cells, size_t response_cell_count, uint8_t mode,
    uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, bool flag,
    uint8_t frame[WEATHER_SESSION_CONTROL_CELLS]);

void weather_session_state_init(WeatherSessionState* state);

// Consume one CAL_USB_READ outcome. The caller performs any returned action;
// after recovery, the next successful read requests an init-frame resend.
WeatherSessionAction weather_session_on_read(WeatherSessionState* state,
                                             bool read_succeeded);

#endif  // WEATHER_LIFE_WEATHER_SESSION_H
