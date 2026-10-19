#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <types.h>

typedef struct {
    uint16_t offset_1;   // offset bits 0..15
    uint16_t selector;   // Code segment selector (0x08)
    uint8_t  ist;        // Interrupt Stack Table offset (0 se non usato)
    uint8_t  type_attr;  // Tipo e attributi (0x8E = 64-bit Interrupt Gate, Ring 0, Present)
    uint16_t offset_2;   // offset bits 16..31
    uint32_t offset_3;   // offset bits 32..63
    uint32_t zero;       // Riservato
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_frame_t;

typedef void (*isr_handler_t)(interrupt_frame_t* frame);

void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif // ARCH_IDT_H
