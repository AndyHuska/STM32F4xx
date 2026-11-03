#pragma once

#include "pmc_slave_protocol.h"

#define PMC_SLAVE_AXES_MASK UINT32_C(0xC0)

typedef enum {
    PmcSlaveMotion_Local,
    PmcSlaveMotion_Remote,
    PmcSlaveMotion_NoMotion,
    PmcSlaveMotion_Unsupported,
    PmcSlaveMotion_Invalid
} pmc_slave_motion_result_t;

typedef struct {
    float steps_per_unit;
    float max_rate_per_minute;
    float acceleration_per_minute_squared;
    float target;
    float start;
    float feed_per_minute;
    uint32_t axes;
    uint8_t axis;
    bool rapid;
    bool motion_supported;
    bool auxiliary_commands;
} pmc_slave_motion_t;

pmc_slave_motion_result_t pmc_slave_motion_packet(const pmc_slave_motion_t *motion, pmc_slave_packet_t *packet);