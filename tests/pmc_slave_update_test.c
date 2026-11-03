#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../boards/pmc_slave_update_server.h"

static uint8_t flash_bytes[2048], image[1536];
static unsigned writes, erases;
static bool fail_write;

static bool erase_flash(void *context)
{
    (void)context;
    memset(flash_bytes, 0xFF, sizeof(flash_bytes));
    erases++;
    return true;
}

static bool write_flash(void *context, uint32_t offset, const uint8_t *bytes, size_t size)
{
    (void)context;
    if(fail_write || offset > sizeof(flash_bytes) || size > sizeof(flash_bytes) - offset)
        return false;
    memcpy(flash_bytes + offset, bytes, size);
    writes++;
    return true;
}

static bool read_flash(void *context, uint32_t offset, uint8_t *bytes, size_t size)
{
    (void)context;
    if(offset > sizeof(flash_bytes) || size > sizeof(flash_bytes) - offset)
        return false;
    memcpy(bytes, flash_bytes + offset, size);
    return true;
}

static void put_word(uint8_t *bytes, uint32_t value)
{
    for(unsigned index = 0; index < 4; index++)
        bytes[index] = (uint8_t)(value >> (8 * index));
}

static pmc_slave_packet_t make_block(uint32_t session, uint32_t offset, size_t size)
{
    pmc_slave_packet_t packet = {
        .command = PMC_SLAVE_UPDATE_BLOCK, .sequence = offset + 1,
        .word_count = (uint16_t)(3 + size / 4),
        .words = {(int32_t)session, (int32_t)offset}
    };
    packet.words[2] = (int32_t)pmc_slave_update_crc(0, image + offset, size);
    for(size_t index = 0; index < size; index++)
        packet.words[3 + index / 4] |= (int32_t)((uint32_t)image[offset + index] << (8 * (index % 4)));
    return packet;
}

int main(void)
{
    assert(pmc_slave_update_crc(0, (const uint8_t *)"123456789", 9) == UINT32_C(0xCBF43926));
    uint32_t split = pmc_slave_update_crc(0, (const uint8_t *)"1234", 4);
    assert(pmc_slave_update_crc(split, (const uint8_t *)"56789", 5) == UINT32_C(0xCBF43926));
    for(size_t index = 0; index < sizeof(image); index++)
        image[index] = (uint8_t)index;
    memcpy(image, "PMC-v2", 6);
    put_word(image + 512, UINT32_C(0x20020000));
    put_word(image + 516, PMC_SLAVE_UPDATE_BASE + 601);

    pmc_slave_flash_t flash = {.erase = erase_flash, .write = write_flash, .read = read_flash};
    pmc_slave_update_server_t server;
    pmc_slave_update_server_init(&server, &flash);
    pmc_slave_packet_t request = {.command = PMC_SLAVE_UPDATE_INFO}, reply;
    assert(pmc_slave_update_server_handle(&server, &request, &reply));
    assert(reply.word_count == 7 && reply.words[0] == 2 && reply.words[1] == 1);
    request = (pmc_slave_packet_t){.command = PMC_SLAVE_UPDATE_BEGIN, .word_count = 3,
                                  .words = {12, sizeof(image)}};
    request.words[2] = (int32_t)pmc_slave_update_crc(0, image, sizeof(image));
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Ok);
    assert(erases == 1 && writes == 0);
    request = make_block(12, 512, 512);
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Offset);
    request = make_block(12, 0, 512);
    request.words[2]++;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Checksum);
    request = make_block(12, 0, 512);
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Ok);
    assert(server.received == 512 && writes == 0 && flash_bytes[0] == 0xFF);
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && server.received == 512);
    request = make_block(12, 512, 512);
    fail_write = true;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Flash);
    assert(server.received == 512);
    fail_write = false;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Ok);
    request = (pmc_slave_packet_t){.command = PMC_SLAVE_UPDATE_COMMIT, .word_count = 1, .words = {12}};
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_State);
    request = make_block(12, 1024, 512);
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Ok);
    request = (pmc_slave_packet_t){.command = PMC_SLAVE_UPDATE_COMMIT, .word_count = 1, .words = {12}};
    flash_bytes[700] ^= 1;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Checksum);
    assert(flash_bytes[0] == 0xFF && !server.committed);
    flash_bytes[700] ^= 1;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Ok);
    assert(server.committed && memcmp(flash_bytes, image, sizeof(image)) == 0);
    unsigned old_writes = writes;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && writes == old_writes);
    request.command = PMC_SLAVE_UPDATE_REBOOT;
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && server.reboot_requested);
    request = (pmc_slave_packet_t){.command = PMC_SLAVE_UPDATE_ABORT};
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && !server.active && !server.committed);
    request = (pmc_slave_packet_t){.command = PMC_SLAVE_UPDATE_BEGIN, .word_count = 3, .words = {1, 512, 0}};
    assert(pmc_slave_update_server_handle(&server, &request, &reply) && reply.words[0] == PmcUpdate_Image);
    assert(erases == 1);
    puts("PMC slave update server tests passed");
    return 0;
}