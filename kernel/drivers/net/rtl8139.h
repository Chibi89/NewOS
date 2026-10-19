#ifndef DRIVERS_NET_RTL8139_H
#define DRIVERS_NET_RTL8139_H

#include <drivers/net/net_nic.h>
#include <drivers/pci.h>

// Inizializza controller Realtek RTL8139 Fast Ethernet
bool rtl8139_init(pci_device_t* dev, net_nic_t* nic);

// Invia un pacchetto tramite RTL8139
bool rtl8139_send(const void* data, uint16_t len);

// Controlla se sono arrivati pacchetti in ricezione
void rtl8139_poll(net_rx_handler_t rx_handler);

#endif // DRIVERS_NET_RTL8139_H
