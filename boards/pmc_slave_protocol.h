#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PMC_SLAVE_PACKET_ID UINT32_C(0x55555555)
#define PMC_SLAVE_SYNC_FLAG UINT32_C(0x00010000)
#define PMC_SLAVE_HEADER_SIZE 16U
#define PMC_SLAVE_MAX_FRAME 540U
#define PMC_SLAVE_MAX_WORDS ((PMC_SLAVE_MAX_FRAME - PMC_SLAVE_HEADER_SIZE) / 4U)
#define PMC_SLAVE_AXIS_D 3
#define PMC_SLAVE_AXIS_E 4
#define PMC_SLAVE_RELAY_COUNT 4

typedef enum {
    PmcSlave_Error = 0,
    PmcSlave_Disconnect = 1,
    PmcSlave_Echo = 2,
    PmcSlave_Heartbeat = 3,
    PmcSlave_EnableStatus = 5,
    PmcSlave_Reset = 6,
    PmcSlave_ReadInputs = 7,
    PmcSlave_Home = 10,
    PmcSlave_Relay = 11,
    PmcSlave_SoftLimits = 12,
    PmcSlave_MoveIndependent = 13,
    PmcSlave_HomingSetup = 14,
    PmcSlave_KinematicSetup = 15,
    PmcSlave_InputPolarity = 17,
    PmcSlave_OutputPolarity = 18,
    PmcSlave_JogScale = 19,
    PmcSlave_SoftLimitsToggle = 20,
    PmcSlave_GeneralOutput = 24,
    PmcSlave_Status = 25,
    PmcSlave_JogVelocity = 27,
    PmcSlave_Stop = 28,
    PmcSlave_Version = 33,
    PmcSlave_RelayPulseHold = 36
} pmc_slave_command_t;

typedef struct {
    uint32_t sequence;
    uint32_t command;
    uint16_t word_count;
    int32_t words[PMC_SLAVE_MAX_WORDS];
} pmc_slave_packet_t;

typedef struct {
    uint8_t bytes[PMC_SLAVE_MAX_FRAME];
    uint16_t used;
    uint16_t expected;
    uint32_t rejected_frames;
} pmc_slave_decoder_t;

typedef struct {
    int32_t system_ok;
    int32_t position[2];
    int32_t reserved_position;
    uint32_t jogging;
    uint32_t motion_inputs;
    int32_t velocity[2];
    uint32_t sequence;
} pmc_slave_status_t;

typedef enum {
    PmcSlaveReply_Invalid,
    PmcSlaveReply_Acknowledgement,
    PmcSlaveReply_Completion,
    PmcSlaveReply_Status,
    PmcSlaveReply_Version,
    PmcSlaveReply_Error
} pmc_slave_reply_kind_t;

bool pmc_slave_request_valid(const pmc_slave_packet_t *packet);
int8_t pmc_slave_axis_id(uint8_t main_axis);
int8_t pmc_slave_relay_id(uint8_t main_relay);
size_t pmc_slave_encode(const pmc_slave_packet_t *packet, uint8_t *bytes, size_t capacity);
bool pmc_slave_decode(const uint8_t *bytes, size_t size, pmc_slave_packet_t *packet);
bool pmc_slave_decoder_push(pmc_slave_decoder_t *decoder, uint8_t byte, pmc_slave_packet_t *packet);
pmc_slave_reply_kind_t pmc_slave_reply_kind(const pmc_slave_packet_t *packet);
bool pmc_slave_decode_status(const pmc_slave_packet_t *packet, pmc_slave_status_t *status);