#include "usb_device.h"
#include "hid_transport.h"
#include <string.h>

static WeatherDeviceID known_devices[] = {
    {0x1130, 0x0202, "Tenx HID", "Composite HID weather display"},
    {0, 0, NULL, NULL}
};

static bool enumerate_devices_linux(WeatherDeviceID** out_devices, int* out_count)
{
    *out_devices = known_devices;
    *out_count = 1;
    return true;
}

static bool open_device_linux(const char* device_path, USBDevice* device)
{
    HidTransport* transport = NULL;
    (void)device_path;
    if (!device || !hid_transport_open(0x1130, 0x0202, &transport,
                                       device->device_path,
                                       sizeof(device->device_path))) return false;
    device->handle = transport;
    device->vid = 0x1130;
    device->pid = 0x0202;
    device->timeout_ms = 5000;
    return true;
}

static bool close_device_linux(USBDevice* device)
{
    if (!device || !device->handle) return false;
    hid_transport_close((HidTransport*)device->handle);
    device->handle = NULL;
    return true;
}

static bool read_data_linux(USBDevice* device, uint8_t* buffer, int buffer_size,
                            int* bytes_read)
{
    return device && hid_transport_read_feature((HidTransport*)device->handle,
                                                 buffer, buffer_size, bytes_read);
}

static bool write_data_linux(USBDevice* device, const uint8_t* buffer, int buffer_size)
{
    return device && hid_transport_write((HidTransport*)device->handle,
                                          buffer, buffer_size);
}

static bool send_command_linux(USBDevice* device, uint8_t cmd,
                               const uint8_t* data, int data_len)
{
    uint8_t packet[16] = {0};
    if (!device || data_len < 0 || data_len > (int)sizeof(packet) - 2) return false;
    packet[0] = cmd;
    packet[1] = (uint8_t)data_len;
    if (data_len > 0 && data) memcpy(packet + 2, data, (size_t)data_len);
    return write_data_linux(device, packet, (int)sizeof(packet));
}

static bool get_status_linux(USBDevice* device, uint8_t* status)
{
    uint8_t buffer[64];
    int bytes_read = 0;
    if (!device || !status || !read_data_linux(device, buffer, sizeof(buffer),
                                               &bytes_read)) return false;
    if (bytes_read == 0) return false;
    *status = buffer[0];
    return true;
}

static void free_device_list_linux(WeatherDeviceID* devices) { (void)devices; }

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

USBBackend* usb_get_backend_linux(void) { return &linux_backend; }
