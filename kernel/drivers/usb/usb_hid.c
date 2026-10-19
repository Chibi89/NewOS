#include <drivers/usb/usb_hid.h>
#include <drivers/usb/usb.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <string.h>

static usb_hid_instance_t g_hid_instances[USB_HID_MAX_INSTANCES];
static int                g_hid_count = 0;
static bool               g_hid_initialized = false;

// Tabella di traduzione USB HID Usage ID (0x04..0x38) in ASCII
static const char hid_to_ascii_lower[128] = {
    0, 0, 0, 0,
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', // 0x04 - 0x10
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', // 0x11 - 0x1D
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',               // 0x1E - 0x27
    '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\',            // 0x28 - 0x31
    '#', ';', '\'', '`', ',', '.', '/', 0                            // 0x32 - 0x38
};

static const char hid_to_ascii_upper[128] = {
    0, 0, 0, 0,
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 27, '\b', '\t', ' ', '_', '+', '{', '}', '|',
    '~', ':', '"', '~', '<', '>', '?', 0
};

bool usb_hid_init(void) {
    if (g_hid_initialized) return true;

    memset(g_hid_instances, 0, sizeof(g_hid_instances));
    g_hid_count = 0;

    // Ispeziona tutte le periferiche USB registrate alla ricerca di classi HID
    int total = usb_get_device_count();
    for (int i = 0; i < total && g_hid_count < USB_HID_MAX_INSTANCES; i++) {
        usb_device_t* dev = usb_get_device(i);
        if (!dev || !dev->is_connected) continue;

        if (dev->class_code == USB_CLASS_HID) {
            usb_hid_instance_t* inst = &g_hid_instances[g_hid_count];
            inst->port_id = dev->port_id;
            inst->is_attached = true;

            if (dev->protocol == 0x01 || strstr(dev->product, "Keyboard") || strstr(dev->product, "kbd")) {
                inst->type = USB_HID_TYPE_KEYBOARD;
                strncpy(inst->name, dev->product, sizeof(inst->name) - 1);
            } else {
                inst->type = USB_HID_TYPE_MOUSE;
                strncpy(inst->name, dev->product, sizeof(inst->name) - 1);
            }
            g_hid_count++;
        }
    }

    g_hid_initialized = true;
    return true;
}

bool usb_hid_is_active(void) {
    return g_hid_initialized && (g_hid_count > 0);
}

int usb_hid_get_count(void) {
    return g_hid_count;
}

const usb_hid_instance_t* usb_hid_get_instance(int index) {
    if (index < 0 || index >= g_hid_count) return NULL;
    return &g_hid_instances[index];
}

void usb_hid_process_mouse(int8_t dx, int8_t dy, uint8_t buttons) {
    bool left   = (buttons & 0x01) != 0;
    bool right  = (buttons & 0x02) != 0;
    bool middle = (buttons & 0x04) != 0;

    // Inietta direttamente nel cursore grafico di NewOS
    mouse_inject_event((int32_t)dx, (int32_t)dy, left, right, middle);
}

void usb_hid_process_keyboard(uint8_t modifiers, const uint8_t keys[6]) {
    bool shift = (modifiers & 0x02) || (modifiers & 0x20); // Left/Right Shift

    for (int i = 0; i < 6; i++) {
        uint8_t k = keys[i];
        if (k == 0) continue;

        char c = 0;
        if (k < 128) {
            c = shift ? hid_to_ascii_upper[k] : hid_to_ascii_lower[k];
        }

        if (c != 0) {
            keyboard_inject_char(c);
        }
    }
}
