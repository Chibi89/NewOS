#include <arch/x86_64/idt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sched.h>
#include <string.h>

static idt_entry_t idt_entries[256];
static idtr_t      idtr;
static isr_handler_t interrupt_handlers[256];

// Dichiarazioni esterne per gli stubs assembly
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].offset_1 = (uint16_t)(base & 0xFFFF);
    idt_entries[num].selector = sel;
    idt_entries[num].ist = 0;
    idt_entries[num].type_attr = flags;
    idt_entries[num].offset_2 = (uint16_t)((base >> 16) & 0xFFFF);
    idt_entries[num].offset_3 = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt_entries[num].zero = 0;
}

void register_interrupt_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
}

uint64_t isr_handler_main(interrupt_frame_t* frame) {
    if (interrupt_handlers[frame->int_no] != NULL) {
        interrupt_handlers[frame->int_no](frame);
    }

    // Se è un IRQ hardware (32..47), invia EOI al PIC
    if (frame->int_no >= 32 && frame->int_no < 48) {
        pic_send_eoi((uint8_t)(frame->int_no - 32));
    }

    // Preemptive Multitasking: se l'interrupt è il Timer (IRQ 0 = int 32)
    // e lo scheduler è attivo, effettua il cambio di contesto
    if (frame->int_no == 32 && sched_is_active()) {
        interrupt_frame_t* next_frame = sched_schedule(frame);
        return (uint64_t)next_frame;
    }

    return (uint64_t)frame;
}

void idt_init(void) {
    idtr.limit = sizeof(idt_entries) - 1;
    idtr.base  = (uint64_t)&idt_entries[0];

    memset(&idt_entries, 0, sizeof(idt_entries));
    memset(&interrupt_handlers, 0, sizeof(interrupt_handlers));

    // Configura le eccezioni della CPU (0..31) con selettore codice a 64-bit 0x18
    idt_set_gate(0,  (uint64_t)isr0,  0x18, 0x8E);
    idt_set_gate(1,  (uint64_t)isr1,  0x18, 0x8E);
    idt_set_gate(2,  (uint64_t)isr2,  0x18, 0x8E);
    idt_set_gate(3,  (uint64_t)isr3,  0x18, 0x8E);
    idt_set_gate(4,  (uint64_t)isr4,  0x18, 0x8E);
    idt_set_gate(5,  (uint64_t)isr5,  0x18, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x18, 0x8E);
    idt_set_gate(7,  (uint64_t)isr7,  0x18, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x18, 0x8E);
    idt_set_gate(9,  (uint64_t)isr9,  0x18, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x18, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x18, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x18, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x18, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x18, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x18, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x18, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x18, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x18, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x18, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x18, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x18, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x18, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x18, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x18, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x18, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x18, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x18, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x18, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x18, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x18, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x18, 0x8E);

    // Rimappa il PIC 8259: IRQ 0..7 su 32..39, IRQ 8..15 su 40..47
    pic_remap(32, 40);

    // Configura i gate per gli IRQ Hardware (32..47)
    idt_set_gate(32, (uint64_t)irq0,  0x18, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x18, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x18, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x18, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x18, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x18, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x18, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x18, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x18, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x18, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x18, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x18, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x18, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x18, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x18, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x18, 0x8E);

    // Carica la tabella degli interrupt nel processore con lidt
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
