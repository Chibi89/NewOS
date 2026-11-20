#ifndef ARCH_BSOD_H
#define ARCH_BSOD_H

#include <types.h>
#include <arch/x86_64/idt.h>

typedef struct {
    uint32_t magic;       // 'NSYM' (0x4D59534E)
    uint32_t count;       // Numero di record ordinati per addr
    uint32_t str_offset;  // Offset byte inizio pool stringhe
    uint32_t str_size;    // Dimensione pool stringhe
} __attribute__((packed)) ksym_header_t;

typedef struct {
    uint64_t addr;        // Indirizzo istruzione/funzione in kernel.elf
    uint32_t line;        // Numero riga sorgente
    uint16_t file_off;    // Offset nel pool stringhe (path file)
    uint16_t func_off;    // Offset nel pool stringhe (nome funzione)
} __attribute__((packed)) ksym_entry_t;

typedef struct {
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t cr0, cr2, cr3, cr4;

    const char* stop_code; // NULL se da eccezione CPU
    const char* message;   // NULL se da eccezione CPU
    const char* file;      // __FILE__ se KPANIC, altrimenti risolto da KERNEL.SYM
    uint32_t    line;      // __LINE__ se KPANIC, altrimenti risolto da KERNEL.SYM

    const void* sym_data;  // KERNEL.SYM pre-caricato in RAM
    uint32_t    sym_size;  // Dimensione in byte di KERNEL.SYM

    const uint32_t* login_bg_pixels; // Sfondo ufficiale LOGIN.BMP (1024x768 nitido)
    int32_t         login_bg_w;
    int32_t         login_bg_h;

    // Callback Liquid Glass & Graphics non presenti nella kernel_api_t base
    void (*gfx_fill_alpha)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, uint8_t alpha);
    void (*gfx_draw_specular_rim)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius);
    void (*gfx_draw_drop_shadow)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t spread);
    void (*gfx_flip)(void);
    uint32_t (*gfx_get_font_w)(void);
    uint32_t (*gfx_get_font_h)(void);
} bsod_params_t;

void bsod_init(void);
void bsod_from_exception(interrupt_frame_t* frame);
void kpanic(const char* stop_code, const char* msg, const char* file, uint32_t line);

#define KPANIC(msg) kpanic("KERNEL_PANIC", (msg), __FILE__, __LINE__)

#endif // ARCH_BSOD_H
