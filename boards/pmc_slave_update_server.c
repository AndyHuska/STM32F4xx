#include "pmc_slave_update_server.h"

#include <string.h>

static uint32_t word(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

void pmc_slave_update_server_init(pmc_slave_update_server_t *server, const pmc_slave_flash_t *flash)
{
    memset(server, 0, sizeof(*server));
    if(flash)
        server->flash = *flash;
}

static pmc_slave_update_result_t begin(pmc_slave_update_server_t *server, const pmc_slave_packet_t *request)
{
    uint32_t session = (uint32_t)request->words[0], size = (uint32_t)request->words[1];
    if(server->active || server->committed)
        return PmcUpdate_State;
    if(!session || size < PMC_SLAVE_UPDATE_VECTOR_OFFSET + 8 || size % 4U ||
       size > PMC_SLAVE_UPDATE_END - PMC_SLAVE_UPDATE_BASE)
        return PmcUpdate_Image;
    if(!server->flash.erase || !server->flash.write || !server->flash.read ||
       !server->flash.erase(server->flash.context))
        return PmcUpdate_Flash;
    server->session = session;
    server->size = size;
    server->expected_crc = (uint32_t)request->words[2];
    server->received = 0;
    server->last_size = 0;
    server->reboot_requested = false;
    server->active = true;
    return PmcUpdate_Ok;
}

static pmc_slave_update_result_t block(pmc_slave_update_server_t *server, const pmc_slave_packet_t *request)
{
    uint32_t session = (uint32_t)request->words[0], offset = (uint32_t)request->words[1];
    uint32_t crc = (uint32_t)request->words[2], size = (request->word_count - 3U) * 4U;
    if(!server->active || session != server->session)
        return PmcUpdate_State;
    if(offset > server->size || size > server->size - offset)
        return PmcUpdate_Offset;
    for(uint32_t index = 0; index < size; index++)
        server->block[index] = (uint8_t)((uint32_t)request->words[3U + index / 4U] >> (8U * (index % 4U)));
    if(pmc_slave_update_crc(0, server->block, size) != crc)
        return PmcUpdate_Checksum;
    if(server->last_size && offset == server->last_offset && size == server->last_size && crc == server->last_crc)
        return PmcUpdate_Ok;
    if(offset != server->received || offset % PMC_SLAVE_UPDATE_BLOCK_SIZE ||
       size != (server->size - offset > PMC_SLAVE_UPDATE_BLOCK_SIZE ? PMC_SLAVE_UPDATE_BLOCK_SIZE : server->size - offset))
        return PmcUpdate_Offset;
    if(offset == 0)
        memcpy(server->first_block, server->block, size);
    else if(!server->flash.write(server->flash.context, offset, server->block, size))
        return PmcUpdate_Flash;
    server->received += size;
    server->last_offset = offset;
    server->last_size = size;
    server->last_crc = crc;
    return PmcUpdate_Ok;
}

static pmc_slave_update_result_t commit(pmc_slave_update_server_t *server, const pmc_slave_packet_t *request)
{
    if((uint32_t)request->words[0] != server->session || !server->session)
        return PmcUpdate_State;
    if(server->committed)
        return PmcUpdate_Ok;
    if(!server->active || server->received != server->size)
        return PmcUpdate_State;
    if(!server->flash.read(server->flash.context, PMC_SLAVE_UPDATE_VECTOR_OFFSET, server->block, 8))
        return PmcUpdate_Flash;
    uint32_t stack = word(server->block), reset = word(server->block + 4);
    if(stack <= UINT32_C(0x20000000) || stack > UINT32_C(0x20020000) || (stack & 7U) ||
       !(reset & 1U) || (reset & ~1U) < PMC_SLAVE_UPDATE_BASE + PMC_SLAVE_UPDATE_VECTOR_OFFSET ||
       (reset & ~1U) >= PMC_SLAVE_UPDATE_BASE + server->size ||
       word(server->first_block) == 0 || word(server->first_block) == UINT32_MAX)
        return PmcUpdate_Image;

    uint32_t crc = pmc_slave_update_crc(0, server->first_block, PMC_SLAVE_UPDATE_BLOCK_SIZE);
    for(uint32_t offset = PMC_SLAVE_UPDATE_BLOCK_SIZE; offset < server->size; offset += PMC_SLAVE_UPDATE_BLOCK_SIZE) {
        uint32_t size = server->size - offset;
        if(size > PMC_SLAVE_UPDATE_BLOCK_SIZE)
            size = PMC_SLAVE_UPDATE_BLOCK_SIZE;
        if(!server->flash.read(server->flash.context, offset, server->block, size))
            return PmcUpdate_Flash;
        crc = pmc_slave_update_crc(crc, server->block, size);
    }
    if(crc != server->expected_crc)
        return PmcUpdate_Checksum;
    if(!server->flash.write(server->flash.context, 0, server->first_block, PMC_SLAVE_UPDATE_BLOCK_SIZE) ||
       !server->flash.read(server->flash.context, 0, server->block, PMC_SLAVE_UPDATE_BLOCK_SIZE) ||
       memcmp(server->first_block, server->block, PMC_SLAVE_UPDATE_BLOCK_SIZE))
        return PmcUpdate_Flash;
    server->active = false;
    server->committed = true;
    return PmcUpdate_Ok;
}

bool pmc_slave_update_server_handle(pmc_slave_update_server_t *server,
                                    const pmc_slave_packet_t *request, pmc_slave_packet_t *reply)
{
    if(!server || !request || !reply || request->command < PMC_SLAVE_UPDATE_INFO ||
       request->command > PMC_SLAVE_UPDATE_REBOOT)
        return false;
    memset(reply, 0, sizeof(*reply));
    reply->sequence = request->sequence;
    reply->command = request->command;
    reply->word_count = 3;
    reply->words[1] = (int32_t)server->session;
    reply->words[2] = (int32_t)server->received;
    pmc_slave_update_result_t result = PmcUpdate_Invalid;
    if(pmc_slave_update_request_valid(request)) switch(request->command) {
        case PMC_SLAVE_UPDATE_INFO:
            reply->word_count = PMC_SLAVE_UPDATE_INFO_WORDS;
            reply->words[0] = PMC_SLAVE_UPDATE_API;
            reply->words[1] = PMC_SLAVE_UPDATE_BOOTLOADER;
            reply->words[2] = PMC_SLAVE_UPDATE_BASE;
            reply->words[3] = PMC_SLAVE_UPDATE_END;
            reply->words[4] = PMC_SLAVE_UPDATE_BLOCK_SIZE;
            reply->words[5] = (int32_t)server->session;
            reply->words[6] = 0;
            return true;
        case PMC_SLAVE_UPDATE_BEGIN:
            result = begin(server, request);
            break;
        case PMC_SLAVE_UPDATE_BLOCK:
            result = block(server, request);
            break;
        case PMC_SLAVE_UPDATE_COMMIT:
            result = commit(server, request);
            break;
        case PMC_SLAVE_UPDATE_ABORT:
            server->active = server->committed = server->reboot_requested = false;
            result = PmcUpdate_Ok;
            break;
        case PMC_SLAVE_UPDATE_REBOOT:
            if(server->committed && (uint32_t)request->words[0] == server->session) {
                server->reboot_requested = true;
                result = PmcUpdate_Ok;
            } else
                result = PmcUpdate_State;
            break;
    }
    reply->words[0] = result;
    reply->words[1] = (int32_t)server->session;
    reply->words[2] = (int32_t)server->received;
    return true;
}