// weather-life/src/usb_common.c
// Platform-agnostic USB operations

#include "usb_device.h"
#include "weather_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
#endif

// Get appropriate backend based on platform
USBBackend* usb_get_backend(void)
{
#ifdef WEATHER_LIFE_WINDOWS
    extern USBBackend* usb_get_backend_windows(void);
    return usb_get_backend_windows();
#elif WEATHER_LIFE_LINUX
    extern USBBackend* usb_get_backend_linux(void);
    return usb_get_backend_linux();
#elif WEATHER_LIFE_MACOS
    extern USBBackend* usb_get_backend_macos(void);
    return usb_get_backend_macos();
#else
    return NULL;
#endif
}

// High-level API: Find and open weather device
bool usb_find_weather_device(USBDevice* device)
{
    if (!device) return false;
    
    USBBackend* backend = usb_get_backend();
    if (!backend) {
        fprintf(stderr, "No USB backend available\n");
        return false;
    }
    
    // Enumerate devices
    WeatherDeviceID* devices = NULL;
    int device_count = 0;
    
    if (!backend->enumerate_devices(&devices, &device_count)) {
        fprintf(stderr, "Failed to enumerate devices\n");
        return false;
    }
    
    // Try to open each known device
    for (int i = 0; i < device_count; i++) {
        char device_path[256];
        snprintf(device_path, sizeof(device_path), "%04x:%04x",
                 devices[i].vid, devices[i].pid);
        
        printf("Trying device: %s (%s)\n", devices[i].name, device_path);
        
        if (backend->open_device(device_path, device)) {
            printf("SUCCESS: Opened %s\n", devices[i].name);
            backend->free_device_list(devices);
            return true;
        }
    }
    
    printf("Device not found. Tried %d known device IDs.\n", device_count);
    backend->free_device_list(devices);
    return false;
}

// Initialize device and test communication
bool usb_init_device(USBDevice* device)
{
    if (!device || !device->handle) return false;
    
    USBBackend* backend = usb_get_backend();
    if (!backend) return false;
    
    printf("Initializing device at %s\n", device->device_path);
    
    // Send initialization command for legacy HID devices.
    if (!backend->send_command(device, CMD_DEVICE_INIT, NULL, 0)) {
        fprintf(stderr, "Failed to send init command\n");
        return false;
    }
    
    // Small delay for device response
    #ifdef _WIN32
        Sleep(100);
    #else
        usleep(100000);
    #endif
    
    // Get device status
    uint8_t status = 0;
    if (!backend->get_status(device, &status)) {
        fprintf(stderr, "Failed to get device status\n");
        return false;
    }
    
    printf("Device status: 0x%02x\n", status);
    return true;
}

// Send weather data to display
bool usb_send_weather_data(USBDevice* device, const WeatherData* data)
{
    USBBackend* backend;
    WeatherFrameInput input;
    uint8_t frame[WEATHER_FRAME_SIZE];
    time_t now;
    struct tm current_time;

    if (!device || !device->handle || !data) return false;

    backend = usb_get_backend();
    if (!backend || !backend->write_data) return false;

    memset(&input, 0, sizeof(input));
    now = time(NULL);
#ifdef _WIN32
    if (localtime_s(&current_time, &now) != 0) return false;
#else
    if (!localtime_r(&now, &current_time)) return false;
#endif
    input.time.month = current_time.tm_mon + 1;
    input.time.day = current_time.tm_mday;
    input.time.hour = current_time.tm_hour;
    input.time.minute = current_time.tm_min;
    input.current_temperature_c = data->temperature;
    input.pressure_hpa = data->pressure > 0 ? (uint16_t)data->pressure : 0;
    input.wind_speed = data->wind_speed < 0 ? 0 : (uint16_t)data->wind_speed;
    input.humidity_percent = data->humidity < 0 ? 0 : (uint16_t)data->humidity;

    if (!weather_frame_build(&input, frame)) return false;

    // This writes the experimental 16-byte segment directly. Rainfall is not
    // included until its native bit width and offset are proven.
    return backend->write_data(device, frame, WEATHER_FRAME_SIZE);
}

bool usb_replay_captured_registration_frame(USBDevice* device)
{
    (void)device;
    fprintf(stderr,
            "Registration refused: onlywell.dll framing is confirmed, but replaying the device response path remains intentionally disabled until the full HID response contract is proven\n");
    return false;
}

// Close device connection
void usb_close(USBDevice* device)
{
    if (!device) return;
    
    USBBackend* backend = usb_get_backend();
    if (backend && backend->close_device) {
        backend->close_device(device);
    }
}
