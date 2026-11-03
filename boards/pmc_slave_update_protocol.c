#include "pmc_slave_update_protocol.h"

uint32_t pmc_slave_update_crc(uint32_t crc, const uint8_t *bytes, size_t size)
{
    crc = ~crc;
    for(size_t index = 0; index < size; index++) {
        crc ^= bytes[index];
        for(uint8_t bit_index = 0; bit_index < 8; bit_index++)
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
    }
    return ~crc;
}

bool pmc_slave_update_request_valid(const pmc_slave_packet_t *packet)
{
    if(!packet || packet->command != (packet->command & UINT32_C(0xFFFF)))
        return false;
    switch(packet->command) {
        case PMC_SLAVE_UPDATE_INFO:
        case PMC_SLAVE_UPDATE_ABORT:
            return packet->word_count == 0;
        case PMC_SLAVE_UPDATE_BEGIN:
            return packet->word_count == 3;
        case PMC_SLAVE_UPDATE_COMMIT:
        case PMC_SLAVE_UPDATE_REBOOT:
            return packet->word_count == 1;
        case PMC_SLAVE_UPDATE_BLOCK:
            return packet->word_count > 3 && packet->word_count <= 131;
        default:
            return false;
    }
}