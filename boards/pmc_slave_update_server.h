#pragma once

#include "pmc_slave_update_protocol.h"

typedef struct {
    void *context;
    bool (*erase)(void *context);
    bool (*write)(void *context, uint32_t offset, const uint8_t *bytes, size_t size);
    bool (*read)(void *context, uint32_t offset, uint8_t *bytes, size_t size);
} pmc_slave_flash_t;

typedef struct {
    pmc_slave_flash_t flash;
    uint8_t first_block[PMC_SLAVE_UPDATE_BLOCK_SIZE];
    uint8_t block[PMC_SLAVE_UPDATE_BLOCK_SIZE];
    uint32_t session;
    uint32_t size;
    uint32_t expected_crc;
    uint32_t received;
    uint32_t last_offset;
    uint32_t last_size;
    uint32_t last_crc;
    bool active;
    bool committed;
    bool reboot_requested;
} pmc_slave_update_server_t;

void pmc_slave_update_server_init(pmc_slave_update_server_t *server, const pmc_slave_flash_t *flash);
bool pmc_slave_update_server_handle(pmc_slave_update_server_t *server,
                                    const pmc_slave_packet_t *request, pmc_slave_packet_t *reply);