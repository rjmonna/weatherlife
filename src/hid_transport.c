#include "hid_transport.h"
#include "usb_device.h"
#include <hidapi/hidapi.h>
#include <stdlib.h>
#include <string.h>

#define TENX_USAGE_PAGE 1
#define TENX_COMMAND_USAGE 3
#define TENX_PAYLOAD_USAGE 0
#define TENX_COMMAND_REPORT_SIZE 9
#define TENX_PAYLOAD_REPORT_SIZE 17
#define TENX_FEATURE_REPORT_SIZE 65

struct HidTransport {
    hid_device* payload_device;
    hid_device* command_device;
};

bool hid_transport_open(uint16_t vendor_id, uint16_t product_id,
                        HidTransport** out_transport, char* path,
                        size_t path_size)
{
    struct hid_device_info* devices;
    struct hid_device_info* current;
    const char* payload_path = NULL;
    const char* command_path = NULL;
    HidTransport* transport;

    if (!out_transport) return false;
    *out_transport = NULL;
    if (hid_init() != 0) return false;

    devices = hid_enumerate(vendor_id, product_id);
    for (current = devices; current; current = current->next) {
        if (current->usage_page != TENX_USAGE_PAGE) continue;
        if (current->usage == TENX_PAYLOAD_USAGE) payload_path = current->path;
        if (current->usage == TENX_COMMAND_USAGE) command_path = current->path;
    }
    if (!payload_path || !command_path) {
        hid_free_enumeration(devices);
        hid_exit();
        return false;
    }

    transport = (HidTransport*)calloc(1, sizeof(*transport));
    if (!transport) {
        hid_free_enumeration(devices);
        hid_exit();
        return false;
    }
    transport->payload_device = hid_open_path(payload_path);
    transport->command_device = hid_open_path(command_path);
    hid_free_enumeration(devices);
    if (!transport->payload_device || !transport->command_device) {
        hid_transport_close(transport);
        return false;
    }
    if (path && path_size != 0) {
        strncpy(path, payload_path, path_size - 1);
        path[path_size - 1] = '\0';
    }
    *out_transport = transport;
    return true;
}

void hid_transport_close(HidTransport* transport)
{
    if (!transport) return;
    if (transport->payload_device) hid_close(transport->payload_device);
    if (transport->command_device) hid_close(transport->command_device);
    free(transport);
    hid_exit();
}

bool hid_transport_read_feature(HidTransport* transport, uint8_t* buffer,
                                int buffer_size, int* bytes_read)
{
    unsigned char command_report[TENX_COMMAND_REPORT_SIZE] = {0};
    unsigned char report[TENX_FEATURE_REPORT_SIZE] = {0};
    int result;
    int copy_size;

    if (!transport || !transport->payload_device || !buffer || !bytes_read ||
        buffer_size < 0) return false;
    command_report[1] = 0x55;
    command_report[2] = 0x53;
    command_report[3] = 0x42;
    command_report[4] = 0x43;
    command_report[7] = CAL_USB_READ;
    if (hid_write(transport->command_device, command_report,
                  sizeof(command_report)) != (int)sizeof(command_report)) {
        return false;
    }
    result = hid_get_feature_report(transport->payload_device, report,
                                    sizeof(report));
    if (result < 1) return false;
    copy_size = result - 1;
    if (copy_size > buffer_size) copy_size = buffer_size;
    memcpy(buffer, report + 1, (size_t)copy_size);
    *bytes_read = copy_size;
    return true;
}

bool hid_transport_write(HidTransport* transport, const uint8_t* buffer,
                         int buffer_size)
{
    unsigned char command_report[TENX_COMMAND_REPORT_SIZE] = {0};
    unsigned char payload_report[TENX_PAYLOAD_REPORT_SIZE] = {0};

    if (!transport || !transport->payload_device || !transport->command_device ||
        !buffer || buffer_size < 0 || buffer_size > TENX_PAYLOAD_REPORT_SIZE - 1) {
        return false;
    }
    command_report[1] = 0x55;
    command_report[2] = 0x53;
    command_report[3] = 0x42;
    command_report[4] = 0x43;
    command_report[5] = (uint8_t)((uint16_t)buffer_size >> 8);
    command_report[6] = (uint8_t)buffer_size;
    command_report[7] = CAL_USB_WRITE;
    command_report[8] = 0;
    if (hid_write(transport->command_device, command_report,
                  sizeof(command_report)) != (int)sizeof(command_report)) {
        return false;
    }
    memcpy(payload_report + 1, buffer, (size_t)buffer_size);
    return hid_write(transport->payload_device, payload_report,
                     sizeof(payload_report)) == (int)sizeof(payload_report);
}
