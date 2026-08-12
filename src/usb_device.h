// weather-life/src/usb_device.h
// Cross-platform USB HID abstraction layer

#ifndef WEATHER_LIFE_USB_DEVICE_H
#define WEATHER_LIFE_USB_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #define WEATHER_LIFE_WINDOWS 1
#elif __linux__
    #define WEATHER_LIFE_LINUX 1
#elif __APPLE__
    #define WEATHER_LIFE_MACOS 1
#endif

// USB Commands
#define CAL_USB_READ    0x01
#define CAL_USB_WRITE   0x02
#define CAL_USB_STATUS  0x03
#define CMD_DEVICE_INIT 0x04

// Known device IDs (will be populated from binary analysis)
typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char* name;
    const char* description;
} WeatherDeviceID;

// USB device handle
typedef struct {
    void* handle;  // Platform-specific handle
    uint16_t vid;
    uint16_t pid;
    char device_path[256];
    int timeout_ms;
} USBDevice;

// Weather data types
typedef struct {
    float temperature;      // Celsius
    int humidity;          // 0-100%
    float wind_speed;      // m/s
    uint16_t wind_direction; // 0-359 degrees
    uint8_t weather_code;  // Weather code
    int pressure;          // hPa
    float precipitation;   // mm
} WeatherData;

// USB operations
typedef struct {
    bool (*enumerate_devices)(WeatherDeviceID** out_devices, int* out_count);
    bool (*open_device)(const char* device_path, USBDevice* device);
    bool (*close_device)(USBDevice* device);
    bool (*read_data)(USBDevice* device, uint8_t* buffer, int buffer_size, int* bytes_read);
    bool (*write_data)(USBDevice* device, const uint8_t* buffer, int buffer_size);
    bool (*send_command)(USBDevice* device, uint8_t cmd, const uint8_t* data, int data_len);
    bool (*get_status)(USBDevice* device, uint8_t* status);
    void (*free_device_list)(WeatherDeviceID* devices);
} USBBackend;

// Core API
USBBackend* usb_get_backend(void);
bool usb_find_weather_device(USBDevice* device);
bool usb_init_device(USBDevice* device);
bool usb_send_weather_data(USBDevice* device, const WeatherData* data);
void usb_close(USBDevice* device);

#endif  // WEATHER_LIFE_USB_DEVICE_H
