#ifndef DRIVERS_NET_E1000_H
#define DRIVERS_NET_E1000_H

#include <drivers/net/net_nic.h>
#include <drivers/pci.h>

// Inizializza controller Intel E1000 / 82540EM
bool e1000_init(pci_device_t* dev, net_nic_t* nic);

// Invia un pacchetto tramite e1000
bool e1000_send(const void* data, uint16_t len);

// Controlla se sono arrivati pacchetti in ricezione
void e1000_poll(net_rx_handler_t rx_handler);

#endif // DRIVERS_NET_E1000_H
