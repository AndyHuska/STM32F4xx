#pragma once

#include "pmc_slave_protocol.h"

#define PMC_SLAVE_QUEUE_SIZE 4U

typedef enum {
    PmcSlaveEvent_Sent,
    PmcSlaveEvent_Reply,
    PmcSlaveEvent_Status,
    PmcSlaveEvent_Version,
    PmcSlaveEvent_Online,
    PmcSlaveEvent_LinkLost,
    PmcSlaveEvent_Timeout
} pmc_slave_event_t;

typedef struct {
    void *context;
    bool (*send)(void *context, const uint8_t *bytes, size_t size);
    bool (*connected)(void *context);
} pmc_slave_transport_t;

typedef void (*pmc_slave_event_handler_t)(void *context, pmc_slave_event_t event,
                                         uint32_t sequence, const pmc_slave_packet_t *reply);

typedef struct {
    pmc_slave_packet_t packet;
    uint32_t deadline;
} pmc_slave_request_t;

typedef struct {
    pmc_slave_transport_t transport;
    pmc_slave_event_handler_t on_event;
    void *event_context;
    pmc_slave_decoder_t decoder;
    pmc_slave_packet_t received;
    pmc_slave_request_t queue[PMC_SLAVE_QUEUE_SIZE];
    pmc_slave_request_t active;
    pmc_slave_status_t status;
    char version[65];
    uint8_t frame[PMC_SLAVE_MAX_FRAME];
    uint8_t queue_head;
    uint8_t queue_count;
    bool active_valid;
    bool online;
    bool status_valid;
    bool version_valid;
    uint32_t next_sequence;
    uint32_t last_tx;
    uint32_t last_rx;
    uint32_t status_time;
    uint32_t heartbeat_ms;
    uint32_t link_timeout_ms;
    uint32_t unmatched_replies;
} pmc_slave_bridge_t;

void pmc_slave_bridge_init(pmc_slave_bridge_t *bridge, const pmc_slave_transport_t *transport,
                           pmc_slave_event_handler_t handler, void *context);
bool pmc_slave_bridge_submit(pmc_slave_bridge_t *bridge, const pmc_slave_packet_t *request,
                             uint32_t now_ms, uint32_t timeout_ms, uint32_t *sequence);
void pmc_slave_bridge_poll(pmc_slave_bridge_t *bridge, uint32_t now_ms);
void pmc_slave_bridge_receive(pmc_slave_bridge_t *bridge, const uint8_t *bytes, size_t size, uint32_t now_ms);
void pmc_slave_bridge_disconnect(pmc_slave_bridge_t *bridge);
bool pmc_slave_bridge_get_status(const pmc_slave_bridge_t *bridge, uint32_t now_ms,
                                 uint32_t max_age_ms, pmc_slave_status_t *status);