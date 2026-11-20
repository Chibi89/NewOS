#include <bsod.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sched.h>
#include <drivers/power.h>
#include <fs/vfs.h>
#include <gui/graphics.h>
#include <gui/image.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*bsod_entry_fn)(const kernel_api_t* api, bsod_params_t* params);

static bsod_entry_fn g_bsod_entry = NULL;
static uint8_t*      g_bsod_buf   = NULL;
static uint8_t*      g_ksym_buf   = NULL;
static uint32_t      g_ksym_size  = 0;
static image_t*      g_login_bg   = NULL;
static volatile int  g_in_bsod    = 0;

static void bsod_serial_putc(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0) { }
    outb(0x3F8, (uint8_t)c);
}

static void bsod_serial_puts(const char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') bsod_serial_putc('\r');
        bsod_serial_putc(*s++);
    }
}

static void bsod_serial_hex64(uint64_t val) {
    const char* hex = "0123456789ABCDEF";
    bsod_serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        bsod_serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void bsod_fmt_hex64(uint64_t val, char* out) {
    const char* hex = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = hex[(val >> ((15 - i) * 4)) & 0xF];
    }
    out[18] = '\0';
}

void bsod_init(void) {
    if (!g_bsod_entry) {
        const char* dll_paths[] = {
            "C:\\NewOS\\SystemLibrary\\BSOD.DLL",
            "C:\\NewOS\\Boot\\BSOD.DLL",
            "/NewOS/SystemLibrary/BSOD.DLL",
            "BSOD.DLL"
        };
        uint32_t sz = 0;
        for (int i = 0; i < 4 && !g_bsod_entry; i++) {
            uint8_t* buf = (uint8_t*)vfs_read_entire_file(dll_paths[i], &sz);
            if (buf && sz >= sizeof(module_header_t)) {
                if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                    uint8_t* exec_buf = (uint8_t*)kmalloc(sz + 16384);
                    if (exec_buf) {
                        memcpy(exec_buf, buf, sz);
                        memset(exec_buf + sz, 0, 16384);
                        const module_header_t* h = (const module_header_t*)exec_buf;
                        g_bsod_entry = (bsod_entry_fn)(exec_buf + h->entry_offset);
                        g_bsod_buf   = exec_buf;
                    }
                }
            }
            if (buf) kfree(buf);
        }
    }

    if (!g_ksym_buf) {
        const char* sym_paths[] = {
            "C:\\NewOS\\SystemLibrary\\KERNEL.SYM",
            "C:\\NewOS\\Boot\\KERNEL.SYM",
            "/NewOS/SystemLibrary/KERNEL.SYM",
            "KERNEL.SYM"
        };
        uint32_t ssz = 0;
        for (int i = 0; i < 4 && !g_ksym_buf; i++) {
            uint8_t* sbuf = (uint8_t*)vfs_read_entire_file(sym_paths[i], &ssz);
            if (sbuf && ssz >= sizeof(ksym_header_t)) {
                const ksym_header_t* hdr = (const ksym_header_t*)sbuf;
                if (hdr->magic == 0x4D59534E) { // 'NSYM'
                    g_ksym_buf  = sbuf;
                    g_ksym_size = ssz;
                    break;
                }
            }
            if (sbuf) kfree(sbuf);
        }
    }

    if (!g_login_bg) {
        const char* bg_paths[] = {
            "C:\\NewOS\\Resources\\Screens\\LOGIN.BMP",
            "C:\\NewOS\\Resources\\LOGIN.BMP",
            "C:\\NewOS\\Resource\\wallpapers\\LOGIN.BMP",
            "/NewOS/Resources/Screens/LOGIN.BMP"
        };
        for (int i = 0; i < 4 && !g_login_bg; i++) {
            image_t* img = image_load_from_file(bg_paths[i]);
            if (img && img->pixels) {
                if (img->width != 1024 || img->height != 768) {
                    image_t* resized = image_resize(img, 1024, 768);
                    image_free(img);
                    img = resized;
                }
                g_login_bg = img;
            }
        }
    }
}

static void bsod_fallback_render(const bsod_params_t* p) {
    char hex[20];
    gfx_clear(0x003791);
    gfx_draw_string(40, 40, "NewOS V1 (64-bit) - KERNEL FATAL ERROR (Pre-DLL Fallback)", 0xFFFFFF, 0, true);
    gfx_draw_string(40, 80, p->stop_code ? p->stop_code : "CPU_EXCEPTION", 0x99D9FF, 0, true);
    if (p->message) {
        gfx_draw_string(40, 108, p->message, 0xFFFFFF, 0, true);
    }
    gfx_draw_string(40, 148, "RIP:", 0x99D9FF, 0, true);
    bsod_fmt_hex64(p->rip, hex);
    gfx_draw_string(100, 148, hex, 0xFFFFFF, 0, true);
    gfx_draw_string(40, 172, "CR2:", 0x99D9FF, 0, true);
    bsod_fmt_hex64(p->cr2, hex);
    gfx_draw_string(100, 172, hex, 0xFFFFFF, 0, true);
    gfx_draw_string(40, 220, "Premi [INVIO] o [R] per riavviare...", 0xFFFFFF, 0, true);
    gfx_flip();

    for (;;) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (sc == 0x1C || sc == 0x13) power_reboot();
            if (sc == 0x1F) power_shutdown();
        }
        asm volatile("pause");
    }
}

static void bsod_dispatch(bsod_params_t* p) {
    asm volatile("cli");
    if (g_in_bsod) {
        bsod_serial_puts("\n[BSOD] Recursive fault halted!\n");
        for (;;) asm volatile("cli; hlt");
    }
    g_in_bsod = 1;

    uint64_t cr0 = 0, cr2 = 0, cr3 = 0, cr4 = 0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    p->cr0 = cr0;
    p->cr2 = cr2;
    p->cr3 = cr3;
    p->cr4 = cr4;

    p->sym_data              = g_ksym_buf;
    p->sym_size              = g_ksym_size;
    p->gfx_fill_alpha        = gfx_fill_alpha;
    p->gfx_draw_specular_rim = gfx_draw_specular_rim;
    p->gfx_draw_drop_shadow  = gfx_draw_drop_shadow;
    p->gfx_flip              = gfx_flip;
    p->gfx_get_font_w        = gfx_get_font_w;
    p->gfx_get_font_h        = gfx_get_font_h;

    bsod_serial_puts("\n=== NewOS V1 BSOD ===\nRIP=");
    bsod_serial_hex64(p->rip);
    bsod_serial_puts(" CR2=");
    bsod_serial_hex64(p->cr2);
    bsod_serial_puts(" INT=");
    bsod_serial_hex64(p->int_no);
    bsod_serial_puts("\n");

    if (g_bsod_entry) {
        if (g_login_bg && g_login_bg->pixels) {
            p->login_bg_pixels = g_login_bg->pixels;
            p->login_bg_w      = g_login_bg->width;
            p->login_bg_h      = g_login_bg->height;
        }
        g_bsod_entry(screen_mgr_get_api(), p);
    }
    bsod_fallback_render(p);
}

void bsod_from_exception(interrupt_frame_t* frame) {
    bsod_params_t p;
    memset(&p, 0, sizeof(p));
    if (frame) {
        p.int_no   = frame->int_no;
        p.err_code = frame->err_code;
        p.rip      = frame->rip;
        p.cs       = frame->cs;
        p.rflags   = frame->rflags;
        p.rsp      = frame->rsp;
        p.ss       = frame->ss;
        p.rax      = frame->rax;
        p.rbx      = frame->rbx;
        p.rcx      = frame->rcx;
        p.rdx      = frame->rdx;
        p.rsi      = frame->rsi;
        p.rdi      = frame->rdi;
        p.rbp      = frame->rbp;
        p.r8       = frame->r8;
        p.r9       = frame->r9;
        p.r10      = frame->r10;
        p.r11      = frame->r11;
        p.r12      = frame->r12;
        p.r13      = frame->r13;
        p.r14      = frame->r14;
        p.r15      = frame->r15;
    }
    bsod_dispatch(&p);
}

void kpanic(const char* stop_code, const char* msg, const char* file, uint32_t line) {
    bsod_params_t p;
    memset(&p, 0, sizeof(p));
    uint64_t rbp_val = 0, rsp_val = 0;
    asm volatile("mov %%rbp, %0" : "=r"(rbp_val));
    asm volatile("mov %%rsp, %0" : "=r"(rsp_val));
    p.int_no    = 0xFF;
    p.rip       = (uint64_t)__builtin_return_address(0);
    p.rbp       = rbp_val;
    p.rsp       = rsp_val;
    p.stop_code = stop_code ? stop_code : "KERNEL_PANIC";
    p.message   = msg ? msg : "Errore critico irreversibile del kernel";
    p.file      = file;
    p.line      = line;
    bsod_dispatch(&p);
}
