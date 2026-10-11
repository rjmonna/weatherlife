#include "weather_session.h"

#include <string.h>

#include "bit_packer.h"

bool weather_session_build_init_frame(uint8_t packed_control_field,
                                      bool session_flag,
                                      uint8_t frame[WEATHER_FRAME_SIZE])
{
    BitPacker packer;

    if (!frame) return false;

    memset(frame, 0x10, WEATHER_FRAME_SIZE);
    frame[1] |= 0x0a;
    bit_packer_init_at(&packer, frame, WEATHER_FRAME_SIZE, 2, 4);
    bit_packer_append(&packer, packed_control_field, 8);
    bit_packer_append(&packer, session_flag ? 1 : 0, 1);
    return weather_frame_finalize_native_segment(frame, WEATHER_FRAME_SIZE, 64);
}

bool weather_session_build_control_frame(
    const uint8_t* response_cells, size_t response_cell_count,
    const WeatherSessionControlFields* fields,
    uint8_t frame[WEATHER_SESSION_CONTROL_CELLS])
{
    BitPacker packer;

    if (!response_cells || !fields || !frame ||
        response_cell_count < WEATHER_SESSION_CONTROL_PREFIX_CELLS ||
        fields->mode_flags > 7 || fields->field_a > 15 ||
        fields->field_b > 7 || fields->field_c > 7 || fields->field_d > 3) {
        return false;
    }

    for (size_t i = 0; i < WEATHER_SESSION_CONTROL_PREFIX_CELLS; i++) {
        frame[i] = response_cells[i] & 0x0f;
    }
    frame[1] |= 0x04;
    memset(frame + WEATHER_SESSION_CONTROL_PREFIX_CELLS, 0x10,
           WEATHER_SESSION_CONTROL_CELLS -
               WEATHER_SESSION_CONTROL_PREFIX_CELLS);

    bit_packer_init_at(&packer, frame, WEATHER_SESSION_CONTROL_CELLS, 7, 7);
    bit_packer_append(&packer, fields->mode_flags, 3);
    bit_packer_append(&packer, fields->field_a, 4);
    bit_packer_append(&packer, fields->field_b, 3);
    bit_packer_append(&packer, fields->field_c, 3);
    bit_packer_append(&packer, fields->field_d, 2);
    bit_packer_append(&packer, fields->session_flag ? 1 : 0, 1);

    return weather_frame_finalize_native_segment(
        frame, WEATHER_SESSION_CONTROL_CELLS, 64);
}

void weather_session_state_init(WeatherSessionState* state)
{
    if (!state) return;
    state->failed_reads = 0;
    state->reinitialize_after_recovery = false;
}

WeatherSessionAction weather_session_on_read(WeatherSessionState* state,
                                             bool read_succeeded)
{
    if (!state) return WEATHER_SESSION_INVALID_STATE;

    if (read_succeeded) {
        state->failed_reads = 0;
        if (state->reinitialize_after_recovery) {
            state->reinitialize_after_recovery = false;
            return WEATHER_SESSION_SEND_INIT_FRAME;
        }
        return WEATHER_SESSION_PROCESS_RESPONSE;
    }

    state->failed_reads++;
    if (state->failed_reads < WEATHER_SESSION_READ_ATTEMPTS) {
        return WEATHER_SESSION_RETRY_READ;
    }

    state->failed_reads = 0;
    state->reinitialize_after_recovery = true;
    return WEATHER_SESSION_RECOVER_DEVICE;
}
