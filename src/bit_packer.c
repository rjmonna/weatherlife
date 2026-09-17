// Confirmed via GhidraMCP decompilation of usbwr.exe!FUN_0040d340 (2026-09-17).

#include "bit_packer.h"

static const uint8_t bit_masks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

void bit_packer_init(BitPacker* packer, uint8_t* buffer, size_t buffer_size)
{
    packer->buffer = buffer;
    packer->buffer_size = buffer_size;
    packer->bit_position = 0;
}

void bit_packer_append(BitPacker* packer, uint16_t value, int bit_count)
{
    for (int i = 0; i < bit_count; i++) {
        size_t byte_index = packer->bit_position / 8;
        int bit_index = (int)(packer->bit_position % 8);
        if (byte_index >= packer->buffer_size) return;

        int source_bit = (value >> (bit_count - 1 - i)) & 1;
        if (source_bit) {
            packer->buffer[byte_index] |= bit_masks[bit_index];
        }
        packer->bit_position++;
    }
}
