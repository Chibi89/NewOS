#include <drivers/keyboard.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KBD_BUFFER_SIZE      256

static char kbd_buffer[KBD_BUFFER_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;
static bool shift_pressed = false;
static bool caps_lock = false;

// Mappa tasti scancode set 1 standard
static const char kbd_us_lower[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0
};

static const char kbd_us_upper[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0
};

static void keyboard_callback(interrupt_frame_t* frame) {
    (void)frame;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode == 0x2A || scancode == 0x36) { // Left / Right Shift down
        shift_pressed = true;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) { // Left / Right Shift up
        shift_pressed = false;
        return;
    }
    if (scancode == 0x3A) { // Caps Lock down
        caps_lock = !caps_lock;
        return;
    }

    if (scancode & 0x80) {
        // Tasto rilasciato, ignora
        return;
    }

    char c = 0;
    bool upper = shift_pressed ^ caps_lock;
    if (scancode < 128) {
        c = upper ? kbd_us_upper[scancode] : kbd_us_lower[scancode];
    }

    if (c != 0) {
        int next_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
        if (next_head != kbd_tail) {
            kbd_buffer[kbd_head] = c;
            kbd_head = next_head;
        }
    }
}

static void kbd_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (inb(KEYBOARD_STATUS_PORT) & 1) return;
        }
    } else {
        while (timeout--) {
            if (!(inb(KEYBOARD_STATUS_PORT) & 2)) return;
        }
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback); // IRQ 1 = INT 33

    // Abilita prima porta PS/2 (tastiera) sul controller 8042
    kbd_wait(1);
    outb(KEYBOARD_STATUS_PORT, 0xAE);

    // Leggi Configuration Byte
    kbd_wait(1);
    outb(KEYBOARD_STATUS_PORT, 0x20);
    kbd_wait(0);
    uint8_t status = inb(KEYBOARD_DATA_PORT);

    // Abilita IRQ 1 (bit 0) e abilita clock tastiera (azzera bit 4)
    status |= 0x01;
    status &= ~0x10;

    kbd_wait(1);
    outb(KEYBOARD_STATUS_PORT, 0x60);
    kbd_wait(1);
    outb(KEYBOARD_DATA_PORT, status);
}

bool keyboard_has_char(void) {
    return (kbd_head != kbd_tail);
}

char keyboard_get_char(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

void keyboard_inject_char(char c) {
    if (c != 0) {
        int next_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
        if (next_head != kbd_tail) {
            kbd_buffer[kbd_head] = c;
            kbd_head = next_head;
        }
    }
}
