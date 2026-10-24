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

static char g_upd_line[160];

static void remember_upd_line(const char* text, size_t length)
{
    const char* end = text + length;
    for (const char* line = text; line < end; ) {
        const char* eol = line;
        while (eol < end && *eol != '\n' && *eol != '\r') eol++;
        if (eol - line > 3 && strncmp(line, "UPD", 3) == 0) {
            size_t n = (size_t)(eol - line);
            if (n >= sizeof(g_upd_line)) n = sizeof(g_upd_line) - 1;
            memcpy(g_upd_line, line, n);
            g_upd_line[n] = '\0';
            return;
        }
        line = eol;
        while (line < end && (*line == '\n' || *line == '\r')) line++;
    }
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
        remember_upd_line(contents, (size_t)file_size);
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

// Respond only to decoded weather requests (outer type 6). The outer type-1
// registration/city exchange and type-8 control path use separate formats.
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

// usbwr.exe stamps the dongle with the weather file's UPD month/day/hour/minute.
static bool send_upd_timestamp(USBBackend* backend, USBDevice* device,
                               const WeatherLegacyResponse* weather,
                               bool use_current_time)
{
    unsigned parts[4] = {0, 0, 0, 0};
    size_t found = 0;
    uint8_t idle[WEATHER_FRAME_SIZE];
    uint8_t stamp[WEATHER_SESSION_CONTROL_CELLS];

    if (use_current_time) {
        time_t now = time(NULL);
        struct tm local = *localtime(&now);
        parts[0] = (unsigned)local.tm_mon + 1;
        parts[1] = (unsigned)local.tm_mday;
        parts[2] = (unsigned)local.tm_hour;
        parts[3] = (unsigned)local.tm_min;
        found = 5;
    }
    for (const char* p = g_upd_line; !use_current_time && *p && found < 5; p++) {
        if (*p != '<') continue;
        if (found++ == 0) continue;
        parts[found - 2] = (unsigned)strtoul(p + 1, NULL, 10);
    }
    if (found != 5) return false;
    memset(idle, 0x10, sizeof(idle));
    if (!weather_session_build_timestamp_frame(idle, sizeof(idle), 4,
            (uint8_t)parts[0], (uint8_t)parts[1], (uint8_t)parts[2],
            (uint8_t)parts[3], true, stamp)) {
        return false;
    }
    printf("Timestamp frame (UPD %02u/%02u %02u:%02u)\n", parts[0], parts[1],
           parts[2], parts[3]);
    return write_native_segment(backend, device, stamp, sizeof(stamp));
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
    const int stamp_period_ticks = 58000 / WEATHER_SESSION_POLL_INTERVAL_MS;

    weather_session_state_init(&session);
    for (int tick = 0; tick < seconds * 1000 / WEATHER_SESSION_POLL_INTERVAL_MS; tick++) {
        uint8_t response[WEATHER_FRAME_SIZE];
        int length = 0;
        if (reg_code && tick % stamp_period_ticks == 0 &&
            !send_upd_timestamp(backend, device, weather, false)) {
            fprintf(stderr, "[-] Could not send the UPD timestamp frame\n");
            return false;
        }
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
        bool clean = true;
        for (int i = 0; i < length; i++) {
            if ((response[i] & 0xf0) != 0x10) clean = false;
        }
        if (!clean) {
            SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
            continue;
        }
        bool changed = !have_last || length != last_length ||
                       memcmp(last, response, (size_t)length) != 0;
        WeatherSenderRequest request;
        if (!weather_sender_decode_request(response, (size_t)length, &request)) {
            if (changed) {
                fprintf(stderr, "[-] Readback has invalid request cells; no response sent\n");
            }
            SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
            continue;
        }
        if (changed) {
            printf("Device request type %u", request.request_type);
            if (request.request_type == 6) {
                printf(", weather selector %u", request.selector);
            }
            printf("; readback:");
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
        // Type 1 is the unresolved city exchange; type 8 is a control frame.
        // Only type 6 dispatches the decoded inner weather selector.
        if (idle || request.request_type != 6) {
            SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
            continue;
        }
        if (!weather_sender_request_matches_city(&request, weather)) {
            if (changed) {
                fprintf(stderr,
                        "[-] Weather request city %s does not match the loaded data; "
                        "no response sent\n",
                        request.city_code);
            }
            SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
            continue;
        }
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        WeatherSenderResult result = weather_sender_build_weather_segment(
            request.selector, weather, response, (size_t)length, frame);
        if (result == WEATHER_SENDER_UNSUPPORTED_SELECTOR) {
            if (changed) {
                printf("Weather selector %u is not enabled; no response sent\n",
                       request.selector);
            }
        } else if (result != WEATHER_SENDER_OK) {
            fprintf(stderr, "[-] Weather selector %u not answered: %s\n",
                    request.selector,
                    weather_sender_result_text(result));
        } else if (!write_native_segment(
                       backend, device, frame,
                       WEATHER_FRAME_NATIVE_SEGMENT_CELLS)) {
            return false;
        } else {
            printf("Answered weather selector %u with a checksummed segment\n",
                   request.selector);
        }
        SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
    }
    return true;
}

// Explicitly authorized, bounded one-shot send: selector 6 then 8, 10, ..., 16
// (Days 1-6), reading the device back before and after each write.
static bool run_unsolicited_send(USBBackend* backend, USBDevice* device,
                                 const WeatherLegacyResponse* weather,
                                 const char* city_code, const char* reg_code)
{
    const unsigned selectors[] = {6, 8, 10, 12, 14, 16};

    if (strlen(city_code) != 5 || strspn(city_code, "0123456789") != 5) {
        fprintf(stderr, "[-] City code must be five digits\n");
        return false;
    }

    if (reg_code && !run_registration_and_init(backend, device, reg_code)) {
        fprintf(stderr, "[-] Registration/init failed\n");
        return false;
    }
    if (reg_code && !send_upd_timestamp(backend, device, weather, true)) {
        fprintf(stderr, "[-] Could not send the UPD timestamp frame\n");
        return false;
    }
    SLEEP(WEATHER_SESSION_POLL_INTERVAL_MS);
    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        uint8_t response[WEATHER_FRAME_SIZE];
        uint8_t frame[WEATHER_FRAME_NATIVE_SEGMENT_CELLS];
        int length = 0;

        bool read_ok = false;
        for (int attempt = 0; attempt < 5 && !read_ok; attempt++) {
            read_ok = backend->read_data(device, response, sizeof(response), &length);
            if (!read_ok) SLEEP(500);
        }
        if (!read_ok) {
            fprintf(stderr, "[-] Readback failed; stopping\n");
            return false;
        }
        print_read_response("Readback before write", response, length);
        // The dongle sent no request, so synthesize the type-6 request prefix
        // that the sender normally echoes from the readback.
        uint8_t request[WEATHER_FRAME_SIZE];
        memset(request, 0x10, sizeof(request));
        request[1] = 0x16;
        for (int d = 0; d < 5; d++) request[2 + d] = (uint8_t)(0x10 | (city_code[d] - '0'));
        request[7] = (uint8_t)(0x10 | (selectors[i] >> 4));
        request[8] = (uint8_t)(0x10 | (selectors[i] & 0x0f));
        WeatherSenderResult result = weather_sender_build_weather_segment(
            selectors[i], weather, request, sizeof(request), frame);
        if (result != WEATHER_SENDER_OK) {
            fprintf(stderr, "[-] Selector %u not built: %s\n", selectors[i],
                    weather_sender_result_text(result));
            return false;
        }
        if (!write_native_segment(backend, device, frame,
                                  WEATHER_FRAME_NATIVE_SEGMENT_CELLS)) {
            return false;
        }
        printf("Wrote selector %u segment\n", selectors[i]);
        SLEEP(500);
        if (backend->read_data(device, response, sizeof(response), &length)) {
            print_read_response("Readback after write", response, length);
        }
        SLEEP(1500);
    }
    return true;
}

int main(int argc, char* argv[])
{
    bool send_unsolicited = argc > 1 &&
        strcmp(argv[1], "--send-weather-unsolicited") == 0;
    if (send_unsolicited && (argc != 4 && argc != 5)) {
        fprintf(stderr, "Usage: %s --send-weather-unsolicited <legacy-weather-file> <5-digit-city-code> [reg-code]\n",
                argv[0]);
        return 2;
    }
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

    if (send_unsolicited) {
        WeatherLegacyResponse data;
        bool ok = read_weather_file(argv[2], &data) &&
                  run_unsolicited_send(backend, &device, &data, argv[3],
                                       argc == 5 ? argv[4] : NULL);
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
