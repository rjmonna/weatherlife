#include "usb_device.h"
#include "hid_transport.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define TENX_VENDOR_ID 0x1130
#define TENX_PRODUCT_ID 0x0202

static const WeatherDeviceID tenx_device = {
    TENX_VENDOR_ID, TENX_PRODUCT_ID, "Tenx HID", "Composite HID weather display"
};

static bool is_supported_device_path(const char* device_path)
{
    char* end;
    unsigned long vendor_id;
    unsigned long product_id;

    if (!device_path || strlen(device_path) != 9 || device_path[4] != ':') {
        return false;
    }
    for (int i = 0; i < 9; i++) {
        if (i != 4 && !isxdigit((unsigned char)device_path[i])) return false;
    }
    vendor_id = strtoul(device_path, &end, 16);
    if (end != device_path + 4 || *end != ':') return false;
    product_id = strtoul(end + 1, &end, 16);
    return end == device_path + 9 && vendor_id == TENX_VENDOR_ID &&
           product_id == TENX_PRODUCT_ID;
}

static bool enumerate_devices_macos(WeatherDeviceID** out_devices, int* out_count)
{
    bool present;

    if (!out_devices || !out_count) return false;
    *out_devices = NULL;
    *out_count = 0;
    if (!hid_transport_device_present(TENX_VENDOR_ID, TENX_PRODUCT_ID,
                                     &present)) return false;
    if (!present) return true;

    *out_devices = (WeatherDeviceID*)malloc(sizeof(**out_devices));
    if (!*out_devices) return false;
    **out_devices = tenx_device;
    *out_count = 1;
    return true;
}

static bool open_device_macos(const char* device_path, USBDevice* device)
{
    HidTransport* transport = NULL;
    char opened_path[sizeof(device->device_path)] = {0};

    if (!device || device->handle || !is_supported_device_path(device_path) ||
        !hid_transport_open(TENX_VENDOR_ID, TENX_PRODUCT_ID, &transport,
                            opened_path, sizeof(opened_path))) return false;
    device->handle = transport;
    device->vid = TENX_VENDOR_ID;
    device->pid = TENX_PRODUCT_ID;
    memcpy(device->device_path, opened_path, sizeof(device->device_path));
    device->timeout_ms = 5000;
    return true;
}

static bool close_device_macos(USBDevice* device)
{
    if (!device || !device->handle) return false;
    hid_transport_close((HidTransport*)device->handle);
    device->handle = NULL;
    return true;
}

static bool read_data_macos(USBDevice* device, uint8_t* buffer, int buffer_size,
                            int* bytes_read)
{
    if (!device || !device->handle || !bytes_read) return false;
    *bytes_read = 0;
    return hid_transport_read_feature((HidTransport*)device->handle, buffer,
                                      buffer_size, bytes_read);
}

static bool write_data_macos(USBDevice* device, const uint8_t* buffer, int buffer_size)
{
    return device && device->handle &&
           hid_transport_write((HidTransport*)device->handle, buffer, buffer_size);
}

static bool send_command_macos(USBDevice* device, uint8_t cmd,
                               const uint8_t* data, int data_len)
{
    if (!device || !device->handle || cmd != CAL_USB_WRITE || data_len < 0 ||
        (data_len > 0 && !data)) return false;
    return write_data_macos(device, data, data_len);
}

static bool get_status_macos(USBDevice* device, uint8_t* status)
{
    if (!device || !device->handle) return false;
    return hid_transport_get_status((HidTransport*)device->handle, status);
}

static void free_device_list_macos(WeatherDeviceID* devices) { free(devices); }

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

USBBackend* usb_get_backend_macos(void) { return &macos_backend; }
