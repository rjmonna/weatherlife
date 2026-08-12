// weather-life/src/usb_windows.c
// Windows USB HID implementation using WinUSB and SetupDi APIs

#include "usb_device.h"
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")

// Known Weather Device IDs (to be populated from analysis)
static WeatherDeviceID known_devices[] = {
    // Microchip Technology (0x0424) - commonly used in hobbyist USB devices
    {0x0424, 0x274a, "Microchip USB Bridge", "Possible weather device variant 1"},
    
    // Silicon Labs (0x10c4) - CP2102/CP2104 common in weather stations
    {0x10c4, 0xea60, "Silicon Labs CP210x", "USB to Serial (weather data interface)"},
    {0x10c4, 0xea61, "Silicon Labs CP2103", "USB to Serial variant"},
    
    // Generic/DIY vendors
    {0x1209, 0x0001, "InterBiometrics Generic", "Open vendor ID for DIY devices"},
    {0x16c0, 0x0483, "Van Ooijen Technische", "DIY USB device"},
    
    // Prolific (common in cheap USB adapters)
    {0x067b, 0x2303, "Prolific PL2303", "USB Serial adapter for weather"},
    
    // FTDI (0x0403) - Popular for custom USB devices
    {0x0403, 0x6001, "FTDI FT232R", "USB Serial interface"},
    {0x0403, 0x6010, "FTDI FT2232D", "Dual channel USB"},
    
    {0, 0, NULL, NULL}  // Sentinel
};

typedef struct {
    WINUSB_INTERFACE_HANDLE DeviceHandle;
    HANDLE DeviceFile;
    USB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
} DeviceContext;

// Find weather device by enumerating USB devices
static bool enumerate_devices_windows(WeatherDeviceID** out_devices, int* out_count)
{
    *out_devices = known_devices;
    *out_count = sizeof(known_devices) / sizeof(known_devices[0]) - 1;
    return true;
}

// Open USB device using Windows SetupDi API
static bool open_device_windows(const char* device_path, USBDevice* device)
{
    HANDLE deviceHandle = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE winusbHandle = INVALID_HANDLE_VALUE;
    
    if (!device) return false;
    
    // Open device file
    deviceHandle = CreateFileA(
        device_path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );
    
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open device: %s\n", device_path);
        return false;
    }
    
    // Initialize WinUSB
    if (!WinUsb_Initialize(deviceHandle, &winusbHandle)) {
        fprintf(stderr, "Failed to initialize WinUSB\n");
        CloseHandle(deviceHandle);
        return false;
    }
    
    // Store handles
    DeviceContext* ctx = (DeviceContext*)malloc(sizeof(DeviceContext));
    if (!ctx) {
        WinUsb_Free(winusbHandle);
        CloseHandle(deviceHandle);
        return false;
    }
    
    ctx->DeviceHandle = winusbHandle;
    ctx->DeviceFile = deviceHandle;
    device->handle = ctx;
    strncpy_s(device->device_path, sizeof(device->device_path), device_path, _TRUNCATE);
    device->timeout_ms = 5000;
    
    return true;
}

// Close USB device
static bool close_device_windows(USBDevice* device)
{
    if (!device || !device->handle) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    
    if (ctx->DeviceHandle != INVALID_HANDLE_VALUE) {
        WinUsb_Free(ctx->DeviceHandle);
    }
    
    if (ctx->DeviceFile != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->DeviceFile);
    }
    
    free(ctx);
    device->handle = NULL;
    
    return true;
}

// Read data from USB device
static bool read_data_windows(USBDevice* device, uint8_t* buffer, int buffer_size, int* bytes_read)
{
    if (!device || !device->handle || !buffer || !bytes_read) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    ULONG bytesRead = 0;
    
    // Read from bulk in endpoint (endpoint 0x81 is common)
    if (!WinUsb_ReadPipe(ctx->DeviceHandle, 0x81, buffer, buffer_size, &bytesRead, NULL)) {
        fprintf(stderr, "WinUsb_ReadPipe failed: %lu\n", GetLastError());
        return false;
    }
    
    *bytes_read = (int)bytesRead;
    return true;
}

// Write data to USB device
static bool write_data_windows(USBDevice* device, const uint8_t* buffer, int buffer_size)
{
    if (!device || !device->handle || !buffer) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    ULONG bytesWritten = 0;
    
    // Write to bulk out endpoint (endpoint 0x01 is common)
    if (!WinUsb_WritePipe(ctx->DeviceHandle, 0x01, (uint8_t*)buffer, buffer_size, &bytesWritten, NULL)) {
        fprintf(stderr, "WinUsb_WritePipe failed: %lu\n", GetLastError());
        return false;
    }
    
    return bytesWritten == buffer_size;
}

// Send USB command
static bool send_command_windows(USBDevice* device, uint8_t cmd, const uint8_t* data, int data_len)
{
    if (!device) return false;
    
    // Create command packet: [command_id][length][data...]
    uint8_t* packet = (uint8_t*)malloc(1 + 1 + data_len);
    if (!packet) return false;
    
    packet[0] = cmd;
    packet[1] = (uint8_t)data_len;
    
    if (data_len > 0 && data) {
        memcpy(packet + 2, data, data_len);
    }
    
    bool result = write_data_windows(device, packet, 1 + 1 + data_len);
    free(packet);
    
    return result;
}

// Get device status
static bool get_status_windows(USBDevice* device, uint8_t* status)
{
    if (!device || !status) return false;
    
    uint8_t buffer[64];
    int bytes_read = 0;
    
    // Send status query command
    if (!send_command_windows(device, CAL_USB_STATUS, NULL, 0)) {
        return false;
    }
    
    // Read response
    if (!read_data_windows(device, buffer, sizeof(buffer), &bytes_read)) {
        return false;
    }
    
    if (bytes_read > 0) {
        *status = buffer[0];
        return true;
    }
    
    return false;
}

// Free device list (no-op for Windows since we use static list)
static void free_device_list_windows(WeatherDeviceID* devices)
{
    // Static list, nothing to free
    (void)devices;
}

// Get Windows USB backend
static USBBackend windows_backend = {
    .enumerate_devices = enumerate_devices_windows,
    .open_device = open_device_windows,
    .close_device = close_device_windows,
    .read_data = read_data_windows,
    .write_data = write_data_windows,
    .send_command = send_command_windows,
    .get_status = get_status_windows,
    .free_device_list = free_device_list_windows,
};

USBBackend* usb_get_backend_windows(void)
{
    return &windows_backend;
}
