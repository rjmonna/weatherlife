#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

// weather-life/src/main_discover.c
// Main executable to discover and test weather device

#include "usb_device.h"
#include "device_registration.h"
#include "weather_frame.h"
#include "weather_sender.h"
#include "weather_session.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #define SLEEP(ms) Sleep(ms)
#else
    #include <time.h>
    static void sleep_ms(unsigned int milliseconds)
    {
        struct timespec delay = {
            (time_t)(milliseconds / 1000),
            (long)(milliseconds % 1000) * 1000000L
        };
        while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
        }
    }
    #define SLEEP(ms) sleep_ms(ms)
#endif

static void print_read_response(const char* label, const uint8_t* response,
                                int response_length)
{
    printf("%s (%d bytes):", label, response_length);
    for (int i = 0; i < response_length; i++) {
        printf(" %02x", response[i]);
    }
    printf("\n");
}

static bool parse_payload_hex(const char* text, uint8_t payload[WEATHER_FRAME_SIZE])
{
    if (!text || strlen(text) != WEATHER_FRAME_SIZE * 2) return false;

    for (int i = 0; i < WEATHER_FRAME_SIZE; i++) {
        char byte_text[3] = {text[i * 2], text[i * 2 + 1], '\0'};
        char* end = NULL;
        unsigned long value = strtoul(byte_text, &end, 16);
        if (!end || *end != '\0') return false;
        payload[i] = (uint8_t)value;
    }
    return true;
}

static WeatherFrameNativeSelector6Fields selector6_fixture_test_fields(void)
{
    WeatherFrameNativeSelector6Fields fields = {
        .temperature_high = 6,
        .temperature_low = 0x1fc,
        .morning_icon_index = 79,
        .wind_speed = 9,
        .beaufort = 2,
        .weather_word = 0,
        .afternoon_direction_index = 8,
        .afternoon_wind_speed = 20,
        .afternoon_beaufort = 34,
        .afternoon_weather_word = 0,
        .afternoon_humidity = 82
    };
    return fields;
}

static void print_selector6_segment(const uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS])
{
    printf("Selector-6 segment (32 nibble cells):");
    for (size_t i = 0; i < WEATHER_FRAME_NATIVE_SEGMENT_CELLS; i++) {
        printf(" %02x", frame[i]);
    }
    printf("\n");
}

static bool write_native_segment(USBBackend* backend, USBDevice* device,
                                 const uint8_t* frame, size_t cell_count)
{
    const size_t chunk_size = 16;

    if (!backend || !device || !frame || cell_count == 0 ||
        cell_count % chunk_size != 0) {
        return false;
    }

    for (size_t offset = 0; offset < cell_count; offset += chunk_size) {
        if (!backend->write_data(device, frame + offset, (int)chunk_size)) {
            fprintf(stderr, "[-] Native payload chunk at cell %zu failed; segment may be partial\n",
                    offset);
            return false;
        }
        if (offset + chunk_size < cell_count) {
            SLEEP(2);
        }
    }
    return true;
}

static bool run_selector6_fixture_test_once(USBBackend* backend,
                                            USBDevice* device)
{
    uint8_t readback[WEATHER_FRAME_SIZE];
    uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
    int response_length = 0;
    WeatherFrameNativeSelector6Fields fields = selector6_fixture_test_fields();

    if (!backend->read_data(device, readback, sizeof(readback),
                            &response_length) || response_length < 10) {
        fprintf(stderr, "[-] Prefix read failed or was shorter than ten cells; no write sent\n");
        return false;
    }
    if (!weather_frame_build_native_selector6(readback, (size_t)response_length,
                                               &fields, frame)) {
        fprintf(stderr, "[-] Could not build selector-6 fixture test frame; no write sent\n");
        return false;
    }

    print_selector6_segment(frame);
    printf("Icon index 79 and direction index 8 exercise documented unknown-value fallbacks.\n");

    if (!write_native_segment(backend, device, frame,
                              WEATHER_FRAME_NATIVE_SEGMENT_CELLS)) {
        return false;
    }

    printf("Sent one 128-bit selector-6 segment as two 16-cell writes with 2 ms pacing.\n");
    for (int poll = 0; poll < 6; poll++) {
        uint8_t response[WEATHER_FRAME_SIZE];
        response_length = 0;
        SLEEP(100);
        if (!backend->read_data(device, response, sizeof(response),
                                &response_length)) {
            fprintf(stderr, "[-] Post-write read poll %d failed\n", poll + 1);
            return false;
        }
        char label[48];
        snprintf(label, sizeof(label), "Post-write read %d", poll + 1);
        print_read_response(label, response, response_length);
    }
    return true;
}

static const char* weather_sender_result_text(WeatherSenderResult result)
{
    switch (result) {
    case WEATHER_SENDER_OK:
        return "ok";
    case WEATHER_SENDER_UNSUPPORTED_SELECTOR:
        return "unsupported selector";
    case WEATHER_SENDER_MISSING_DAY:
        return "forecast day is absent";
    case WEATHER_SENDER_MISSING_RECORD:
        return "required named record is absent";
    case WEATHER_SENDER_INVALID_VALUE:
        return "record value or readback prefix is invalid";
    }
    return "invalid sender result";
}

static bool read_weather_file(const char* path, WeatherLegacyResponse* weather)
{
    FILE* file = NULL;
    long file_size;
    char* contents = NULL;
    bool parsed = false;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[-] Could not open weather response file: %s\n", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_size = ftell(file)) <= 0 || file_size > 1024 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[-] Weather response file is empty, too large, or unreadable\n");
        fclose(file);
        return false;
    }
    contents = (char*)malloc((size_t)file_size);
    if (!contents) {
        fprintf(stderr, "[-] Could not allocate weather response buffer\n");
        fclose(file);
        return false;
    }
    if (fread(contents, 1, (size_t)file_size, file) != (size_t)file_size) {
        fprintf(stderr, "[-] Could not read complete weather response file\n");
    } else if (!weather_parse_legacy_records(contents, (size_t)file_size, weather)) {
        fprintf(stderr, "[-] Weather file does not match the parsed legacy named-record format\n");
    } else {
        parsed = true;
    }
    free(contents);
    fclose(file);
    return parsed;
}

static bool parse_positive_seconds(const char* text, int* seconds)
{
    char* end;
    long value;

    if (!text || !seconds) return false;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        value <= 0 || value > 86400) {
        return false;
    }
    *seconds = (int)value;
    return true;
}

// Respond only to supported forecast requests. Initialization and the
// selector-1 city exchange remain gated because their complete source data
// and response contract are not yet decoded.
// Mirrors usbwr.exe startup: repeat the registration frame every 100 ms until
// the readback echoes the id, then send the 64-bit init frame whose 8-bit field
// is the last two hex digits of the reg.dat code ("c48eb" -> 0xeb) and whose
// session flag starts at 1.
static bool run_registration_and_init(USBBackend* backend, USBDevice* device,
                                      const char* reg_code)
{
    uint8_t field_hex[2];
    WeatherRegistrationId id;
    uint8_t reg[WEATHER_FRAME_SIZE];
    bool matched = false;

    if (!reg_code || strlen(reg_code) != 5) return false;
    for (int i = 0; i < 2; i++) {
        char c = reg_code[3 + i];
        if (c >= '0' && c <= '9') field_hex[i] = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') field_hex[i] = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') field_hex[i] = (uint8_t)(c - 'A' + 10);
        else return false;
    }
    srand((unsigned)time(NULL));
    weather_registration_generate_id(&id);
    weather_registration_build_frame(&id, reg);
    for (int attempt = 0; attempt < 30 && !matched; attempt++) {
        uint8_t resp[WEATHER_FRAME_SIZE];
        uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE];
        int length = 0;
        if (!backend->write_data(device, reg, sizeof(reg))) return false;
        SLEEP(100);
        if (backend->read_data(device, resp, sizeof(resp), &length) &&
            length >= WEATHER_REGISTRATION_FRAME_SIZE) {
            weather_registration_decode_response(resp, decoded);
            matched = weather_registration_response_matches(decoded, &id);
        }
    }
    printf("Registration acknowledgment: %s\n", matched ? "matched" : "not matched");
    if (!matched) return false;

    uint8_t init[WEATHER_FRAME_SIZE];
    if (!weather_session_build_init_frame((uint8_t)((field_hex[0] << 4) | field_hex[1]),
                                          true, init) ||
        !write_native_segment(backend, device, init, sizeof(init))) {
        return false;
    }
    printf("Init frame sent (field 0x%02x)\n", (field_hex[0] << 4) | field_hex[1]);
    return true;
}

static bool run_listen_and_answer(USBBackend* backend, USBDevice* device,
                                  const WeatherLegacyResponse* weather,
                                  int seconds, const char* reg_code)
{
    if (reg_code && !run_registration_and_init(backend, device, reg_code)) {
        fprintf(stderr, "[-] Registration/init failed\n");
        return false;
    }
    int last_length = 0;
    uint8_t last[WEATHER_FRAME_SIZE] = {0};
    bool have_last = false;
    WeatherSessionState session;

    weather_session_state_init(&session);
    for (int tick = 0; tick < seconds * 1000 / WEATHER_SESSION_POLL_INTERVAL_MS; tick++) {
        uint8_t response[WEATHER_FRAME_SIZE];
        int length = 0;
        bool read_ok = backend->read_data(device, response, sizeof(response),
                                          &length);
        WeatherSessionAction action = weather_session_on_read(&session, read_ok);
        if (!read_ok) {
            if (action == WEATHER_SESSION_RECOVER_DEVICE) {
                fprintf(stderr,
                        "[-] Read failed four times; stopping instead of reconnecting "
                        "without the unresolved startup initialization field\n");
                return false;
            }
            SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
            continue;
        }
        if (action != WEATHER_SESSION_PROCESS_RESPONSE ||
            length < WEATHER_FRAME_NATIVE_SELECTOR6_PREFIX_CELLS ||
            length > (int)sizeof(response)) {
            fprintf(stderr, "[-] Read returned an invalid response length (%d); no write sent\n",
                    length);
            return false;
        }
        unsigned selector = response[1] & 0x0f;
        bool changed = !have_last || length != last_length ||
                       memcmp(last, response, (size_t)length) != 0;
        if (changed) {
            printf("Device requested selector %u; readback:", selector);
            for (int i = 0; i < length; i++) printf(" %02x", response[i]);
            printf("\n");
            memcpy(last, response, (size_t)length);
            last_length = length;
            have_last = true;
        }
        bool idle = true;
        for (int i = 0; i < length; i++) {
            if (response[i] != 0x10) idle = false;
        }
        if (idle) {
            // usbwr.exe only has branches for request types 1, 6 and 8.
            SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
            continue;
        }
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        WeatherSenderResult result = weather_sender_build_weather_segment(
            selector, weather, response, (size_t)length, frame);
        if (result == WEATHER_SENDER_UNSUPPORTED_SELECTOR) {
            if (changed) {
                printf("Selector %u is not enabled; no response sent\n", selector);
            }
        } else if (result != WEATHER_SENDER_OK) {
            fprintf(stderr, "[-] Selector %u not answered: %s\n", selector,
                    weather_sender_result_text(result));
        } else if (!write_native_segment(
                       backend, device, frame,
                       WEATHER_FRAME_NATIVE_SEGMENT_CELLS)) {
            return false;
        } else {
            printf("Answered selector %u with a checksummed forecast segment\n",
                   selector);
        }
        SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
    }
    return true;
}

// Experimental: register, then write many varied checksum-valid segments
// (selector 6 icon/temperature sweep, later-day first/second) with a labelled
// step number so an observer can report which step changed the display.
static bool run_sweep_writes(USBBackend* backend, USBDevice* device)
{
    uint8_t base[WEATHER_FRAME_SIZE];
    int length = 0;
    if (!backend->read_data(device, base, sizeof(base), &length) || length < 10) {
        fprintf(stderr, "[-] Prefix read failed; no write sent\n");
        return false;
    }

    uint8_t reg[WEATHER_FRAME_SIZE];
    WeatherRegistrationId id;
    srand((unsigned)time(NULL));
    weather_registration_generate_id(&id);
    weather_registration_build_frame(&id, reg);
    if (!backend->write_data(device, reg, sizeof(reg))) {
        return false;
    }
    printf("Step 0: registration sent\n");
    fflush(stdout);
    SLEEP(500);

    int step = 0;
    for (int icon = 0; icon <= 79; icon++) {
        uint8_t prefix[WEATHER_FRAME_SIZE];
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        memcpy(prefix, base, sizeof(prefix));
        WeatherFrameNativeSelector6Fields f = selector6_fixture_test_fields();
        f.morning_icon_index = (uint16_t)icon;
        f.temperature_high = (uint16_t)(icon % 40);
        f.afternoon_direction_index = (uint16_t)(icon % 8);
        if (weather_frame_build_native_selector6(prefix, sizeof(prefix), &f, frame)) {
            printf("Step %d: selector-6 icon=%d temp=%d\n", ++step, icon, f.temperature_high);
            fflush(stdout);
            write_native_segment(backend, device, frame,
                                 WEATHER_FRAME_NATIVE_SEGMENT_CELLS);
            SLEEP(1200);
        }
    }

    for (unsigned sel = 8; sel <= 17; sel++) {
        uint8_t prefix[WEATHER_FRAME_SIZE];
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        memcpy(prefix, base, sizeof(prefix));
        prefix[1] = (uint8_t)(0x10 | sel);
        bool ok;
        if (sel % 2 == 0) {
            WeatherFrameNativeLaterDayFirstFields f = {
                .temperature_high = 20, .temperature_low = 10,
                .morning_icon_index = 3, .wind_speed = 5,
                .weather_direction_index = 2, .humidity = 50,
                .afternoon_icon_index = 4, .afternoon_wind_speed = 6,
                .afternoon_direction_index = 3, .afternoon_humidity = 55,
                .night_icon_index = 5
            };
            ok = weather_frame_build_native_later_day_first(prefix, sizeof(prefix), &f, frame);
        } else {
            WeatherFrameNativeLaterDaySecondFields f = {
                .night_wind_speed = 3, .night_direction_index = 1,
                .night_humidity = 60, .sunrise_hour = 6, .sunrise_minute = 30,
                .sunset_hour = 20, .sunset_minute = 15,
                .precipitation_percent = 40, .rainfall = 2,
                .sunshine_hours = 8, .uv_index = 5, .dew_point = 12
            };
            ok = weather_frame_build_native_later_day_second(prefix, sizeof(prefix), &f, frame);
        }
        if (ok) {
            printf("Step %d: later-day selector %u\n", ++step, sel);
            fflush(stdout);
            write_native_segment(backend, device, frame,
                                 WEATHER_FRAME_NATIVE_SEGMENT_CELLS);
            SLEEP(1200);
        }
    }

    uint8_t resp[WEATHER_FRAME_SIZE];
    length = 0;
    if (backend->read_data(device, resp, sizeof(resp), &length)) {
        print_read_response("Final read", resp, length);
    }
    printf("Sweep done\n");
    return true;
}

static bool write_retry(USBBackend* backend, USBDevice* device,
                        const uint8_t* cells, size_t count)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        if (write_native_segment(backend, device, cells, count)) {
            return true;
        }
        SLEEP(20);
    }
    return false;
}

// Experimental fuzzer: registers, then mixes structured segments (random
// field values over the registration echo prefix) with raw random nibble-cell
// frames, logging every non-idle response. Observe the display meanwhile.
static bool run_fuzz_writes(USBBackend* backend, USBDevice* device, int rounds)
{
    uint8_t prefix[WEATHER_FRAME_SIZE];
    int length = 0;
    srand((unsigned)time(NULL));

    WeatherRegistrationId id;
    uint8_t reg[WEATHER_FRAME_SIZE];
    weather_registration_generate_id(&id);
    weather_registration_build_frame(&id, reg);
    if (!backend->write_data(device, reg, sizeof(reg))) {
        return false;
    }
    SLEEP(300);
    if (!backend->read_data(device, prefix, sizeof(prefix), &length) || length < 10) {
        return false;
    }
    print_read_response("Registration echo", prefix, length);

    for (int round = 1; round <= rounds; round++) {
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        int mode = rand() % 4;
        bool ok = false;
        if (mode == 0) {
            WeatherFrameNativeSelector6Fields f = {
                .temperature_high = (uint16_t)(rand() & 0x1ff),
                .temperature_low = (uint16_t)(rand() & 0x1ff),
                .morning_icon_index = (uint16_t)(rand() % 80),
                .wind_speed = (uint16_t)(rand() & 0x7ff),
                .beaufort = (uint16_t)(rand() & 3),
                .weather_word = (uint16_t)(rand() & 0xff),
                .afternoon_direction_index = (uint16_t)(rand() % 9),
                .afternoon_wind_speed = (uint16_t)(rand() & 0x7f),
                .afternoon_beaufort = (uint16_t)(rand() & 0xff),
                .afternoon_weather_word = (uint16_t)(rand() & 0x1f),
                .afternoon_humidity = (uint16_t)(rand() & 0xff)
            };
            ok = weather_frame_build_native_selector6(prefix, (size_t)length, &f, frame);
        } else if (mode == 1) {
            uint8_t p[WEATHER_FRAME_SIZE];
            memcpy(p, prefix, sizeof(p));
            p[1] = (uint8_t)(0x10 | (rand() & 0x0f));
            WeatherFrameNativeLaterDayFirstFields f = {
                .temperature_high = (uint16_t)(rand() & 0x1ff),
                .temperature_low = (uint16_t)(rand() & 0x1ff),
                .morning_icon_index = (uint16_t)(rand() % 80),
                .wind_speed = (uint16_t)(rand() & 0x7ff),
                .weather_direction_index = (uint16_t)(rand() % 9),
                .humidity = (uint16_t)(rand() & 0xff),
                .afternoon_icon_index = (uint16_t)(rand() % 80),
                .afternoon_wind_speed = (uint16_t)(rand() & 0x7f),
                .afternoon_direction_index = (uint16_t)(rand() % 9),
                .afternoon_humidity = (uint16_t)(rand() & 0xff),
                .night_icon_index = (uint16_t)(rand() % 80)
            };
            ok = weather_frame_build_native_later_day_first(p, sizeof(p), &f, frame);
        } else {
            for (size_t i = 0; i < sizeof(frame); i++) {
                frame[i] = (uint8_t)(mode == 2 ? (0x10 | (rand() & 0x0f))
                                               : (rand() & 0xff));
            }
            ok = true;
        }
        if (!ok) {
            continue;
        }
        bool sent = write_retry(backend, device, frame, sizeof(frame));
        printf("Round %d mode %d %s\n", round, mode, sent ? "sent" : "FAILED");
        fflush(stdout);
        for (int poll = 0; poll < 4; poll++) {
            uint8_t resp[WEATHER_FRAME_SIZE];
            int n = 0;
            SLEEP(100);
            if (backend->read_data(device, resp, sizeof(resp), &n) && n >= 16) {
                for (int i = 0; i < 16; i++) {
                    if (resp[i] != 0x10) {
                        char label[48];
                        snprintf(label, sizeof(label), "  NON-IDLE after round %d", round);
                        print_read_response(label, resp, n);
                        memcpy(prefix, resp, sizeof(prefix));
                        length = n;
                        break;
                    }
                }
            }
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    bool listen_mode = argc > 1 && strcmp(argv[1], "--serve-weather") == 0;
    int listen_seconds = 0;
    WeatherLegacyResponse weather;
    bool replay_observed_frame = argc > 1 &&
                                 strcmp(argv[1], "--replay-observed-frame") == 0;
    bool read_once = argc > 1 && strcmp(argv[1], "--read-once") == 0;
    bool probe_registration_once = argc > 1 &&
                                   strcmp(argv[1], "--probe-registration-once") == 0;
    bool send_observed_weather_payload_once = argc > 1 &&
        strcmp(argv[1], "--send-observed-weather-payload-once") == 0;
    bool send_selector6_fixture_test_once = argc > 1 &&
        strcmp(argv[1], "--send-selector6-fixture-test-once") == 0;
    bool print_selector6_fixture_test = argc > 1 &&
        strcmp(argv[1], "--print-selector6-fixture-test") == 0;
    bool print_registration_frame = argc > 1 &&
                                    strcmp(argv[1], "--print-registration-frame") == 0;
    bool print_weather_frame = argc > 1 &&
                              strcmp(argv[1], "--print-weather-frame") == 0;

    if (listen_mode) {
        if ((argc != 4 && argc != 5) || !parse_positive_seconds(argv[3], &listen_seconds)) {
            fprintf(stderr,
                    "Usage: %s --serve-weather <legacy-weather-file> <seconds> [reg-code]\n",
                    argv[0]);
            return 2;
        }
        if (!read_weather_file(argv[2], &weather)) return 1;
    }

    if (print_selector6_fixture_test) {
        uint8_t readback[WEATHER_FRAME_SIZE];
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        WeatherFrameNativeSelector6Fields fields = selector6_fixture_test_fields();
        memset(readback, 0x10, sizeof(readback));
        if (!weather_frame_build_native_selector6(readback, sizeof(readback),
                                                   &fields, frame)) {
            fprintf(stderr, "[-] Could not build selector-6 fixture test frame\n");
            return 1;
        }
        print_selector6_segment(frame);
        printf("Readback prefix is the observed idle marker state; icon/direction use unknown fallbacks.\n");
        return 0;
    }

    if (print_weather_frame) {
        WeatherFrameTime sample_time = {9, 17, 14, 30};
        uint8_t frame[WEATHER_FRAME_SIZE];
        if (!weather_frame_build_proven_upd(&sample_time, frame)) {
            printf("[-] Could not build weather frame\n");
            return 1;
        }
        printf("frame:");
        for (int i = 0; i < WEATHER_FRAME_SIZE; i++) printf(" %02x", frame[i]);
        printf("\n");
        return 0;
    }

    if (print_registration_frame) {
        WeatherRegistrationId id;
        uint8_t frame[WEATHER_REGISTRATION_FRAME_SIZE];
        weather_registration_generate_id(&id);
        weather_registration_build_frame(&id, frame);
        printf("id nibbles:");
        for (int i = 0; i < WEATHER_REGISTRATION_ID_NIBBLES; i++) printf(" %x", id.nibbles[i]);
        printf("\nframe:");
        for (int i = 0; i < WEATHER_REGISTRATION_FRAME_SIZE; i++) printf(" %02x", frame[i]);
        printf("\n");
        return 0;
    }

    printf("="
);
    printf("Weather-Life USB Device Discoverer\n");
    printf("="
);
    printf("\n");
    
    // Get USB backend for current platform
    USBBackend* backend = usb_get_backend();
    if (!backend) {
        fprintf(stderr, "Error: No USB backend available for this platform\n");
        return 1;
    }
    
    printf("[*] Platform detected: ");
    #ifdef WEATHER_LIFE_WINDOWS
        printf("Windows (WinUSB)\n");
    #elif WEATHER_LIFE_LINUX
        printf("Linux (libusb)\n");
    #elif WEATHER_LIFE_MACOS
        printf("macOS (IOKit)\n");
    #else
        printf("Unknown\n");
    #endif
    
    printf("[*] Enumerating known weather devices...\n\n");
    
    // Enumerate devices
    WeatherDeviceID* devices = NULL;
    int device_count = 0;
    
    if (!backend->enumerate_devices(&devices, &device_count)) {
        fprintf(stderr, "Error: Failed to enumerate devices\n");
        return 1;
    }
    
    printf("Known device IDs:\n");
    for (int i = 0; i < device_count && devices[i].vid != 0; i++) {
        printf("  %d. VID:0x%04x PID:0x%04x - %s\n",
               i + 1, devices[i].vid, devices[i].pid, devices[i].name);
        printf("     Description: %s\n", devices[i].description);
    }
    printf("\n");
    
    // Try to find and open device
    printf("[*] Attempting to locate weather device...\n");
    
    USBDevice device;
    memset(&device, 0, sizeof(USBDevice));
    
    if (!usb_find_weather_device(&device)) {
        fprintf(stderr, "\n[!] Error: Weather device not found!\n");
        fprintf(stderr, "Troubleshooting:\n");
        fprintf(stderr, "  1. Plug in the weather dongle\n");
        fprintf(stderr, "  2. Wait 2-3 seconds for device to enumerate\n");
        fprintf(stderr, "  3. Run again\n");
        
        printf("\nWindows: Check Device Manager for unknown devices\n");
        printf("Linux: Run 'lsusb' to see connected USB devices\n");
        printf("macOS: Run 'system_profiler SPUSBDataType'\n");
        
        backend->free_device_list(devices);
        return 1;
    }
    
    printf("\n[+] SUCCESS! Found device:\n");
    printf("    VID: 0x%04x\n", device.vid);
    printf("    PID: 0x%04x\n", device.pid);
    printf("    Path: %s\n", device.device_path);
    printf("    Timeout: %d ms\n", device.timeout_ms);

    if (argc > 2 && strcmp(argv[1], "--fuzz-writes") == 0) {
        bool ok = run_fuzz_writes(backend, &device, atoi(argv[2]));
        usb_close(&device);
        backend->free_device_list(devices);
        return ok ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--sweep-writes") == 0) {
        bool ok = run_sweep_writes(backend, &device);
        usb_close(&device);
        backend->free_device_list(devices);
        return ok ? 0 : 1;
    }

    if (listen_seconds > 0) {
        bool ok = run_listen_and_answer(backend, &device, &weather,
                                        listen_seconds,
                                        argc == 5 ? argv[4] : NULL);
        usb_close(&device);
        backend->free_device_list(devices);
        return ok ? 0 : 1;
    }

    if (send_selector6_fixture_test_once) {
        bool sent_and_polled = run_selector6_fixture_test_once(backend, &device);
        usb_close(&device);
        backend->free_device_list(devices);
        return sent_and_polled ? 0 : 1;
    }

    if (probe_registration_once || send_observed_weather_payload_once) {
        uint8_t response[64];
        int response_length = 0;
        if (!backend->read_data(&device, response, sizeof(response),
                                &response_length)) {
            fprintf(stderr, "[-] Baseline read failed; no payload sent\n");
            usb_close(&device);
            backend->free_device_list(devices);
            return 1;
        }
        if (probe_registration_once) {
            printf("Response before registration: %d bytes (redacted)\n",
                   response_length);
        } else {
            print_read_response("Response before write", response,
                                response_length);
        }

        uint8_t payload[WEATHER_FRAME_SIZE];
        WeatherRegistrationId registration_id;
        if (probe_registration_once) {
            srand((unsigned)time(NULL));
            weather_registration_generate_id(&registration_id);
            weather_registration_build_frame(&registration_id, payload);
        } else if (argc != 3 || !parse_payload_hex(argv[2], payload)) {
            fprintf(stderr,
                    "[-] Expected exactly 32 hex digits for the captured 16-byte payload\n");
            usb_close(&device);
            backend->free_device_list(devices);
            return 2;
        }

        if (!backend->write_data(&device, payload, sizeof(payload))) {
            fprintf(stderr, "[-] One-shot payload write failed\n");
            usb_close(&device);
            backend->free_device_list(devices);
            return 1;
        }
        printf("Sent one %s payload.\n",
               probe_registration_once ? "registration" : "captured weather");
        SLEEP(100);

        response_length = 0;
        if (!backend->read_data(&device, response, sizeof(response),
                                &response_length)) {
            fprintf(stderr, "[-] Follow-up read failed\n");
            usb_close(&device);
            backend->free_device_list(devices);
            return 1;
        }
        if (probe_registration_once) {
            if (response_length >= WEATHER_REGISTRATION_FRAME_SIZE) {
                uint8_t decoded[WEATHER_REGISTRATION_FRAME_SIZE];
                weather_registration_decode_response(response, decoded);
                printf("Registration acknowledgment: %s\n",
                       weather_registration_response_matches(decoded,
                                                             &registration_id)
                           ? "matched" : "not matched");
            } else {
                printf("Registration acknowledgment: response too short\n");
            }
        } else {
            print_read_response("Response after write", response,
                                response_length);
        }

        usb_close(&device);
        backend->free_device_list(devices);
        return 0;
    }

    if (read_once) {
        uint8_t response[256];
        int response_length = 0;
        bool read_ok = backend->read_data(&device, response,
                                          sizeof(response), &response_length);
        if (read_ok) {
            printf("Read response (%d bytes):", response_length);
            for (int i = 0; i < response_length; i++) {
                printf(" %02x", response[i]);
            }
            printf("\n");
        } else {
            fprintf(stderr, "[-] Confirmed read poll failed\n");
        }
        usb_close(&device);
        backend->free_device_list(devices);
        return read_ok ? 0 : 1;
    }
    
    // Try to initialize device
    printf("\n[*] Initializing device...\n");
    
    if (!usb_init_device(&device)) {
        fprintf(stderr, "[!] Warning: Device initialization may have failed\n");
        fprintf(stderr, "    But device is still open - trying to continue\n");
    }

    if (replay_observed_frame) {
        if (usb_replay_captured_registration_frame(&device)) {
            printf("[+] Replayed the captured registration-handshake frame\n");
        } else {
            printf("[-] Registration replay is disabled until transport framing is proven\n");
        }
        usb_close(&device);
        backend->free_device_list(devices);
        return 0;
    }
    
    // Test sending command
    printf("\n[*] Testing USB communication...\n");
    
    uint8_t test_data[] = {0x01, 0x02, 0x03};
    if (backend->send_command(&device, CAL_USB_READ, test_data, sizeof(test_data))) {
        printf("[+] Successfully sent test command\n");
    } else {
        printf("[-] Test command failed (device may not support this)\n");
    }
    
    // Try to get status
    uint8_t status = 0;
    SLEEP(500);  // Wait for response
    
    if (backend->get_status(&device, &status)) {
        printf("[+] Device status: 0x%02x\n", status);
    } else {
        printf("[-] Could not get device status\n");
    }
    
    // Send sample weather data
    printf("\n[*] Testing weather data transmission...\n");
    
    WeatherData sample_weather = {
        .temperature = 22.5f,
        .humidity = 65,
        .wind_speed = 3.2f,
        .wind_direction = 180,
        .weather_code = 0x02,  // Cloudy
        .pressure = 1013
    };
    
    if (usb_send_weather_data(&device, &sample_weather)) {
        printf("[+] Successfully sent weather data:\n");
        printf("    Temperature: %.1f C\n", sample_weather.temperature);
        printf("    Humidity: %d%%\n", sample_weather.humidity);
        printf("    Wind: %.1f m/s at %d degrees\n",
               sample_weather.wind_speed, sample_weather.wind_direction);
    } else {
        printf("[-] Failed to send weather data\n");
    }
    
    // Close device
    printf("\n[*] Closing device...\n");
    usb_close(&device);
    
    backend->free_device_list(devices);
    
    printf("\n" "="
);
    printf("Device test completed successfully!\n");
    printf("="
);
    printf("\n");
    
    return 0;
}
