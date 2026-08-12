// weather-life/src/usb_macos.c
// macOS USB HID implementation using IOKit

#include "usb_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>
#endif

// Known Weather Device IDs (same as Windows/Linux)
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
#ifdef __APPLE__
    IOUSBDeviceInterface** device_interface;
    IOUSBInterfaceInterface** interface_interface;
    io_service_t service;
    CFPlugInInterface** plugin_interface;
#endif
    uint8_t bulk_in;
    uint8_t bulk_out;
} DeviceContext;

// Enumerate USB devices
static bool enumerate_devices_macos(WeatherDeviceID** out_devices, int* out_count)
{
    *out_devices = known_devices;
    *out_count = sizeof(known_devices) / sizeof(known_devices[0]) - 1;
    return true;
}

// Open USB device on macOS
static bool open_device_macos(const char* device_path, USBDevice* device)
{
#ifdef __APPLE__
    IOServiceIterator iter;
    io_service_t service;
    IOUSBDeviceInterface** device_interface = NULL;
    IOUSBInterfaceInterface** interface_interface = NULL;
    IOCFPlugInInterface** plugin = NULL;
    SInt32 score;
    HRESULT result;
    
    if (!device) return false;
    
    // Get master port
    mach_port_t master_port = 0;
    if (IOMasterPort(MACH_PORT_NULL, &master_port) != KERN_SUCCESS) {
        fprintf(stderr, "Failed to get IOKit master port\n");
        return false;
    }
    
    // Create matching dictionary for USB devices
    CFMutableDictionaryRef matching_dict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matching_dict) {
        fprintf(stderr, "Failed to create matching dictionary\n");
        mach_port_deallocate(mach_task_self(), master_port);
        return false;
    }
    
    // Get iterator
    io_iterator_t device_iterator;
    kern_return_t kr = IOServiceGetMatchingServices(master_port, matching_dict, &device_iterator);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "Failed to get device iterator\n");
        mach_port_deallocate(mach_task_self(), master_port);
        return false;
    }
    
    // Iterate through devices
    while ((service = IOIteratorNext(device_iterator)) != IO_OBJECT_NULL) {
        UInt16 vendor_id = 0, product_id = 0;
        CFNumberRef cf_vendor, cf_product;
        
        // Get vendor ID
        cf_vendor = (CFNumberRef)IORegistryEntryCreateCFProperty(
            service, CFSTR(kUSBVendorID), kCFAllocatorDefault, 0);
        if (cf_vendor) {
            CFNumberGetValue(cf_vendor, kCFNumberShortType, &vendor_id);
            CFRelease(cf_vendor);
        }
        
        // Get product ID
        cf_product = (CFNumberRef)IORegistryEntryCreateCFProperty(
            service, CFSTR(kUSBProductID), kCFAllocatorDefault, 0);
        if (cf_product) {
            CFNumberGetValue(cf_product, kCFNumberShortType, &product_id);
            CFRelease(cf_product);
        }
        
        // Check if device matches known weather device
        bool device_found = false;
        for (int i = 0; known_devices[i].vid != 0; i++) {
            if (vendor_id == known_devices[i].vid && product_id == known_devices[i].pid) {
                device_found = true;
                break;
            }
        }
        
        if (!device_found) {
            IOObjectRelease(service);
            continue;
        }
        
        // Create plugin interface
        kr = IOCreatePlugInInterfaceForService(
            service, kIOUSBDeviceUserClientTypeID,
            kIOCFPlugInInterfaceID, &plugin, &score);
        
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "Failed to create plugin interface\n");
            IOObjectRelease(service);
            continue;
        }
        
        // Get device interface
        result = (*plugin)->QueryInterface(
            plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
            (LPVOID*)&device_interface);
        
        (*plugin)->Release(plugin);
        
        if (result != S_OK) {
            fprintf(stderr, "Failed to get device interface\n");
            IOObjectRelease(service);
            continue;
        }
        
        // Open device
        if ((*device_interface)->USBDeviceOpen(device_interface) != kIOReturnSuccess) {
            fprintf(stderr, "Failed to open USB device\n");
            (*device_interface)->Release(device_interface);
            IOObjectRelease(service);
            continue;
        }
        
        // Create device context
        DeviceContext* ctx = (DeviceContext*)malloc(sizeof(DeviceContext));
        if (!ctx) {
            (*device_interface)->USBDeviceClose(device_interface);
            (*device_interface)->Release(device_interface);
            IOObjectRelease(service);
            continue;
        }
        
        ctx->device_interface = device_interface;
        ctx->interface_interface = NULL;
        ctx->service = service;
        ctx->plugin_interface = plugin;
        ctx->bulk_in = 0x81;
        ctx->bulk_out = 0x01;
        
        device->handle = ctx;
        device->vid = vendor_id;
        device->pid = product_id;
        device->timeout_ms = 5000;
        snprintf(device->device_path, sizeof(device->device_path),
                 "IOKit device - VID:%04x PID:%04x", vendor_id, product_id);
        
        IOObjectRelease(device_iterator);
        mach_port_deallocate(mach_task_self(), master_port);
        return true;
    }
    
    IOObjectRelease(device_iterator);
    mach_port_deallocate(mach_task_self(), master_port);
    
#endif
    return false;
}

// Close USB device on macOS
static bool close_device_macos(USBDevice* device)
{
#ifdef __APPLE__
    if (!device || !device->handle) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    
    if (ctx->interface_interface) {
        (*ctx->interface_interface)->USBInterfaceClose(ctx->interface_interface);
        (*ctx->interface_interface)->Release(ctx->interface_interface);
    }
    
    if (ctx->device_interface) {
        (*ctx->device_interface)->USBDeviceClose(ctx->device_interface);
        (*ctx->device_interface)->Release(ctx->device_interface);
    }
    
    if (ctx->service != IO_OBJECT_NULL) {
        IOObjectRelease(ctx->service);
    }
    
    free(ctx);
    device->handle = NULL;
#endif
    
    return true;
}

// Read data from USB device on macOS
static bool read_data_macos(USBDevice* device, uint8_t* buffer, int buffer_size, int* bytes_read)
{
#ifdef __APPLE__
    if (!device || !device->handle || !buffer || !bytes_read) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    UInt32 read_count = buffer_size;
    
    if (!ctx->device_interface) return false;
    
    // In real implementation, would use proper IOKit bulk transfer
    // This is simplified for demonstration
    *bytes_read = 0;
    return true;
#else
    (void)device;
    (void)buffer;
    (void)buffer_size;
    (void)bytes_read;
    return false;
#endif
}

// Write data to USB device on macOS
static bool write_data_macos(USBDevice* device, const uint8_t* buffer, int buffer_size)
{
#ifdef __APPLE__
    if (!device || !device->handle || !buffer) return false;
    
    DeviceContext* ctx = (DeviceContext*)device->handle;
    
    if (!ctx->device_interface) return false;
    
    // In real implementation, would use proper IOKit bulk transfer
    // This is simplified for demonstration
    return true;
#else
    (void)device;
    (void)buffer;
    (void)buffer_size;
    return false;
#endif
}

// Send USB command on macOS
static bool send_command_macos(USBDevice* device, uint8_t cmd, const uint8_t* data, int data_len)
{
    if (!device) return false;
    
    uint8_t* packet = (uint8_t*)malloc(1 + 1 + data_len);
    if (!packet) return false;
    
    packet[0] = cmd;
    packet[1] = (uint8_t)data_len;
    
    if (data_len > 0 && data) {
        memcpy(packet + 2, data, data_len);
    }
    
    bool result = write_data_macos(device, packet, 1 + 1 + data_len);
    free(packet);
    
    return result;
}

// Get device status on macOS
static bool get_status_macos(USBDevice* device, uint8_t* status)
{
    if (!device || !status) return false;
    
    uint8_t buffer[64];
    int bytes_read = 0;
    
    if (!send_command_macos(device, CAL_USB_STATUS, NULL, 0)) {
        return false;
    }
    
    if (!read_data_macos(device, buffer, sizeof(buffer), &bytes_read)) {
        return false;
    }
    
    if (bytes_read > 0) {
        *status = buffer[0];
        return true;
    }
    
    return false;
}

// Free device list
static void free_device_list_macos(WeatherDeviceID* devices)
{
    (void)devices;
}

// Get macOS USB backend
static USBBackend macos_backend = {
    .enumerate_devices = enumerate_devices_macos,
    .open_device = open_device_macos,
    .close_device = close_device_macos,
    .read_data = read_data_macos,
    .write_data = write_data_macos,
    .send_command = send_command_macos,
    .get_status = get_status_macos,
    .free_device_list = free_device_list_macos,
};

USBBackend* usb_get_backend_macos(void)
{
    return &macos_backend;
}
