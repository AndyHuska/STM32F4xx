#include "pmc_slave_bridge.h"

#include <string.h>

static void emit(pmc_slave_bridge_t *bridge, pmc_slave_event_t event,
                 uint32_t sequence, const pmc_slave_packet_t *reply)
{
    if(bridge->on_event)
        bridge->on_event(bridge->event_context, event, sequence, reply);
}

static uint32_t next_sequence(pmc_slave_bridge_t *bridge)
{
    if(++bridge->next_sequence == 0)
        bridge->next_sequence = 1;
    return bridge->next_sequence;
}

static bool needs_online(uint32_t command)
{
    switch(command & UINT32_C(0xFFFF)) {
        case PmcSlave_Echo:
        case PmcSlave_Heartbeat:
        case PmcSlave_Version:
        case PmcSlave_ReadInputs:
        case PmcSlave_EnableStatus:
        case PmcSlave_Stop:
            return false;
        default:
            return true;
    }
}

void pmc_slave_bridge_init(pmc_slave_bridge_t *bridge, const pmc_slave_transport_t *transport,
                           pmc_slave_event_handler_t handler, void *context)
{
    memset(bridge, 0, sizeof(*bridge));
    if(transport)
        bridge->transport = *transport;
    bridge->on_event = handler;
    bridge->event_context = context;
    bridge->heartbeat_ms = 1000;
    bridge->link_timeout_ms = 2000;
}

bool pmc_slave_bridge_submit(pmc_slave_bridge_t *bridge, const pmc_slave_packet_t *request,
                             uint32_t now_ms, uint32_t timeout_ms, uint32_t *sequence)
{
    if(!bridge || !bridge->transport.send || !pmc_slave_request_valid(request) ||
       !timeout_ms || timeout_ms > INT32_MAX || bridge->queue_count >= PMC_SLAVE_QUEUE_SIZE ||
       (!bridge->online && needs_online(request->command)))
        return false;

    uint8_t slot = (uint8_t)((bridge->queue_head + bridge->queue_count) % PMC_SLAVE_QUEUE_SIZE);
    bridge->queue[slot].packet = *request;
    bridge->queue[slot].packet.sequence = next_sequence(bridge);
    bridge->queue[slot].deadline = now_ms + timeout_ms;
    bridge->queue_count++;
    if(sequence)
        *sequence = bridge->queue[slot].packet.sequence;
    return true;
}

void pmc_slave_bridge_disconnect(pmc_slave_bridge_t *bridge)
{
    uint32_t sequences[PMC_SLAVE_QUEUE_SIZE + 1];
    uint8_t count = 0;
    bool was_online = bridge->online;

    if(bridge->active_valid)
        sequences[count++] = bridge->active.packet.sequence;
    while(bridge->queue_count) {
        sequences[count++] = bridge->queue[bridge->queue_head].packet.sequence;
        bridge->queue_head = (uint8_t)((bridge->queue_head + 1) % PMC_SLAVE_QUEUE_SIZE);
        bridge->queue_count--;
    }

    bridge->active_valid = bridge->online = bridge->status_valid = bridge->version_valid = false;
    bridge->decoder.used = bridge->decoder.expected = 0;
    for(uint8_t index = 0; index < count; index++)
        emit(bridge, PmcSlaveEvent_LinkLost, sequences[index], NULL);
    if(was_online && count == 0)
        emit(bridge, PmcSlaveEvent_LinkLost, 0, NULL);
}

void pmc_slave_bridge_poll(pmc_slave_bridge_t *bridge, uint32_t now_ms)
{
    if(!bridge || !bridge->transport.send)
        return;

    if((bridge->transport.connected && !bridge->transport.connected(bridge->transport.context)) ||
       (bridge->online && bridge->link_timeout_ms && now_ms - bridge->last_rx >= bridge->link_timeout_ms)) {
        pmc_slave_bridge_disconnect(bridge);
        return;
    }

    if(bridge->active_valid && (int32_t)(now_ms - bridge->active.deadline) >= 0) {
        uint32_t sequence = bridge->active.packet.sequence;
        bridge->active_valid = false;
        emit(bridge, PmcSlaveEvent_Timeout, sequence, NULL);
        pmc_slave_bridge_disconnect(bridge);
        return;
    }

    while(!bridge->active_valid && bridge->queue_count) {
        pmc_slave_request_t *request = &bridge->queue[bridge->queue_head];
        if((int32_t)(now_ms - request->deadline) >= 0) {
            uint32_t sequence = request->packet.sequence;
            bridge->queue_head = (uint8_t)((bridge->queue_head + 1) % PMC_SLAVE_QUEUE_SIZE);
            bridge->queue_count--;
            emit(bridge, PmcSlaveEvent_Timeout, sequence, NULL);
            continue;
        }

        size_t size = pmc_slave_encode(&request->packet, bridge->frame, sizeof(bridge->frame));
        if(!size || !bridge->transport.send(bridge->transport.context, bridge->frame, size))
            return;

        bridge->active = *request;
        bridge->active_valid = true;
        bridge->queue_head = (uint8_t)((bridge->queue_head + 1) % PMC_SLAVE_QUEUE_SIZE);
        bridge->queue_count--;
        bridge->last_tx = now_ms;
        emit(bridge, PmcSlaveEvent_Sent, bridge->active.packet.sequence, NULL);
    }

    if(bridge->heartbeat_ms && now_ms - bridge->last_tx >= bridge->heartbeat_ms) {
        pmc_slave_packet_t heartbeat = {
            .sequence = next_sequence(bridge),
            .command = PmcSlave_Heartbeat
        };
        size_t size = pmc_slave_encode(&heartbeat, bridge->frame, sizeof(bridge->frame));
        if(bridge->transport.send(bridge->transport.context, bridge->frame, size))
            bridge->last_tx = now_ms;
    }
}

static void received(pmc_slave_bridge_t *bridge, uint32_t now_ms)
{
    const pmc_slave_packet_t *packet = &bridge->received;
    pmc_slave_reply_kind_t kind = pmc_slave_reply_kind(packet);
    if(kind == PmcSlaveReply_Invalid) {
        bridge->unmatched_replies++;
        return;
    }

    bridge->last_rx = now_ms;
    if(!bridge->online && kind != PmcSlaveReply_Version) {
        bridge->online = true;
        emit(bridge, PmcSlaveEvent_Online, 0, NULL);
    }

    if(kind == PmcSlaveReply_Version) {
        for(uint16_t index = 0; index < 64; index++)
            bridge->version[index] = (char)((uint32_t)packet->words[index / 4] >> (8U * (index % 4)));
        bridge->version[64] = '\0';
        bridge->version_valid = true;
    }

    if(kind == PmcSlaveReply_Status) {
        pmc_slave_decode_status(packet, &bridge->status);
        bridge->status_time = now_ms;
        bridge->status_valid = true;
        emit(bridge, PmcSlaveEvent_Status, packet->sequence, packet);
        return;
    }

    bool matched = bridge->active_valid && packet->sequence == bridge->active.packet.sequence &&
        ((packet->command & UINT32_C(0xFFFF)) == (bridge->active.packet.command & UINT32_C(0xFFFF)) ||
         kind == PmcSlaveReply_Error);

    if(!matched) {
        if(kind == PmcSlaveReply_Version)
            emit(bridge, PmcSlaveEvent_Version, packet->sequence, packet);
        else if(kind == PmcSlaveReply_Completion)
            emit(bridge, PmcSlaveEvent_Reply, packet->sequence, packet);
        else if((packet->command & UINT32_C(0xFFFF)) != PmcSlave_Heartbeat)
            bridge->unmatched_replies++;
        return;
    }

    bool waiting_completion = (bridge->active.packet.command & PMC_SLAVE_SYNC_FLAG) != 0;

    if(!waiting_completion || kind == PmcSlaveReply_Completion || kind == PmcSlaveReply_Error ||
       (packet->word_count == 1 && packet->words[0] == 0))
        bridge->active_valid = false;

    emit(bridge, kind == PmcSlaveReply_Version ? PmcSlaveEvent_Version : PmcSlaveEvent_Reply,
         packet->sequence, packet);
}

void pmc_slave_bridge_receive(pmc_slave_bridge_t *bridge, const uint8_t *bytes, size_t size, uint32_t now_ms)
{
    if(!bridge || (!bytes && size))
        return;
    for(size_t index = 0; index < size; index++)
        if(pmc_slave_decoder_push(&bridge->decoder, bytes[index], &bridge->received))
            received(bridge, now_ms);
}

bool pmc_slave_bridge_get_status(const pmc_slave_bridge_t *bridge, uint32_t now_ms,
                                 uint32_t max_age_ms, pmc_slave_status_t *status)
{
    if(!bridge || !status || !bridge->online || !bridge->status_valid ||
       now_ms - bridge->status_time > max_age_ms)
        return false;
    *status = bridge->status;
    return true;
}