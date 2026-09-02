// weather-life/src/usb_windows.c
// Windows HID implementation using SetupDi and HID.DLL

#include "usb_device.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

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
    HANDLE device_file;
    PHIDP_PREPARSED_DATA preparsed_data;
    HIDP_CAPS caps;
} DeviceContext;

static bool is_known_device(const HIDD_ATTRIBUTES* attributes)
{
    for (int i = 0; known_devices[i].vid != 0; i++) {
        if (attributes->VendorID == known_devices[i].vid &&
            attributes->ProductID == known_devices[i].pid) {
            return true;
        }
    }
    return false;
}

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
    GUID hid_guid;
    HDEVINFO device_info;
    SP_DEVICE_INTERFACE_DATA interface_data;

    (void)device_path;
    if (!device) return false;
    HidD_GetHidGuid(&hid_guid);
    device_info = SetupDiGetClassDevsA(&hid_guid, NULL, NULL,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) return false;

    memset(&interface_data, 0, sizeof(interface_data));
    interface_data.cbSize = sizeof(interface_data);
    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(device_info, NULL, &hid_guid, index,
                                     &interface_data);
         index++) {
        DWORD detail_size = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = NULL;
        HANDLE file = INVALID_HANDLE_VALUE;
        HIDD_ATTRIBUTES attributes;
        PHIDP_PREPARSED_DATA preparsed_data = NULL;
        HIDP_CAPS caps;
        DeviceContext* context;

        SetupDiGetDeviceInterfaceDetailA(device_info, &interface_data, NULL, 0,
                                         &detail_size, NULL);
        if (!detail_size) continue;
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(detail_size);
        if (!detail) continue;
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailA(device_info, &interface_data,
                                              detail, detail_size, NULL, NULL)) {
            free(detail);
            continue;
        }

        file = CreateFileA(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
        memset(&attributes, 0, sizeof(attributes));
        attributes.Size = sizeof(attributes);
        if (file == INVALID_HANDLE_VALUE || !HidD_GetAttributes(file, &attributes) ||
            !is_known_device(&attributes) || !HidD_GetPreparsedData(file, &preparsed_data) ||
            HidP_GetCaps(preparsed_data, &caps) != HIDP_STATUS_SUCCESS) {
            if (preparsed_data) HidD_FreePreparsedData(preparsed_data);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            free(detail);
            continue;
        }

        context = (DeviceContext*)malloc(sizeof(*context));
        if (!context) {
            HidD_FreePreparsedData(preparsed_data);
            CloseHandle(file);
            free(detail);
            continue;
        }
        context->device_file = file;
        context->preparsed_data = preparsed_data;
        context->caps = caps;
        device->handle = context;
        device->vid = attributes.VendorID;
        device->pid = attributes.ProductID;
        strncpy_s(device->device_path, sizeof(device->device_path),
                  detail->DevicePath, _TRUNCATE);
        device->timeout_ms = 5000;
        free(detail);
        SetupDiDestroyDeviceInfoList(device_info);
        return true;
    }

    SetupDiDestroyDeviceInfoList(device_info);
    return false;
}

// Close USB device
static bool close_device_windows(USBDevice* device)
{
    DeviceContext* context;
    if (!device || !device->handle) return false;

    context = (DeviceContext*)device->handle;
    if (context->preparsed_data) HidD_FreePreparsedData(context->preparsed_data);
    if (context->device_file != INVALID_HANDLE_VALUE) CloseHandle(context->device_file);
    free(context);
    device->handle = NULL;
    return true;
}

// Read data from USB device
static bool read_data_windows(USBDevice* device, uint8_t* buffer, int buffer_size, int* bytes_read)
{
    DeviceContext* context;
    BYTE* report;
    ULONG report_size;
    if (!device || !device->handle || !buffer || !bytes_read || buffer_size < 0) return false;

    context = (DeviceContext*)device->handle;
    report_size = context->caps.FeatureReportByteLength;
    if (report_size <= 1 || buffer_size < (int)(report_size - 1)) return false;
    report = (BYTE*)calloc(report_size, sizeof(BYTE));
    if (!report) return false;
    if (!HidD_GetFeature(context->device_file, report, report_size)) {
        free(report);
        return false;
    }
    memcpy(buffer, report + 1, report_size - 1);
    *bytes_read = (int)report_size - 1;
    free(report);
    return true;
}

// Write data to USB device
static bool write_data_windows(USBDevice* device, const uint8_t* buffer, int buffer_size)
{
    DeviceContext* context;
    BYTE* report;
    ULONG report_size;
    bool result;
    if (!device || !device->handle || !buffer || buffer_size < 0) return false;

    context = (DeviceContext*)device->handle;
    report_size = context->caps.FeatureReportByteLength;
    if (report_size <= 1 || buffer_size > (int)(report_size - 1)) return false;
    report = (BYTE*)calloc(report_size, sizeof(BYTE));
    if (!report) return false;
    memcpy(report + 1, buffer, buffer_size);
    result = HidD_SetFeature(context->device_file, report, report_size) != FALSE;
    free(report);
    return result;
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
