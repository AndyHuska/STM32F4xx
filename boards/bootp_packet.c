#include "bootp_packet.h"

#include <string.h>

static uint32_t read_network_word(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void copy_identity(uint8_t *destination, const char *source)
{
    if(source)
        for(size_t index = 0; index < 64 && source[index]; index++)
            destination[index] = (uint8_t)source[index];
}

static bool valid_address(const uint8_t bytes[4])
{
    return bytes[0] && bytes[0] < 224 && bytes[0] != 127;
}

size_t bootp_make_profiled_request(uint8_t *bytes, size_t capacity, const uint8_t mac[6],
                                  uint32_t transaction, uint16_t seconds,
                                  const bootp_request_profile_t *profile)
{
    if(!bytes || !mac || capacity < BOOTP_PACKET_SIZE)
        return 0;
    memset(bytes, 0, BOOTP_PACKET_SIZE);
    bytes[0] = 1;
    bytes[1] = 1;
    bytes[2] = 6;
    bytes[4] = (uint8_t)(transaction >> 24);
    bytes[5] = (uint8_t)(transaction >> 16);
    bytes[6] = (uint8_t)(transaction >> 8);
    bytes[7] = (uint8_t)transaction;
    bytes[8] = (uint8_t)(seconds >> 8);
    bytes[9] = (uint8_t)seconds;
    bytes[10] = 0x80;
    memcpy(bytes + 28, mac, 6);
    if(profile)
        copy_identity(bytes + 108, profile->file_identity);
    if(profile && profile->legacy_vendor_identity)
        copy_identity(bytes + 240, profile->vendor_identity);
    else {
        bytes[236] = 99;
        bytes[237] = 130;
        bytes[238] = 83;
        bytes[239] = 99;
        bytes[240] = 255;
    }
    return BOOTP_PACKET_SIZE;
}

size_t bootp_make_request(uint8_t *bytes, size_t capacity, const uint8_t mac[6],
                          uint32_t transaction, uint16_t seconds,
                          const char *loader_version, const char *application_version)
{
    bootp_request_profile_t profile = {
        .file_identity = loader_version,
        .vendor_identity = application_version,
        .legacy_vendor_identity = true
    };
    return bootp_make_profiled_request(bytes, capacity, mac, transaction, seconds,
                                      loader_version || application_version ? &profile : NULL);
}

bool bootp_parse_reply(const uint8_t *bytes, size_t size, const uint8_t mac[6],
                       uint32_t transaction, bootp_address_t *address)
{
    if(!bytes || !mac || !address || size < 236 || bytes[0] != 2 ||
       bytes[1] != 1 || bytes[2] != 6 || read_network_word(bytes + 4) != transaction ||
       memcmp(bytes + 28, mac, 6) || !valid_address(bytes + 16))
        return false;

    bootp_address_t result = {0};
    memcpy(result.address, bytes + 16, 4);
    bool mask_seen = false, gateway_seen = false;
    if(size >= 240 && read_network_word(bytes + 236) == UINT32_C(0x63825363)) {
        size_t offset = 240;
        while(offset < size) {
            uint8_t option = bytes[offset++];
            if(option == 255)
                break;
            if(option == 0)
                continue;
            if(offset == size)
                return false;
            uint8_t length = bytes[offset++];
            if(length > size - offset)
                return false;
            if(option == 53 || option == 52)
                return false;
            if(option == 1) {
                if(length != 4 || mask_seen)
                    return false;
                memcpy(result.mask, bytes + offset, 4);
                mask_seen = true;
            } else if(option == 3) {
                if(length < 4 || length % 4 || gateway_seen || !valid_address(bytes + offset))
                    return false;
                memcpy(result.gateway, bytes + offset, 4);
                gateway_seen = true;
            }
            offset += length;
        }
    }
    if(!mask_seen) {
        result.mask[0] = 255;
        result.mask[1] = result.address[0] >= 128 ? 255 : 0;
        result.mask[2] = result.address[0] >= 192 ? 255 : 0;
    }
    uint32_t mask = read_network_word(result.mask);
    uint32_t host = ~mask;
    uint32_t ip = read_network_word(result.address);
    if(!mask || (host & (host + 1U)) ||
       (host > 1U && ((ip & host) == 0 || (ip & host) == host)))
        return false;
    *address = result;
    return true;
}