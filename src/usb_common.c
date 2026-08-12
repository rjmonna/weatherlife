// weather-life/src/usb_common.c
// Platform-agnostic USB operations

#include "usb_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    
    // Send initialization command
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
    if (!device || !device->handle || !data) return false;
    
    USBBackend* backend = usb_get_backend();
    if (!backend) return false;
    
    // Format weather data for LCD display
    // Format: [temp_byte][humidity_byte][condition_byte][wind_speed_word][wind_dir_word]
    uint8_t packet[16];
    int packet_len = 0;
    
    // Temperature (0-100, mapping to display range)
    packet[packet_len++] = (uint8_t)(data->temperature + 50);  // Offset for negatives
    
    // Humidity percentage
    packet[packet_len++] = (uint8_t)data->humidity;
    
    // Weather condition code
    packet[packet_len++] = data->weather_code;
    
    // Wind speed (as 16-bit value)
    *(uint16_t*)(packet + packet_len) = (uint16_t)(data->wind_speed * 10);  // Scale to int
    packet_len += 2;
    
    // Wind direction (0-359)
    *(uint16_t*)(packet + packet_len) = data->wind_direction;
    packet_len += 2;
    
    // Send to device
    return backend->send_command(device, CAL_USB_WRITE, packet, packet_len);
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
