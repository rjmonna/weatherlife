// weather-life/src/usb_linux.c
// Linux USB HID implementation using libusb and hidapi

#include "usb_device.h"
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Known Weather Device IDs (same as Windows)
static WeatherDeviceID known_devices[] = {
    {0x0424, 0x274a, "Microchip USB Bridge", "Possible weather device variant 1"},
    {0x10c4, 0xea60, "Silicon Labs CP210x", "USB to Serial (weather data interface)"},
    {0x10c4, 0xea61, "Silicon Labs CP2103", "USB to Serial variant"},
    {0x1209, 0x0001, "InterBiometrics Generic", "Open vendor ID for DIY devices"},
    {0x16c0, 0x0483, "Van Ooijen Technische", "DIY USB device"},
    {0x067b, 0x2303, "Prolific PL2303", "USB Serial adapter for weather"},
    {0x0403, 0x6001, "FTDI FT232R", "USB Serial interface"},
    {0x0403, 0x6010, "FTDI FT2232D", "Dual channel USB"},
    {0, 0, NULL, NULL}
};

typedef struct {
    libusb_device_handle* device_handle;
    libusb_context* context;
    uint8_t bulk_in;
    uint8_t bulk_out;
} DeviceContext;

// Initialize libusb
static bool initialize_libusb(libusb_context** context)
{
    int result = libusb_init(context);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Failed to initialize libusb: %s\n", libusb_strerror(result));
        return false;
    }
    return true;
}

// Enumerate USB devices
static bool enumerate_devices_linux(WeatherDeviceID** out_devices, int* out_count)
{
    *out_devices = known_devices;
    *out_count = sizeof(known_devices) / sizeof(known_devices[0]) - 1;
    return true;
}

// Open USB device
static bool open_device_linux(const char* device_path, USBDevice* device)
{
    libusb_context* context = NULL;
    libusb_device** device_list = NULL;
    libusb_device_handle* handle = NULL;
    ssize_t device_count = 0;
    
    if (!device) return false;
    
    // Initialize libusb
    if (!initialize_libusb(&context)) {
        return false;
    }
    
    // Get device list
    device_count = libusb_get_device_list(context, &device_list);
    if (device_count < 0) {
        fprintf(stderr, "Failed to get device list\n");
        libusb_exit(context);
        return false;
    }
    
    // Open first weather device found
    for (ssize_t i = 0; i < device_count; i++) {
        struct libusb_device_descriptor desc;
        
        if (libusb_get_device_descriptor(device_list[i], &desc) != LIBUSB_SUCCESS) {
            continue;
        }
        
        // Check if device matches known weather device
        for (int j = 0; known_devices[j].vid != 0; j++) {
            if (desc.idVendor == known_devices[j].vid && 
                desc.idProduct == known_devices[j].pid) {
                
                // Found matching device
                if (libusb_open(device_list[i], &handle) != LIBUSB_SUCCESS) {
                    fprintf(stderr, "Failed to open device\n");
                    continue;
                }
                
                // Create device context
                DeviceContext* ctx = (DeviceContext*)malloc(sizeof(DeviceContext));
                if (!ctx) {
                    libusb_close(handle);
                    libusb_free_device_list(device_list, 1);
                    libusb_exit(context);
                    return false;
                }
                
                ctx->device_handle = handle;
                ctx->context = context;
                ctx->bulk_in = 0x81;   // Common bulk in endpoint
                ctx->bulk_out = 0x01;  // Common bulk out endpoint
                
                device->handle = ctx;
                device->vid = desc.idVendor;
                device->pid = desc.idProduct;
                device->timeout_ms = 5000;
                snprintf(device->device_path, sizeof(device->device_path), 
                         "bus=%03d dev=%03d", libusb_get_bus_number(device_list[i]),
                         libusb_get_device_address(device_list[i]));
                
                libusb_free_device_list(device_list, 0);
                return true;
            }
        }
    }
    
    libusb_free_device_list(device_list, 1);
    libusb_exit(context);
    return false;
}

// Close USB device
static bool close_device_linux(USBDevice* device)
{
    if (!device || !device->handle) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    
    if (ctx->device_handle) {
        libusb_close(ctx->device_handle);
    }
    
    if (ctx->context) {
        libusb_exit(ctx->context);
    }
    
    free(ctx);
    device->handle = NULL;
    
    return true;
}

// Read data from USB device
static bool read_data_linux(USBDevice* device, uint8_t* buffer, int buffer_size, int* bytes_read)
{
    if (!device || !device->handle || !buffer || !bytes_read) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    int actual_length = 0;
    
    int result = libusb_bulk_transfer(ctx->device_handle, ctx->bulk_in, buffer, 
                                       buffer_size, &actual_length, device->timeout_ms);
    
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Read failed: %s\n", libusb_strerror(result));
        return false;
    }
    
    *bytes_read = actual_length;
    return true;
}

// Write data to USB device
static bool write_data_linux(USBDevice* device, const uint8_t* buffer, int buffer_size)
{
    if (!device || !device->handle || !buffer) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    int actual_length = 0;
    
    int result = libusb_bulk_transfer(ctx->device_handle, ctx->bulk_out, (uint8_t*)buffer,
                                       buffer_size, &actual_length, device->timeout_ms);
    
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Write failed: %s\n", libusb_strerror(result));
        return false;
    }
    
    return actual_length == buffer_size;
}

// Send USB command
static bool send_command_linux(USBDevice* device, uint8_t cmd, const uint8_t* data, int data_len)
{
    if (!device) return false;
    
    uint8_t* packet = (uint8_t*)malloc(1 + 1 + data_len);
    if (!packet) return false;
    
    packet[0] = cmd;
    packet[1] = (uint8_t)data_len;
    
    if (data_len > 0 && data) {
        memcpy(packet + 2, data, data_len);
    }
    
    bool result = write_data_linux(device, packet, 1 + 1 + data_len);
    free(packet);
    
    return result;
}

// Get device status
static bool get_status_linux(USBDevice* device, uint8_t* status)
{
    if (!device || !status) return false;
    
    uint8_t buffer[64];
    int bytes_read = 0;
    
    if (!send_command_linux(device, CAL_USB_STATUS, NULL, 0)) {
        return false;
    }
    
    if (!read_data_linux(device, buffer, sizeof(buffer), &bytes_read)) {
        return false;
    }
    
    if (bytes_read > 0) {
        *status = buffer[0];
        return true;
    }
    
    return false;
}

// Free device list
static void free_device_list_linux(WeatherDeviceID* devices)
{
    (void)devices;
}

// Get Linux USB backend
static USBBackend linux_backend = {
    .enumerate_devices = enumerate_devices_linux,
    .open_device = open_device_linux,
    .close_device = close_device_linux,
    .read_data = read_data_linux,
    .write_data = write_data_linux,
    .send_command = send_command_linux,
    .get_status = get_status_linux,
    .free_device_list = free_device_list_linux,
};

USBBackend* usb_get_backend_linux(void)
{
    return &linux_backend;
}
