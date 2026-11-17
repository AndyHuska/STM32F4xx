#include "driver.h"

#if ETHERNET_ENABLE && !defined(_WIZCHIP_) && BOOTP_ENABLE

#include "bootp_client.h"
#include "boards/bootp_packet.h"
#if BOOTP_PMC_PROFILE
#include "boards/bootp_pmc_profile.h"
#endif
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include <string.h>

static struct {
    struct netif *netif;
    struct udp_pcb *pcb;
    uint32_t transaction;
    uint32_t started;
    uint32_t next_request;
    uint32_t retry_ms;
    bool acquiring;
    uint8_t packet[576];
} bootp;

static void receive_reply(void *context, struct udp_pcb *pcb, struct pbuf *packet,
                           const ip_addr_t *source, u16_t port)
{
    (void)context;
    (void)pcb;
    (void)source;
    bootp_address_t acquired;
    if(packet && bootp.acquiring && port == 67 && packet->tot_len <= sizeof(bootp.packet) &&
       pbuf_copy_partial(packet, bootp.packet, packet->tot_len, 0) == packet->tot_len &&
       bootp_parse_reply(bootp.packet, packet->tot_len, bootp.netif->hwaddr, bootp.transaction, &acquired)) {
        ip4_addr_t address, mask, gateway;
        IP4_ADDR(&address, acquired.address[0], acquired.address[1], acquired.address[2], acquired.address[3]);
        IP4_ADDR(&mask, acquired.mask[0], acquired.mask[1], acquired.mask[2], acquired.mask[3]);
        IP4_ADDR(&gateway, acquired.gateway[0], acquired.gateway[1], acquired.gateway[2], acquired.gateway[3]);
        bootp.acquiring = false;
        netif_set_addr(bootp.netif, &address, &mask, &gateway);
    }
    if(packet)
        pbuf_free(packet);
}

void bootp_client_link(struct netif *netif, bool link_up)
{
    if(bootp.pcb) {
        udp_remove(bootp.pcb);
        bootp.pcb = NULL;
    }
    bootp.netif = netif;
    bootp.acquiring = false;
    if(!link_up) {
        ip4_addr_t cleared = {0};
        netif_set_addr(netif, &cleared, &cleared, &cleared);
        return;
    }
    bootp.started = HAL_GetTick();
    bootp.transaction ^= bootp.started ^ DWT->CYCCNT;
    for(uint8_t index = 0; index < 6; index++)
        bootp.transaction = bootp.transaction * 33U + netif->hwaddr[index];
    if(!++bootp.transaction)
        bootp.transaction = 1;
    bootp.retry_ms = 1000;
    bootp.next_request = bootp.started;
    bootp.acquiring = true;
}

void bootp_client_poll(void)
{
    if(!bootp.acquiring || !netif_is_link_up(bootp.netif))
        return;
    uint32_t now = HAL_GetTick();
    if((int32_t)(now - bootp.next_request) < 0)
        return;
    bootp.next_request = now + bootp.retry_ms;
    if(bootp.retry_ms < 16000)
        bootp.retry_ms *= 2;

    if(!bootp.pcb) {
        bootp.pcb = udp_new_ip_type(IPADDR_TYPE_V4);
        if(!bootp.pcb)
            return;
        ip_set_option(bootp.pcb, SOF_BROADCAST);
        if(udp_bind(bootp.pcb, IP_ANY_TYPE, 68) != ERR_OK) {
            udp_remove(bootp.pcb);
            bootp.pcb = NULL;
            return;
        }
        udp_bind_netif(bootp.pcb, bootp.netif);
        udp_recv(bootp.pcb, receive_reply, NULL);
    }

    const bootp_request_profile_t *profile = NULL;
#if BOOTP_PMC_PROFILE
    const char *loader = NULL, *application = NULL;
#if defined(BOARD_PMC_02_AA) && defined(PMC_LEGACY_BOOTLOADER)
    extern const char pmc_package_version[64];
    const char *stored_loader = (const char *)UINT32_C(0x0800FF08);
    if(!memcmp(stored_loader, "PMC-02-AA_", 10) && memchr(stored_loader, 0, 64))
        loader = stored_loader;
    application = pmc_package_version;
#endif
    bootp_request_profile_t pmc_profile = bootp_pmc_profile(loader, application);
    profile = &pmc_profile;
#endif
    uint32_t seconds = (now - bootp.started) / 1000U;
    size_t size = bootp_make_profiled_request(bootp.packet, sizeof(bootp.packet), bootp.netif->hwaddr,
                                             bootp.transaction, seconds > UINT16_MAX ? UINT16_MAX : (uint16_t)seconds,
                                             profile);
    if(!size)
        return;
    struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)size, PBUF_RAM);
    if(!packet)
        return;
    if(pbuf_take(packet, bootp.packet, (u16_t)size) == ERR_OK)
        udp_sendto_if_src(bootp.pcb, packet, IP_ADDR_BROADCAST, 67, bootp.netif, IP_ANY_TYPE);
    pbuf_free(packet);
}

#endif