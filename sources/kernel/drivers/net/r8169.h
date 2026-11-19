#ifndef DRIVERS_NET_R8169_H
#define DRIVERS_NET_R8169_H

#include <drivers/net/net_nic.h>
#include <drivers/pci.h>

bool r8169_init(pci_device_t* dev, net_nic_t* nic);
bool r8169_send(const void* data, uint16_t len);
void r8169_poll(net_rx_handler_t rx_handler);

#endif // DRIVERS_NET_R8169_H
