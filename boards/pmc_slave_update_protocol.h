#pragma once

#include "pmc_slave_protocol.h"

#define PMC_SLAVE_UPDATE_API 2U
#define PMC_SLAVE_UPDATE_BLOCK_SIZE 512U
#define PMC_SLAVE_UPDATE_INFO 37U
#define PMC_SLAVE_UPDATE_BEGIN 38U
#define PMC_SLAVE_UPDATE_BLOCK 39U
#define PMC_SLAVE_UPDATE_COMMIT 40U
#define PMC_SLAVE_UPDATE_ABORT 41U
#define PMC_SLAVE_UPDATE_REBOOT 42U
#define PMC_SLAVE_UPDATE_APPLICATION 0U
#define PMC_SLAVE_UPDATE_BOOTLOADER 1U
#define PMC_SLAVE_UPDATE_BASE UINT32_C(0x08010000)
#define PMC_SLAVE_UPDATE_END UINT32_C(0x08080000)
#define PMC_SLAVE_UPDATE_VECTOR_OFFSET 512U
#define PMC_SLAVE_UPDATE_INFO_WORDS 7U

typedef enum {
    PmcUpdate_Ok = 0,
    PmcUpdate_Invalid = 1,
    PmcUpdate_State = 2,
    PmcUpdate_Offset = 3,
    PmcUpdate_Checksum = 4,
    PmcUpdate_Flash = 5,
    PmcUpdate_Image = 6,
    PmcUpdate_Unsupported = 7
} pmc_slave_update_result_t;

uint32_t pmc_slave_update_crc(uint32_t crc, const uint8_t *bytes, size_t size);
bool pmc_slave_update_request_valid(const pmc_slave_packet_t *packet);