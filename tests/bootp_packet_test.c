#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../boards/bootp_packet.h"
#include "../boards/bootp_pmc_profile.h"

static const uint8_t mac[6] = {0x40, 0xD8, 0x55, 0x19, 0xC0, 0};

static void reply(uint8_t *bytes)
{
    assert(bootp_make_request(bytes, BOOTP_PACKET_SIZE, mac, 0x12345678, 3, NULL, NULL) == BOOTP_PACKET_SIZE);
    bytes[0] = 2;
    bytes[16] = 192;
    bytes[17] = 168;
    bytes[18] = 1;
    bytes[19] = 218;
}

int main(void)
{
    uint8_t bytes[BOOTP_PACKET_SIZE];
    bootp_address_t address;
    bootp_request_profile_t pmc = bootp_pmc_profile(NULL, NULL);
    size_t request_size = bootp_make_profiled_request(bytes, sizeof(bytes), mac, 0x12345678, 0, &pmc);
    assert(request_size >= 304 && bytes[108] == 'P' && bytes[109] == 'M' && bytes[110] == 'C');
    assert(bytes[0] == 1 && bytes[1] == 1 && bytes[2] == 6);
    assert(!strcmp((char *)bytes + 108, "PMC-02-AA_GRBLHAL"));
    assert(!strcmp((char *)bytes + 240, "PMC-02-AA_GRBLHAL"));
    assert(bytes[236] == 0);
    pmc = bootp_pmc_profile("PMC-02-AA_1.0.6BL", "PMC-02-AA_APP2");
    assert(bootp_make_profiled_request(bytes, sizeof(bytes), mac, 1, 0, &pmc) == 304);
    assert(!strcmp((char *)bytes + 108, "PMC-02-AA_1.0.6BL"));
    assert(!strcmp((char *)bytes + 240, "PMC-02-AA_APP2"));
    pmc = bootp_pmc_profile("NOT_A_PMC", "APP");
    assert(bootp_make_profiled_request(bytes, sizeof(bytes), mac, 1, 0, &pmc) == 304);
    assert(!memcmp(bytes + 108, "PMC", 3));
    assert(bootp_make_profiled_request(bytes, sizeof(bytes), mac, 1, 0, NULL) == 304);
    assert(bytes[108] == 0 && bytes[236] == 99 && bytes[239] == 99 && bytes[240] == 255);
    bootp_request_profile_t standard = {.file_identity = "boot.bin"};
    assert(bootp_make_profiled_request(bytes, sizeof(bytes), mac, 1, 0, &standard) == 304);
    assert(!strcmp((char *)bytes + 108, "boot.bin") && bytes[236] == 99 && bytes[240] == 255);
    assert(bootp_make_request(bytes, sizeof(bytes), mac, 0x12345678, 258, "PMC-02-AA_1.0.6BL", "APP") == 304);
    assert(bytes[0] == 1 && bytes[1] == 1 && bytes[2] == 6);
    assert(bytes[4] == 0x12 && bytes[7] == 0x78);
    assert(bytes[8] == 1 && bytes[9] == 2 && bytes[10] == 0x80 && bytes[11] == 0);
    assert(memcmp(bytes + 28, mac, 6) == 0);
    assert(strcmp((char *)bytes + 108, "PMC-02-AA_1.0.6BL") == 0);
    assert(bytes[236] == 0 && strcmp((char *)bytes + 240, "APP") == 0);
    assert(!bootp_make_request(bytes, 303, mac, 1, 0, NULL, NULL));
    bytes[0] = 2;
    memcpy(bytes + 16, (uint8_t[]){192, 168, 1, 218}, 4);
    assert(bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    assert(memcmp(address.mask, (uint8_t[]){255, 255, 255, 0}, 4) == 0);
    assert(memcmp(address.gateway, (uint8_t[]){0, 0, 0, 0}, 4) == 0);

    reply(bytes);
    uint8_t options[] = {1, 4, 255, 255, 255, 0, 3, 8, 192, 168, 1, 1, 192, 168, 1, 2, 255};
    memcpy(bytes + 240, options, sizeof(options));
    assert(bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    assert(address.address[3] == 218 && address.gateway[3] == 1 && address.mask[2] == 255);
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345679, &address));
    bytes[28] ^= 1;
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    bytes[28] ^= 1;
    assert(!bootp_parse_reply(bytes, 235, mac, 0x12345678, &address));
    assert(!bootp_parse_reply(bytes, 244, mac, 0x12345678, &address));
    bytes[0] = 1;
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    bytes[0] = 2;
    bytes[2] = 5;
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    bytes[2] = 6;
    bytes[243] = 0;
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    reply(bytes);
    memcpy(bytes + 240, (uint8_t[]){53, 1, 2, 255}, 4);
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    reply(bytes);
    memcpy(bytes + 240, (uint8_t[]){52, 1, 1, 255}, 4);
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    reply(bytes);
    memcpy(bytes + 240, (uint8_t[]){1, 4, 255, 0, 255, 0, 255}, 7);
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    reply(bytes);
    bytes[19] = 255;
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    reply(bytes);
    bytes[16] = 224;
    assert(!bootp_parse_reply(bytes, sizeof(bytes), mac, 0x12345678, &address));
    reply(bytes);
    bytes[16] = 10;
    assert(bootp_parse_reply(bytes, 236, mac, 0x12345678, &address));
    assert(address.mask[0] == 255 && address.mask[1] == 0);
    reply(bytes);
    bytes[16] = 172;
    assert(bootp_parse_reply(bytes, 236, mac, 0x12345678, &address));
    assert(address.mask[1] == 255 && address.mask[2] == 0);
    puts("BOOTP packet tests passed");
    return 0;
}