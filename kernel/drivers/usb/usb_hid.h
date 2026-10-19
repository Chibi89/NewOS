#ifndef DRIVERS_USB_HID_H
#define DRIVERS_USB_HID_H

#include <types.h>
#include <drivers/usb/usb.h>

#define USB_HID_MAX_INSTANCES 4

typedef enum {
    USB_HID_TYPE_UNKNOWN  = 0,
    USB_HID_TYPE_KEYBOARD = 1,
    USB_HID_TYPE_MOUSE    = 2
} usb_hid_type_t;

typedef struct {
    uint8_t         port_id;
    usb_hid_type_t  type;
    bool            is_attached;
    char            name[40];
} usb_hid_instance_t;

bool usb_hid_init(void);
bool usb_hid_is_active(void);
int  usb_hid_get_count(void);
const usb_hid_instance_t* usb_hid_get_instance(int index);

// Iniezione e decodifica eventi standard Boot Protocol
void usb_hid_process_mouse(int8_t dx, int8_t dy, uint8_t buttons);
void usb_hid_process_keyboard(uint8_t modifiers, const uint8_t keys[6]);

#endif // DRIVERS_USB_HID_H
