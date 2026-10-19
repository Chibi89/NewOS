#include <drivers/mouse.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>

#define MOUSE_PORT       0x60
#define MOUSE_STATUS     0x64
#define MOUSE_CMD        0x64

static uint32_t scr_w = 1024;
static uint32_t scr_h = 768;

static uint8_t mouse_cycle = 0;
static int8_t  mouse_byte[3];
static mouse_state_t current_mouse;

static void mouse_wait(uint8_t type) {
    uint32_t time_out = 100000;
    if (type == 0) {
        // Aspetta che il buffer sia leggibile (bit 0 = 1)
        while (time_out--) {
            if ((inb(MOUSE_STATUS) & 1) == 1) return;
        }
    } else {
        // Aspetta che il buffer sia pronto per la scrittura (bit 1 = 0)
        while (time_out--) {
            if ((inb(MOUSE_STATUS) & 2) == 0) return;
        }
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(MOUSE_CMD, 0xD4);
    mouse_wait(1);
    outb(MOUSE_PORT, write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_PORT);
}

static void mouse_callback(interrupt_frame_t* frame) {
    (void)frame;
    uint8_t status = inb(MOUSE_STATUS);
    if (!(status & 0x20)) {
        // Non è un pacchetto per il mouse
        return;
    }

    uint8_t b = inb(MOUSE_PORT);

    if (mouse_cycle == 0) {
        // Il bit 3 del primo byte deve essere sempre 1 nei pacchetti PS/2 standard
        if (!(b & 0x08)) {
            return;
        }
        mouse_byte[0] = (int8_t)b;
        mouse_cycle++;
    } else if (mouse_cycle == 1) {
        mouse_byte[1] = (int8_t)b;
        mouse_cycle++;
    } else if (mouse_cycle == 2) {
        mouse_byte[2] = (int8_t)b;
        mouse_cycle = 0;

        // Decodifica pacchetto a 3 byte
        uint8_t flags = (uint8_t)mouse_byte[0];
        int32_t dx = mouse_byte[1];
        int32_t dy = mouse_byte[2];

        // Segno X e Y
        if (flags & 0x10) dx |= 0xFFFFFF00;
        if (flags & 0x20) dy |= 0xFFFFFF00;

        current_mouse.x += dx;
        current_mouse.y -= dy; // Inversione asse Y dello schermo

        if (current_mouse.x < 0) current_mouse.x = 0;
        if (current_mouse.x >= (int32_t)scr_w) current_mouse.x = scr_w - 1;
        if (current_mouse.y < 0) current_mouse.y = 0;
        if (current_mouse.y >= (int32_t)scr_h) current_mouse.y = scr_h - 1;

        current_mouse.left_button   = (flags & 0x01) ? true : false;
        current_mouse.right_button  = (flags & 0x02) ? true : false;
        current_mouse.middle_button = (flags & 0x04) ? true : false;
    }
}

void mouse_init(uint32_t screen_width, uint32_t screen_height) {
    scr_w = screen_width;
    scr_h = screen_height;
    current_mouse.x = screen_width / 2;
    current_mouse.y = screen_height / 2;
    current_mouse.left_button = false;
    current_mouse.right_button = false;
    current_mouse.middle_button = false;
    mouse_cycle = 0;

    // Registra IRQ 12 = INT 44
    register_interrupt_handler(44, mouse_callback);

    // Abilita la porta ausiliaria del mouse sul controller PS/2
    mouse_wait(1);
    outb(MOUSE_CMD, 0xA8);

    // Abilita interrupt per mouse
    mouse_wait(1);
    outb(MOUSE_CMD, 0x20);
    mouse_wait(0);
    uint8_t status = inb(MOUSE_PORT);
    status |= 0x03;     // Enable mouse IRQ 12 (bit 1) and keyboard IRQ 1 (bit 0)
    status &= ~0x30;    // Disable mouse clock disable (bit 5) and keyboard clock disable (bit 4)

    mouse_wait(1);
    outb(MOUSE_CMD, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT, status);

    // Imposta impostazioni di default
    mouse_write(0xF6);
    mouse_read(); // ACK (0xFA)

    // Abilita data streaming
    mouse_write(0xF4);
    mouse_read(); // ACK (0xFA)
}

void mouse_set_bounds(uint32_t screen_width, uint32_t screen_height) {
    scr_w = screen_width;
    scr_h = screen_height;
    if (current_mouse.x >= (int32_t)scr_w) current_mouse.x = scr_w - 1;
    if (current_mouse.y >= (int32_t)scr_h) current_mouse.y = scr_h - 1;
}

void mouse_get_state(mouse_state_t* state) {
    if (state) {
        *state = current_mouse;
    }
}

void mouse_inject_event(int32_t dx, int32_t dy, bool left, bool right, bool middle) {
    current_mouse.x += dx;
    current_mouse.y += dy;
    if (current_mouse.x < 0) current_mouse.x = 0;
    if (current_mouse.x >= (int32_t)scr_w) current_mouse.x = scr_w - 1;
    if (current_mouse.y < 0) current_mouse.y = 0;
    if (current_mouse.y >= (int32_t)scr_h) current_mouse.y = scr_h - 1;

    current_mouse.left_button   = left;
    current_mouse.right_button  = right;
    current_mouse.middle_button = middle;
}
