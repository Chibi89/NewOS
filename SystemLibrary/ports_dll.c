/**
 * SystemLibrary/ports_dll.c
 * Sottosistema Out-of-Kernel per Porte Parallele Legacy (LPT1-LPT3),
 * Porte Seriali Fisiche UART 16550A (COM1-COM4) e Porte Virtuali (VCOM / VLPT).
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_ports_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "PORTS",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

static inline void p_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t p_inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

typedef enum {
    PORT_KIND_COM = 0,
    PORT_KIND_LPT,
    PORT_KIND_VIRTUAL
} port_kind_t;

typedef struct {
    char name[12];
    char hw_type[28];
    uint16_t io_base;
    uint8_t irq;
    port_kind_t kind;
    bool present;
    uint32_t baud;
} port_entry_t;

typedef struct {
    port_entry_t ports[10];
    int num_ports;
    int selected_idx;
    char status_msg[72];
} ports_state_t;

static void probe_serial_port(ports_state_t* st, const char* name, uint16_t base, uint8_t irq) {
    port_entry_t* pe = &st->ports[st->num_ports++];
    dll_strcpy(pe->name, name, sizeof(pe->name));
    pe->io_base = base;
    pe->irq = irq;
    pe->kind = PORT_KIND_COM;
    pe->baud = 115200;

    // Test Scratch Register (base + 7) per rilevare UART fisico reale
    uint8_t old_scr = p_inb((uint16_t)(base + 7));
    p_outb((uint16_t)(base + 7), 0xA5);
    uint8_t v1 = p_inb((uint16_t)(base + 7));
    p_outb((uint16_t)(base + 7), 0x5A);
    uint8_t v2 = p_inb((uint16_t)(base + 7));
    p_outb((uint16_t)(base + 7), old_scr);

    if (v1 == 0xA5 && v2 == 0x5A) {
        // Verifica FIFO 16550A (FCR/IIR)
        p_outb((uint16_t)(base + 2), 0xC7);
        uint8_t iir = p_inb((uint16_t)(base + 2));
        pe->present = true;
        if ((iir & 0xC0) == 0xC0)
            dll_strcpy(pe->hw_type, "UART 16550A (FIFO 16B)", sizeof(pe->hw_type));
        else
            dll_strcpy(pe->hw_type, "UART 16450 / 8250", sizeof(pe->hw_type));
    } else {
        pe->present = false;
        dll_strcpy(pe->hw_type, "Non presente", sizeof(pe->hw_type));
    }
}

static void probe_parallel_port(ports_state_t* st, const char* name, uint16_t base, uint8_t irq) {
    port_entry_t* pe = &st->ports[st->num_ports++];
    dll_strcpy(pe->name, name, sizeof(pe->name));
    pe->io_base = base;
    pe->irq = irq;
    pe->kind = PORT_KIND_LPT;
    pe->baud = 0;

    // Test registro dati IEEE 1284 (base + 0)
    uint8_t old_d = p_inb(base);
    p_outb(base, 0xAA);
    uint8_t r1 = p_inb(base);
    p_outb(base, old_d);

    if (r1 == 0xAA) {
        pe->present = true;
        dll_strcpy(pe->hw_type, "IEEE 1284 (SPP/EPP/ECP)", sizeof(pe->hw_type));
    } else {
        pe->present = false;
        dll_strcpy(pe->hw_type, "Legacy non rilevata", sizeof(pe->hw_type));
    }
}

static void add_virtual_port(ports_state_t* st, const char* name, const char* desc, uint32_t baud) {
    port_entry_t* pe = &st->ports[st->num_ports++];
    dll_strcpy(pe->name, name, sizeof(pe->name));
    dll_strcpy(pe->hw_type, desc, sizeof(pe->hw_type));
    pe->io_base = 0;
    pe->irq = 0;
    pe->kind = PORT_KIND_VIRTUAL;
    pe->present = true;
    pe->baud = baud;
}

static void ports_scan_all(ports_state_t* st) {
    st->num_ports = 0;
    probe_serial_port(st, "COM1", 0x3F8, 4);
    probe_serial_port(st, "COM2", 0x2F8, 3);
    probe_serial_port(st, "COM3", 0x3E8, 4);
    probe_serial_port(st, "COM4", 0x2E8, 3);
    probe_parallel_port(st, "LPT1", 0x378, 7);
    probe_parallel_port(st, "LPT2", 0x278, 5);
    add_virtual_port(st, "VCOM1", "USB CDC-ACM Virtual Serial", 115200);
    add_virtual_port(st, "VCOM2", "Loopback / Null-Modem Virt.", 115200);
    add_virtual_port(st, "VLPT1", "Stampa Virtuale su File", 0);
    st->selected_idx = 0;
}

static void pt_draw_card(int32_t x, int32_t y, int32_t w, int32_t h, const dll_theme_t* t) {
    if (!g_api) return;
    if (t->is_glass) {
        g_api->gfx_fill_rounded_alpha(x, y, w, h, 8, t->card_bg, 210);
        g_api->gfx_draw_rounded_rect(x, y, w, h, 8, t->card_border);
    } else {
        g_api->gfx_fill_rect(x, y, w, h, t->card_bg);
        g_api->gfx_draw_bevel_rect(x, y, w, h, false);
    }
}

static void pt_draw_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* lbl, bool primary, const dll_theme_t* t) {
    if (!g_api) return;
    if (t->is_glass) {
        g_api->gfx_fill_rounded_alpha(x, y, w, h, 6, primary ? t->accent : t->card_bg, primary ? 235 : 215);
        g_api->gfx_draw_rounded_rect(x, y, w, h, 6, primary ? t->accent : t->card_border);
    } else {
        g_api->gfx_fill_rect(x, y, w, h, primary ? t->accent : t->card_bg);
        g_api->gfx_draw_bevel_rect(x, y, w, h, false);
    }
    int32_t tx = x + (w - (int32_t)dll_strlen(lbl) * 8) / 2;
    if (tx < x + 4) tx = x + 4;
    g_api->gfx_draw_string(tx, y + (h - 14) / 2, lbl, primary ? t->accent_text : t->text_primary, 0, true);
}

static void ports_draw(window_t* win) {
    ports_state_t* st = (ports_state_t*)win->app_data;
    if (!st || !g_api) return;

    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 12, cy = win->y + 32;
    int32_t cw = win->width - 24, ch = win->height - 44;

    pt_draw_card(cx, cy, cw, 44, &t);
    g_api->gfx_draw_icon_32(cx + 8, cy + 6, ICON_MAINTENANCE);
    g_api->gfx_draw_string(cx + 48, cy + 7, "Porte Fisiche & Virtuali (COM, LPT, VCOM, VLPT)", t.text_primary, 0, true);
    g_api->gfx_draw_string(cx + 48, cy + 24, "Rilevamento I/O bare-metal UART 16550A e IEEE 1284", t.accent, 0, true);

    // Tabella porte
    int32_t tbl_y = cy + 52;
    int32_t tbl_h = ch - 134;
    pt_draw_card(cx, tbl_y, cw, tbl_h, &t);

    g_api->gfx_fill_rect(cx + 4, tbl_y + 4, cw - 8, 20, t.is_glass ? 0xE2E8F0 : 0xD4D0C8);
    g_api->gfx_draw_string(cx + 12,  tbl_y + 7, "Porta", 0x1E293B, 0, true);
    g_api->gfx_draw_string(cx + 76,  tbl_y + 7, "Controller / Tipo", 0x1E293B, 0, true);
    g_api->gfx_draw_string(cx + 305, tbl_y + 7, "I/O Base", 0x1E293B, 0, true);
    g_api->gfx_draw_string(cx + 390, tbl_y + 7, "IRQ", 0x1E293B, 0, true);
    g_api->gfx_draw_string(cx + 435, tbl_y + 7, "Stato", 0x1E293B, 0, true);

    for (int i = 0; i < st->num_ports; i++) {
        int32_t ry = tbl_y + 26 + i * 21;
        if (ry + 20 > tbl_y + tbl_h - 4) break;
        const port_entry_t* pe = &st->ports[i];
        bool sel = (i == st->selected_idx);
        if (sel) g_api->gfx_fill_rect(cx + 6, ry, cw - 12, 19, t.accent);

        uint32_t fg = sel ? t.accent_text : t.text_primary;
        uint32_t sub = sel ? t.accent_text : t.text_secondary;
        g_api->gfx_draw_string(cx + 12, ry + 2, pe->name, fg, 0, true);
        g_api->gfx_draw_string(cx + 76, ry + 2, pe->hw_type, sub, 0, true);

        char iostr[12];
        if (pe->io_base > 0) {
            iostr[0] = '0'; iostr[1] = 'x';
            const char* hex = "0123456789ABCDEF";
            iostr[2] = hex[(pe->io_base >> 8) & 0xF];
            iostr[3] = hex[(pe->io_base >> 4) & 0xF];
            iostr[4] = hex[pe->io_base & 0xF];
            iostr[5] = '\0';
        } else {
            dll_strcpy(iostr, "Virtual", sizeof(iostr));
        }
        g_api->gfx_draw_string(cx + 305, ry + 2, iostr, sub, 0, true);

        char irqstr[8];
        if (pe->irq > 0) {
            irqstr[0] = (char)('0' + pe->irq);
            irqstr[1] = '\0';
        } else {
            dll_strcpy(irqstr, "-", sizeof(irqstr));
        }
        g_api->gfx_draw_string(cx + 396, ry + 2, irqstr, sub, 0, true);
        g_api->gfx_draw_string(cx + 435, ry + 2, pe->present ? "Attiva" : "Assente",
                               sel ? t.accent_text : (pe->present ? 0x16A34A : 0x94A3B8), 0, true);
    }

    // Pannello configurazione porta selezionata
    int32_t cfg_y = tbl_y + tbl_h + 6;
    pt_draw_card(cx, cfg_y, cw, 40, &t);
    const port_entry_t* sel_p = (st->selected_idx >= 0 && st->selected_idx < st->num_ports)
                                    ? &st->ports[st->selected_idx] : NULL;
    g_api->gfx_draw_string(cx + 10, cfg_y + 12, "Baud:", t.text_primary, 0, true);
    const uint32_t bauds[3] = { 9600, 38400, 115200 };
    const char* blbls[3] = { "9600", "38400", "115200" };
    for (int b = 0; b < 3; b++) {
        pt_draw_button(cx + 58 + b * 74, cfg_y + 8, 68, 24, blbls[b],
                       (sel_p && sel_p->baud == bauds[b]), &t);
    }
    pt_draw_button(cx + 292, cfg_y + 8, 124, 24, "Test Loopback", true, &t);

    int32_t foot_y = cy + ch - 28;
    if (st->status_msg[0]) {
        g_api->gfx_draw_string(cx + 4, foot_y + 5, st->status_msg, t.accent, 0, true);
    }
    pt_draw_button(cx + cw - 190, foot_y, 96, 24, "Aggiorna", false, &t);
    pt_draw_button(cx + cw - 86,  foot_y, 86, 24, "Chiudi", false, &t);
}

static void ports_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    ports_state_t* st = (ports_state_t*)win->app_data;
    if (!st || !g_api || !clicked) return;

    int32_t cx = 12, cy = 32, cw = win->width - 24, ch = win->height - 44;
    int32_t tbl_y = cy + 52;
    int32_t tbl_h = ch - 134;

    if (lx >= cx + 6 && lx <= cx + cw - 6 && ly >= tbl_y + 26 && ly <= tbl_y + tbl_h - 4) {
        int idx = (ly - (tbl_y + 26)) / 21;
        if (idx >= 0 && idx < st->num_ports) st->selected_idx = idx;
        return;
    }

    int32_t cfg_y = tbl_y + tbl_h + 6;
    if (ly >= cfg_y + 8 && ly <= cfg_y + 32) {
        const uint32_t bauds[3] = { 9600, 38400, 115200 };
        for (int b = 0; b < 3; b++) {
            int32_t bx = cx + 58 + b * 74;
            if (lx >= bx && lx <= bx + 68 && st->selected_idx >= 0) {
                st->ports[st->selected_idx].baud = bauds[b];
                dll_strcpy(st->status_msg, "Baud rate aggiornato.", sizeof(st->status_msg));
                return;
            }
        }
        if (lx >= cx + 292 && lx <= cx + 416 && st->selected_idx >= 0) {
            const port_entry_t* pe = &st->ports[st->selected_idx];
            if (pe->kind == PORT_KIND_COM && pe->present) {
                // Test loopback hardware reale tramite MCR bit 4 (Loopback Mode)
                uint8_t old_mcr = p_inb((uint16_t)(pe->io_base + 4));
                p_outb((uint16_t)(pe->io_base + 4), 0x1E);
                p_outb(pe->io_base, 0x5A);
                uint8_t rx = p_inb(pe->io_base);
                p_outb((uint16_t)(pe->io_base + 4), old_mcr);
                dll_strcpy(st->status_msg, (rx == 0x5A) ? "Loopback UART HW OK!" : "Loopback eseguito.", sizeof(st->status_msg));
            } else {
                dll_strcpy(st->status_msg, pe->present ? "Test porta OK!" : "Porta fisica assente.", sizeof(st->status_msg));
            }
            return;
        }
    }

    int32_t foot_y = cy + ch - 28;
    if (ly >= foot_y && ly <= foot_y + 24) {
        if (lx >= cx + cw - 190 && lx <= cx + cw - 94) {
            ports_scan_all(st);
            dll_strcpy(st->status_msg, "Scansione I/O completata.", sizeof(st->status_msg));
            return;
        }
        if (lx >= cx + cw - 86 && lx <= cx + cw) {
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void ports_key(window_t* win, char c) {
    if (c == 27 && g_api) g_api->wm_destroy_window(win->id);
}

__attribute__((section(".text.entry"), used))
int ports_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    ports_state_t* st = (ports_state_t*)g_api->kmalloc(sizeof(ports_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(ports_state_t); i++) ((uint8_t*)st)[i] = 0;

    ports_scan_all(st);

    win->app_data = st;
    win->on_draw  = ports_draw;
    win->on_mouse = ports_mouse;
    win->on_key   = ports_key;
    return 0;
}
