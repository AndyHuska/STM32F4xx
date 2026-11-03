#include "pmc_slave_protocol.h"

#include <string.h>

static uint32_t read_word(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_word(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int32_t read_signed_word(const uint8_t *bytes)
{
    uint32_t value = read_word(bytes);
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

int8_t pmc_slave_axis_id(uint8_t main_axis)
{
    return main_axis == 6 ? PMC_SLAVE_AXIS_D : main_axis == 7 ? PMC_SLAVE_AXIS_E : -1;
}

int8_t pmc_slave_relay_id(uint8_t main_relay)
{
    return main_relay >= 4 && main_relay < 8 ? (int8_t)(main_relay - 4) : -1;
}

bool pmc_slave_request_valid(const pmc_slave_packet_t *packet)
{
    if(!packet || (packet->command & ~(UINT32_C(0xFFFF) | PMC_SLAVE_SYNC_FLAG)))
        return false;

    uint32_t command = packet->command & UINT32_C(0xFFFF);
    uint16_t words;
    bool has_axis = false;

    if((packet->command & PMC_SLAVE_SYNC_FLAG) &&
       command != PmcSlave_MoveIndependent && command != PmcSlave_Home)
        return false;

    switch(command) {
        case PmcSlave_Echo:
            return packet->word_count <= PMC_SLAVE_MAX_WORDS;
        case PmcSlave_Disconnect:
        case PmcSlave_Heartbeat:
        case PmcSlave_Reset:
        case PmcSlave_ReadInputs:
        case PmcSlave_Home:
        case PmcSlave_Version:
            words = 0;
            break;
        case PmcSlave_EnableStatus:
            return packet->word_count <= 1;
        case PmcSlave_SoftLimitsToggle:
            words = 1;
            break;
        case PmcSlave_Stop:
            words = 1;
            break;
        case PmcSlave_JogScale:
        case PmcSlave_JogVelocity:
            words = 2;
            has_axis = true;
            break;
        case PmcSlave_GeneralOutput:
            words = 3;
            break;
        case PmcSlave_InputPolarity:
        case PmcSlave_OutputPolarity:
            words = 3;
            has_axis = true;
            break;
        case PmcSlave_Relay:
            words = 4;
            break;
        case PmcSlave_MoveIndependent:
        case PmcSlave_SoftLimits:
        case PmcSlave_KinematicSetup:
            words = 4;
            has_axis = true;
            break;
        case PmcSlave_HomingSetup:
            words = 5;
            has_axis = true;
            break;
        case PmcSlave_RelayPulseHold:
            words = 6;
            break;
        default:
            return false;
    }

    if(packet->word_count != words)
        return false;

    if(has_axis && packet->words[0] != PMC_SLAVE_AXIS_D && packet->words[0] != PMC_SLAVE_AXIS_E)
        return false;

    if(command == PmcSlave_Relay || command == PmcSlave_RelayPulseHold) {
        if(packet->words[0] < 0 || packet->words[0] >= PMC_SLAVE_RELAY_COUNT)
            return false;
        if(command == PmcSlave_RelayPulseHold &&
           (packet->words[1] < 0 || packet->words[1] >= PMC_SLAVE_RELAY_COUNT))
            return false;
    }

    return true;
}

size_t pmc_slave_encode(const pmc_slave_packet_t *packet, uint8_t *bytes, size_t capacity)
{
    if(!packet || !bytes || packet->word_count > PMC_SLAVE_MAX_WORDS)
        return 0;

    size_t size = PMC_SLAVE_HEADER_SIZE + 4U * packet->word_count;
    if(capacity < size)
        return 0;

    write_word(bytes, PMC_SLAVE_PACKET_ID);
    write_word(bytes + 4, packet->sequence);
    write_word(bytes + 8, packet->command);
    write_word(bytes + 12, 4U * packet->word_count);

    for(uint16_t index = 0; index < packet->word_count; index++)
        write_word(bytes + PMC_SLAVE_HEADER_SIZE + 4U * index, (uint32_t)packet->words[index]);

    return size;
}

bool pmc_slave_decode(const uint8_t *bytes, size_t size, pmc_slave_packet_t *packet)
{
    if(!bytes || !packet || size < PMC_SLAVE_HEADER_SIZE || size > PMC_SLAVE_MAX_FRAME ||
       read_word(bytes) != PMC_SLAVE_PACKET_ID)
        return false;

    uint32_t payload = read_word(bytes + 12);
    if(payload > PMC_SLAVE_MAX_FRAME - PMC_SLAVE_HEADER_SIZE || payload % 4U ||
       payload != size - PMC_SLAVE_HEADER_SIZE)
        return false;

    packet->sequence = read_word(bytes + 4);
    packet->command = read_word(bytes + 8);
    packet->word_count = (uint16_t)(payload / 4U);

    for(uint16_t index = 0; index < packet->word_count; index++)
        packet->words[index] = read_signed_word(bytes + PMC_SLAVE_HEADER_SIZE + 4U * index);

    return true;
}

bool pmc_slave_decoder_push(pmc_slave_decoder_t *decoder, uint8_t byte, pmc_slave_packet_t *packet)
{
    if(!decoder || !packet)
        return false;

    if(decoder->used >= PMC_SLAVE_MAX_FRAME) {
        decoder->used = decoder->expected = 0;
        decoder->rejected_frames++;
    }

    if(decoder->used < 4 && byte != 0x55U) {
        decoder->used = 0;
        return false;
    }

    decoder->bytes[decoder->used++] = byte;

    if(decoder->used == PMC_SLAVE_HEADER_SIZE) {
        uint32_t payload = read_word(decoder->bytes + 12);
        if(payload > PMC_SLAVE_MAX_FRAME - PMC_SLAVE_HEADER_SIZE || payload % 4U) {
            uint16_t trailing = 0;
            while(trailing < 3 && decoder->bytes[PMC_SLAVE_HEADER_SIZE - 1 - trailing] == 0x55U)
                trailing++;
            decoder->used = trailing;
            decoder->expected = 0;
            decoder->rejected_frames++;
            return false;
        }
        decoder->expected = (uint16_t)(PMC_SLAVE_HEADER_SIZE + payload);
    }

    if(decoder->expected && decoder->used == decoder->expected) {
        bool valid = pmc_slave_decode(decoder->bytes, decoder->used, packet);
        decoder->used = decoder->expected = 0;
        if(!valid)
            decoder->rejected_frames++;
        return valid;
    }

    return false;
}

pmc_slave_reply_kind_t pmc_slave_reply_kind(const pmc_slave_packet_t *packet)
{
    if(!packet || (packet->command & ~(UINT32_C(0xFFFF) | PMC_SLAVE_SYNC_FLAG)))
        return PmcSlaveReply_Invalid;

    uint32_t command = packet->command & UINT32_C(0xFFFF);

    if(command == PmcSlave_Status)
        return packet->word_count == 8 ? PmcSlaveReply_Status : PmcSlaveReply_Invalid;
    if(command == PmcSlave_Version)
        return packet->word_count == 16 ? PmcSlaveReply_Version : PmcSlaveReply_Invalid;
    if(command == PmcSlave_Error)
        return PmcSlaveReply_Error;
    if(command == PmcSlave_Heartbeat || command == PmcSlave_Echo || command == PmcSlave_Disconnect)
        return packet->word_count == 0 ? PmcSlaveReply_Acknowledgement : PmcSlaveReply_Invalid;
    if(packet->word_count != 1)
        return PmcSlaveReply_Invalid;
    if((command == PmcSlave_MoveIndependent || command == PmcSlave_Home) &&
       (packet->command & PMC_SLAVE_SYNC_FLAG))
        return PmcSlaveReply_Completion;

    switch(command) {
        case PmcSlave_EnableStatus:
        case PmcSlave_Reset:
        case PmcSlave_ReadInputs:
        case PmcSlave_Home:
        case PmcSlave_Relay:
        case PmcSlave_SoftLimits:
        case PmcSlave_MoveIndependent:
        case PmcSlave_HomingSetup:
        case PmcSlave_KinematicSetup:
        case PmcSlave_InputPolarity:
        case PmcSlave_OutputPolarity:
        case PmcSlave_JogScale:
        case PmcSlave_SoftLimitsToggle:
        case PmcSlave_GeneralOutput:
        case PmcSlave_JogVelocity:
        case PmcSlave_Stop:
        case PmcSlave_RelayPulseHold:
            return PmcSlaveReply_Acknowledgement;
        default:
            return PmcSlaveReply_Invalid;
    }
}

bool pmc_slave_decode_status(const pmc_slave_packet_t *packet, pmc_slave_status_t *status)
{
    if(!status || pmc_slave_reply_kind(packet) != PmcSlaveReply_Status)
        return false;

    status->system_ok = packet->words[0];
    status->position[0] = packet->words[1];
    status->position[1] = packet->words[2];
    status->reserved_position = packet->words[3];
    status->jogging = (uint32_t)packet->words[4];
    status->motion_inputs = (uint32_t)packet->words[5];
    status->velocity[0] = packet->words[6];
    status->velocity[1] = packet->words[7];
    status->sequence = packet->sequence;
    return true;
}