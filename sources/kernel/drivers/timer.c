#include <drivers/timer.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>

static volatile uint64_t system_ticks = 0;
static uint32_t current_freq = 100;

static void timer_callback(interrupt_frame_t* frame) {
    (void)frame;
    system_ticks++;
}

void timer_init(uint32_t frequency) {
    current_freq = frequency;
    register_interrupt_handler(32, timer_callback); // IRQ 0 = INT 32

    uint32_t divisor = 1193180 / frequency;

    // Comando PIT: Canale 0, Accesso Lobo/Hibo, Modalità 2 (Rate Generator)
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t timer_get_ticks(void) {
    return system_ticks;
}

void timer_sleep(uint32_t ms) {
    uint64_t target_ticks = system_ticks + ((uint64_t)ms * current_freq) / 1000;
    while (system_ticks < target_ticks) {
        hlt();
    }
}
