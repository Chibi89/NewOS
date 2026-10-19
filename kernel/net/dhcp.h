#ifndef NET_DHCP_H
#define NET_DHCP_H

#include <types.h>

typedef enum {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_DISCOVER_SENT,
    DHCP_STATE_OFFER_RECEIVED,
    DHCP_STATE_REQUEST_SENT,
    DHCP_STATE_BOUND,
    DHCP_STATE_FAILED
} dhcp_state_t;

// Inizializza il client DHCP su porta UDP 68
void dhcp_init(void);

// Avvia la negoziazione DHCP (Discover -> Offer -> Request -> Ack)
bool dhcp_discover(void);

// Restituisce lo stato corrente della negoziazione DHCP
dhcp_state_t dhcp_get_state(void);
const char*  dhcp_state_to_str(dhcp_state_t st);

#endif // NET_DHCP_H
