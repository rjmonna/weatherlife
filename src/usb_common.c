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
    
    // DeviceIni is an enumeration/open operation; status performs the
    // confirmed read-header plus feature-report exchange.
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
    (void)device;
    (void)data;
    fprintf(stderr,
            "Weather transmission refused: native header field semantics and wire offsets remain unproven\n");
    return false;
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
