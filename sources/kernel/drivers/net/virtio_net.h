#ifndef DRIVERS_NET_VIRTIO_NET_H
#define DRIVERS_NET_VIRTIO_NET_H

#include <drivers/net/net_nic.h>
#include <drivers/pci.h>

bool virtio_net_init(pci_device_t* dev, net_nic_t* nic);
bool virtio_net_send(const void* data, uint16_t len);
void virtio_net_poll(net_rx_handler_t rx_handler);

#endif // DRIVERS_NET_VIRTIO_NET_H
