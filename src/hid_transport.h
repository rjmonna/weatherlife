#ifndef WEATHER_LIFE_HID_TRANSPORT_H
#define WEATHER_LIFE_HID_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct HidTransport HidTransport;

bool hid_transport_device_present(uint16_t vendor_id, uint16_t product_id,
                                 bool* out_present);
bool hid_transport_open(uint16_t vendor_id, uint16_t product_id,
                        HidTransport** out_transport, char* path,
                        size_t path_size);
void hid_transport_close(HidTransport* transport);
bool hid_transport_get_status(HidTransport* transport, uint8_t* status);
bool hid_transport_read_feature(HidTransport* transport, uint8_t* buffer,
                                 int buffer_size, int* bytes_read);
bool hid_transport_write(HidTransport* transport, const uint8_t* buffer,
                         int buffer_size);

#endif
