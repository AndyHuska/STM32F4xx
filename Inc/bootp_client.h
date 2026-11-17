#pragma once

#include <stdbool.h>
#include "lwip/netif.h"

void bootp_client_link(struct netif *netif, bool link_up);
void bootp_client_poll(void);