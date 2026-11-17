#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOTP_PACKET_SIZE 304U

typedef struct {
    uint8_t address[4];
    uint8_t mask[4];
    uint8_t gateway[4];
} bootp_address_t;

typedef struct {
    const char *file_identity;
    const char *vendor_identity;
    bool legacy_vendor_identity;
} bootp_request_profile_t;

size_t bootp_make_profiled_request(uint8_t *bytes, size_t capacity, const uint8_t mac[6],
                                  uint32_t transaction, uint16_t seconds,
                                  const bootp_request_profile_t *profile);

size_t bootp_make_request(uint8_t *bytes, size_t capacity, const uint8_t mac[6],
                          uint32_t transaction, uint16_t seconds,
                          const char *loader_version, const char *application_version);
bool bootp_parse_reply(const uint8_t *bytes, size_t size, const uint8_t mac[6],
                       uint32_t transaction, bootp_address_t *address);