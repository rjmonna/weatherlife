// Confirmed via GhidraMCP decompilation of usbwr.exe!FUN_0040d340 (2026-09-17).

#include "bit_packer.h"

static const uint8_t bit_masks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

void bit_packer_init(BitPacker* packer, uint8_t* buffer, size_t buffer_size)
{
    packer->buffer = buffer;
    packer->buffer_size = buffer_size;
    packer->bit_position = 0;
    packer->native_byte_offset = 0;
    packer->native_bit_offset = 0;
    packer->native_cursor = 0;
}

void bit_packer_init_at(BitPacker* packer, uint8_t* buffer, size_t buffer_size,
                        size_t byte_offset, int bit_offset)
{
    packer->buffer = buffer;
    packer->buffer_size = buffer_size;
    packer->bit_position = byte_offset * 8 + (size_t)bit_offset;
    packer->native_byte_offset = byte_offset;
    packer->native_bit_offset = bit_offset;
    packer->native_cursor = 1;
}

void bit_packer_append(BitPacker* packer, uint16_t value, int bit_count)
{
    for (int i = 0; i < bit_count; i++) {
        size_t byte_index;
        int bit_index;

        if (packer->native_cursor) {
            byte_index = packer->native_byte_offset;
            bit_index = packer->native_bit_offset;
        } else {
            byte_index = packer->bit_position / 8;
            bit_index = (int)(packer->bit_position % 8);
        }
        if (byte_index >= packer->buffer_size) return;

        int source_bit = (value >> (bit_count - 1 - i)) & 1;
        if (source_bit) {
            packer->buffer[byte_index] |= bit_masks[bit_index];
        }
        packer->bit_position++;
        if (packer->native_cursor) {
            packer->native_bit_offset++;
            if (packer->native_bit_offset == 8) {
                packer->native_byte_offset++;
                packer->native_bit_offset = 4;
            }
        }
    }
}
