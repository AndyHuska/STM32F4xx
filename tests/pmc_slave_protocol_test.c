#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../boards/pmc_slave_protocol.h"
#include "../boards/pmc_slave_bridge.h"
#include "../boards/pmc_slave_motion.h"

typedef struct {
    bool connected;
    bool busy;
    unsigned sends;
    unsigned events[7];
    pmc_slave_packet_t sent;
} test_transport_t;

static bool test_send(void *context, const uint8_t *bytes, size_t size)
{
    test_transport_t *transport = context;
    if(transport->busy)
        return false;
    assert(pmc_slave_decode(bytes, size, &transport->sent));
    transport->sends++;
    return true;
}

static bool test_connected(void *context)
{
    return ((test_transport_t *)context)->connected;
}

static void test_event(void *context, pmc_slave_event_t event, uint32_t sequence,
                       const pmc_slave_packet_t *reply)
{
    test_transport_t *transport = context;
    (void)sequence;
    (void)reply;
    transport->events[event]++;
}

static void deliver(pmc_slave_bridge_t *bridge, const pmc_slave_packet_t *packet, uint32_t now)
{
    uint8_t bytes[PMC_SLAVE_MAX_FRAME];
    size_t size = pmc_slave_encode(packet, bytes, sizeof(bytes));
    assert(size);
    pmc_slave_bridge_receive(bridge, bytes, size, now);
}

static void test_request_scope(void)
{
    assert(pmc_slave_axis_id(6) == 3 && pmc_slave_axis_id(7) == 4);
    assert(pmc_slave_axis_id(0) == -1 && pmc_slave_axis_id(8) == -1);
    assert(pmc_slave_relay_id(4) == 0 && pmc_slave_relay_id(7) == 3);
    assert(pmc_slave_relay_id(3) == -1 && pmc_slave_relay_id(8) == -1);
    pmc_slave_packet_t packet = {
        .sequence = 7,
        .command = PmcSlave_MoveIndependent,
        .word_count = 4,
        .words = {PMC_SLAVE_AXIS_D, -25, 100, 200}
    };
    assert(pmc_slave_request_valid(&packet));
    packet.words[0] = PMC_SLAVE_AXIS_E;
    assert(pmc_slave_request_valid(&packet));
    for(int32_t axis = 0; axis < 8; axis++) {
        packet.words[0] = axis;
        assert(pmc_slave_request_valid(&packet) == (axis == 3 || axis == 4));
    }
    packet.words[0] = PMC_SLAVE_AXIS_D;
    packet.command |= PMC_SLAVE_SYNC_FLAG;
    assert(pmc_slave_request_valid(&packet));
    packet.word_count = 3;
    assert(!pmc_slave_request_valid(&packet));

    const uint32_t excluded[] = {4, 8, 9, 16, 21, 22, 26, 29, 30, 31, 32, 34, 35, 999};
    packet.word_count = 4;
    for(size_t index = 0; index < sizeof(excluded) / sizeof(excluded[0]); index++) {
        packet.command = excluded[index];
        assert(!pmc_slave_request_valid(&packet));
    }
    packet.command = PmcSlave_Relay;
    packet.words[0] = 3;
    assert(pmc_slave_request_valid(&packet));
    packet.words[0] = 4;
    assert(!pmc_slave_request_valid(&packet));
    packet.words[0] = -1;
    assert(!pmc_slave_request_valid(&packet));
    packet.command = PmcSlave_Heartbeat | UINT32_C(0x80000000);
    packet.word_count = 0;
    assert(!pmc_slave_request_valid(&packet));
    assert(!pmc_slave_request_valid(NULL));
    packet = (pmc_slave_packet_t){.command = PmcSlave_Stop, .word_count = 1, .words = {-1}};
    assert(pmc_slave_request_valid(&packet));
}

static void test_encoding_and_framing(void)
{
    pmc_slave_packet_t packet = {
        .sequence = UINT32_C(0x12345678),
        .command = PmcSlave_MoveIndependent | PMC_SLAVE_SYNC_FLAG,
        .word_count = 4,
        .words = {PMC_SLAVE_AXIS_D, -25, INT32_MIN, INT32_MAX}
    }, decoded = {0};
    uint8_t bytes[PMC_SLAVE_MAX_FRAME] = {0};
    size_t size = pmc_slave_encode(&packet, bytes, sizeof(bytes));
    assert(size == 32);
    assert(bytes[0] == 0x55 && bytes[3] == 0x55);
    assert(bytes[4] == 0x78 && bytes[7] == 0x12);
    assert(bytes[8] == 13 && bytes[10] == 1);
    assert(bytes[12] == 16 && bytes[13] == 0);
    assert(bytes[20] == 0xE7 && bytes[23] == 0xFF);
    assert(pmc_slave_decode(bytes, size, &decoded));
    assert(decoded.sequence == packet.sequence && decoded.command == packet.command);
    assert(decoded.word_count == 4);
    assert(memcmp(decoded.words, packet.words, 16) == 0);
    assert(!pmc_slave_decode(bytes, size - 1, &decoded));
    assert(!pmc_slave_decode(bytes, 15, &decoded));
    assert(pmc_slave_encode(&packet, bytes, 31) == 0);
    assert(!pmc_slave_decode(NULL, size, &decoded));
    bytes[12] = 3;
    assert(!pmc_slave_decode(bytes, size, &decoded));
    memset(bytes + 12, 0xFF, 4);
    assert(!pmc_slave_decode(bytes, size, &decoded));

    size = pmc_slave_encode(&packet, bytes, sizeof(bytes));
    pmc_slave_decoder_t decoder = {0};
    assert(!pmc_slave_decoder_push(&decoder, 0x42, &decoded));
    assert(!pmc_slave_decoder_push(&decoder, 0x55, &decoded));
    assert(!pmc_slave_decoder_push(&decoder, 0x11, &decoded));
    for(size_t index = 0; index < size; index++)
        assert(pmc_slave_decoder_push(&decoder, bytes[index], &decoded) == (index + 1 == size));
    assert(decoded.sequence == packet.sequence);
    for(size_t index = 0; index < size; index++)
        assert(pmc_slave_decoder_push(&decoder, bytes[index], &decoded) == (index + 1 == size));

    memset(bytes + 12, 0xFF, 4);
    for(size_t index = 0; index < 16; index++)
        assert(!pmc_slave_decoder_push(&decoder, bytes[index], &decoded));
    assert(decoder.rejected_frames == 1);
    size = pmc_slave_encode(&packet, bytes, sizeof(bytes));
    for(size_t index = 0; index < size; index++)
        assert(pmc_slave_decoder_push(&decoder, bytes[index], &decoded) == (index + 1 == size));

    packet.command = PmcSlave_Echo;
    packet.word_count = PMC_SLAVE_MAX_WORDS;
    size = pmc_slave_encode(&packet, bytes, sizeof(bytes));
    assert(size == PMC_SLAVE_MAX_FRAME);
    assert(pmc_slave_decode(bytes, size, &decoded));
    packet.word_count++;
    assert(pmc_slave_encode(&packet, bytes, sizeof(bytes)) == 0);
}

static void test_reply_semantics(void)
{
    pmc_slave_packet_t packet = {.command = PmcSlave_MoveIndependent, .word_count = 1, .words = {1}};
    assert(pmc_slave_reply_kind(&packet) == PmcSlaveReply_Acknowledgement);
    packet.command |= PMC_SLAVE_SYNC_FLAG;
    assert(pmc_slave_reply_kind(&packet) == PmcSlaveReply_Completion);
    packet.command = PmcSlave_Stop | PMC_SLAVE_SYNC_FLAG;
    assert(pmc_slave_reply_kind(&packet) == PmcSlaveReply_Acknowledgement);
    packet.command = 31;
    assert(pmc_slave_reply_kind(&packet) == PmcSlaveReply_Invalid);
    packet.command = PmcSlave_Version;
    packet.word_count = 16;
    assert(pmc_slave_reply_kind(&packet) == PmcSlaveReply_Version);
    packet.word_count = 15;
    assert(pmc_slave_reply_kind(&packet) == PmcSlaveReply_Invalid);

    packet = (pmc_slave_packet_t){
        .sequence = 42, .command = PmcSlave_Status, .word_count = 8,
        .words = {1, -100, 200, 0, 3, 0x1234, -40, 50}
    };
    pmc_slave_status_t status;
    assert(pmc_slave_decode_status(&packet, &status));
    assert(status.system_ok == 1 && status.sequence == 42);
    assert(status.position[0] == -100 && status.position[1] == 200);
    assert(status.velocity[0] == -40 && status.velocity[1] == 50);
    assert(status.jogging == 3 && status.motion_inputs == 0x1234);
    packet.word_count = 7;
    assert(!pmc_slave_decode_status(&packet, &status));
}

static void test_bridge(void)
{
    test_transport_t test = {.connected = true};
    pmc_slave_transport_t transport = {.context = &test, .send = test_send, .connected = test_connected};
    pmc_slave_bridge_t bridge;
    pmc_slave_bridge_init(&bridge, &transport, test_event, &test);
    pmc_slave_packet_t request = {
        .command = PmcSlave_MoveIndependent | PMC_SLAVE_SYNC_FLAG,
        .word_count = 4, .words = {3, 100, 20, 30}
    };
    uint32_t sequence;
    assert(!pmc_slave_bridge_submit(&bridge, &request, 0, 1000, &sequence));
    pmc_slave_packet_t heartbeat = {.command = PmcSlave_Heartbeat, .sequence = 999};
    deliver(&bridge, &heartbeat, 0);
    assert(bridge.online && test.events[PmcSlaveEvent_Online] == 1);
    assert(pmc_slave_bridge_submit(&bridge, &request, 0, 1000, &sequence));
    test.busy = true;
    pmc_slave_bridge_poll(&bridge, 1);
    assert(!bridge.active_valid && bridge.queue_count == 1 && test.sends == 0);
    test.busy = false;
    pmc_slave_bridge_poll(&bridge, 2);
    assert(bridge.active_valid && bridge.queue_count == 0 && test.sent.sequence == sequence);
    pmc_slave_packet_t reply = {.command = PmcSlave_MoveIndependent, .sequence = sequence, .word_count = 1, .words = {1}};
    deliver(&bridge, &reply, 3);
    assert(bridge.active_valid);
    reply.sequence++;
    deliver(&bridge, &reply, 4);
    assert(bridge.active_valid && bridge.unmatched_replies == 1);
    reply.sequence = sequence;
    reply.command |= PMC_SLAVE_SYNC_FLAG;
    deliver(&bridge, &reply, 5);
    assert(!bridge.active_valid);

    pmc_slave_packet_t status = {.command = PmcSlave_Status, .word_count = 8, .words = {1, -5, 6, 0, 0, 4, -7, 8}};
    deliver(&bridge, &status, 10);
    pmc_slave_status_t snapshot;
    assert(pmc_slave_bridge_get_status(&bridge, 20, 10, &snapshot));
    assert(snapshot.position[0] == -5 && snapshot.velocity[1] == 8);
    assert(!pmc_slave_bridge_get_status(&bridge, 21, 10, &snapshot));

    request = (pmc_slave_packet_t){.command = PmcSlave_Version};
    assert(pmc_slave_bridge_submit(&bridge, &request, 20, 100, &sequence));
    pmc_slave_bridge_poll(&bridge, 20);
    reply = (pmc_slave_packet_t){.command = PmcSlave_Version, .sequence = sequence, .word_count = 16, .words = {0x2D434D50}};
    deliver(&bridge, &reply, 21);
    assert(bridge.version_valid && !bridge.active_valid && strncmp(bridge.version, "PMC-", 4) == 0);

    request = (pmc_slave_packet_t){.command = PmcSlave_Echo};
    for(unsigned index = 0; index < PMC_SLAVE_QUEUE_SIZE; index++)
        assert(pmc_slave_bridge_submit(&bridge, &request, 30, 100, NULL));
    assert(!pmc_slave_bridge_submit(&bridge, &request, 30, 100, NULL));
    pmc_slave_bridge_poll(&bridge, 30);
    assert(bridge.active_valid && bridge.queue_count == PMC_SLAVE_QUEUE_SIZE - 1);
    unsigned sends = test.sends;
    pmc_slave_bridge_poll(&bridge, 130);
    assert(!bridge.active_valid && !bridge.online && bridge.queue_count == 0);
    assert(test.sends == sends && test.events[PmcSlaveEvent_Timeout] == 1);
    assert(test.events[PmcSlaveEvent_LinkLost] == PMC_SLAVE_QUEUE_SIZE - 1);

    deliver(&bridge, &heartbeat, UINT32_MAX - 6);
    assert(pmc_slave_bridge_submit(&bridge, &request, UINT32_MAX - 5, 10, NULL));
    pmc_slave_bridge_poll(&bridge, 0);
    assert(bridge.active_valid);
    pmc_slave_bridge_poll(&bridge, 4);
    assert(!bridge.active_valid && test.events[PmcSlaveEvent_Timeout] == 2);

    deliver(&bridge, &heartbeat, 3000);
    bridge.last_tx = 3000;
    pmc_slave_bridge_poll(&bridge, 4000);
    assert(test.sent.command == PmcSlave_Heartbeat);
    pmc_slave_bridge_poll(&bridge, 5000);
    assert(!bridge.online);
    assert(!pmc_slave_bridge_get_status(&bridge, 5000, 10000, &snapshot));
    assert(pmc_slave_bridge_submit(&bridge, &request, 5001, 100, NULL));
    test.connected = false;
    pmc_slave_bridge_poll(&bridge, 5002);
    assert(bridge.queue_count == 0);
    test.connected = true;
    request = (pmc_slave_packet_t){.command = PmcSlave_Stop, .word_count = 1};
    assert(pmc_slave_bridge_submit(&bridge, &request, 5003, 100, &sequence));
    pmc_slave_bridge_poll(&bridge, 5003);
    assert(bridge.active_valid && test.sent.command == PmcSlave_Stop);
    reply = (pmc_slave_packet_t){.command = PmcSlave_Stop | PMC_SLAVE_SYNC_FLAG,
                                .sequence = sequence, .word_count = 1, .words = {1}};
    deliver(&bridge, &reply, 5004);
    assert(!bridge.active_valid && bridge.online && !bridge.status_valid);
}

static void test_unsolicited_version(void)
{
    test_transport_t test = {.connected = true};
    pmc_slave_transport_t transport = {.context = &test, .send = test_send, .connected = test_connected};
    pmc_slave_bridge_t bridge;
    pmc_slave_bridge_init(&bridge, &transport, test_event, &test);
    pmc_slave_packet_t version = {
        .command = PmcSlave_Version, .sequence = 2000000, .word_count = 16,
        .words = {0x2D434D50}
    };
    deliver(&bridge, &version, 10);
    assert(bridge.version_valid && strncmp(bridge.version, "PMC-", 4) == 0);
    assert(!bridge.online && !bridge.active_valid && !bridge.status_valid);
    assert(test.events[PmcSlaveEvent_Version] == 1 && bridge.unmatched_replies == 0);

    pmc_slave_packet_t request = {.command = PmcSlave_Version};
    uint32_t sequence;
    assert(pmc_slave_bridge_submit(&bridge, &request, 11, 100, &sequence));
    pmc_slave_bridge_poll(&bridge, 11);
    assert(bridge.active_valid);
    deliver(&bridge, &version, 12);
    assert(bridge.active_valid && bridge.active.packet.sequence == sequence);
    version.sequence = sequence;
    deliver(&bridge, &version, 13);
    assert(!bridge.active_valid && !bridge.online && bridge.version_valid);

    pmc_slave_packet_t heartbeat = {.command = PmcSlave_Heartbeat};
    deliver(&bridge, &heartbeat, 14);
    assert(bridge.online);
    request = (pmc_slave_packet_t){
        .command = PmcSlave_MoveIndependent | PMC_SLAVE_SYNC_FLAG,
        .word_count = 4, .words = {3, 100, 200, 300}
    };
    assert(pmc_slave_bridge_submit(&bridge, &request, 15, 100, &sequence));
    pmc_slave_bridge_poll(&bridge, 15);
    version.sequence = sequence;
    deliver(&bridge, &version, 16);
    assert(bridge.active_valid && bridge.active.packet.sequence == sequence);
    version.word_count = 15;
    deliver(&bridge, &version, 17);
    assert(bridge.active_valid && bridge.unmatched_replies == 1);

    version.word_count = 16;
    for(uint16_t index = 0; index < 16; index++)
        version.words[index] = 0x41414141;
    deliver(&bridge, &version, 18);
    assert(strlen(bridge.version) == 64 && bridge.version[64] == '\0');
}

static void test_motion_routing(void)
{
    pmc_slave_motion_t motion = {
        .steps_per_unit = 200.0f,
        .max_rate_per_minute = 1200.0f,
        .acceleration_per_minute_squared = 36000.0f,
        .target = 25.0f, .start = 0.0f,
        .feed_per_minute = 600.0f,
        .axes = UINT32_C(0x40), .axis = 6,
        .motion_supported = true
    };
    pmc_slave_packet_t packet = {0};
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Remote);
    assert(packet.command == (PmcSlave_MoveIndependent | PMC_SLAVE_SYNC_FLAG));
    assert(packet.words[0] == 3 && packet.words[1] == 5000);
    assert(packet.words[2] == 2000 && packet.words[3] == 2000);
    assert(pmc_slave_request_valid(&packet));
    motion.rapid = true;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Remote);
    assert(packet.words[2] == 4000);
    motion.rapid = false;
    motion.feed_per_minute = 2400.0f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Remote);
    assert(packet.words[2] == 4000);
    motion.axes = 0x80;
    motion.axis = 7;
    motion.target = -25.0f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Remote);
    assert(packet.words[0] == 4 && packet.words[1] == -5000);
    motion.axes = 1;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Local);
    motion.axes = 0x41;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Unsupported);
    motion.axes = 0xC0;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Unsupported);
    motion.axes = 0x80;
    motion.motion_supported = false;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Unsupported);
    motion.motion_supported = true;
    motion.auxiliary_commands = true;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Unsupported);
    motion.auxiliary_commands = false;
    motion.target = NAN;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Invalid);
    motion.target = 1.0e20f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Invalid);
    motion.target = 10.0f;
    motion.feed_per_minute = 0.0f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Invalid);
    motion.feed_per_minute = 120.0f;
    motion.steps_per_unit = 0.0f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_Invalid);
    motion.steps_per_unit = 200.0f;
    motion.target = motion.start = 25.0f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_NoMotion);
    motion.target = 25.0001f;
    assert(pmc_slave_motion_packet(&motion, &packet) == PmcSlaveMotion_NoMotion);
}

int main(void)
{
    test_request_scope();
    test_encoding_and_framing();
    test_reply_semantics();
    test_bridge();
    test_unsolicited_version();
    test_motion_routing();
    puts("PMC slave protocol, bridge and motion tests passed");
    return 0;
}