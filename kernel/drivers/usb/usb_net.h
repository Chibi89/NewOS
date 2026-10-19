#ifndef DRIVERS_USB_USB_NET_H
#define DRIVERS_USB_USB_NET_H

#include <types.h>
#include <drivers/usb/usb.h>

bool                usb_net_init(void);
bool                usb_net_is_present(void);
const usb_device_t* usb_net_get_active_dongle(void);
uint32_t            usb_net_get_bus_max_rate_mbps(void);
bool                usb_net_send_packet(const void* data, uint32_t len);
int                 usb_net_recv_packet(void* buf, uint32_t max_len);

#endif // DRIVERS_USB_USB_NET_H
