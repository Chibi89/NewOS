#include <bsod.h>
#include <screen_modules.h>

__attribute__((section(".header"), used)) module_header_t g_bsod_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "BSOD",
    .signature = MODULE_AUTH_TAG
};

static int bsod_render_and_halt(const kernel_api_t* api, bsod_params_t* p);

// Entry point garantito all'offset 64 (subito dopo .header)
int module_main(const kernel_api_t* api, bsod_params_t* p) {
    return bsod_render_and_halt(api, p);
}

// ─── Helper I/O e Stringhe Zero-Libc ────────────────────────────────────────

static inline uint8_t io_inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_putc(char c) {
    while ((io_inb(0x3F8 + 5) & 0x20) == 0) { }
    io_outb(0x3F8, (uint8_t)c);
}

static void serial_puts(const char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static uint32_t str_len(const char* s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char* dst, const char* src, uint32_t max_len) {
    if (!dst || max_len == 0) return;
    uint32_t i = 0;
    while (src && src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void str_cat(char* dst, const char* src, uint32_t max_len) {
    uint32_t d = str_len(dst);
    uint32_t i = 0;
    if (!dst || d + 1 >= max_len) return;
    while (src && src[i] && d + i + 1 < max_len) {
        dst[d + i] = src[i];
        i++;
    }
    dst[d + i] = '\0';
}

static void fmt_hex64(uint64_t val, char* out, bool prefix) {
    const char* hex = "0123456789ABCDEF";
    int pos = 0;
    if (prefix) {
        out[pos++] = '0';
        out[pos++] = 'x';
    }
    for (int i = 0; i < 16; i++) {
        out[pos++] = hex[(val >> ((15 - i) * 4)) & 0xF];
    }
    out[pos] = '\0';
}

static void fmt_hex32(uint32_t val, char* out, bool prefix) {
    const char* hex = "0123456789ABCDEF";
    int pos = 0;
    if (prefix) {
        out[pos++] = '0';
        out[pos++] = 'x';
    }
    for (int i = 0; i < 8; i++) {
        out[pos++] = hex[(val >> ((7 - i) * 4)) & 0xF];
    }
    out[pos] = '\0';
}

static void fmt_uint(uint64_t val, char* out) {
    char tmp[24];
    int i = 0;
    if (val == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (val > 0 && i < 22) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

// ─── Tabella Eccezioni CPU x86_64 ───────────────────────────────────────────

typedef struct {
    const char* stop_code;
    const char* desc;
} exc_info_t;

static exc_info_t get_exc_info(uint64_t int_no) {
    switch (int_no) {
        case 0:  return (exc_info_t){"DIVIDE_BY_ZERO (#DE)", "Divisione per zero o overflow quoziente"};
        case 1:  return (exc_info_t){"DEBUG_TRAP (#DB)", "Eccezione di debug hardware non gestita"};
        case 2:  return (exc_info_t){"NMI_INTERRUPT", "Interrupt hardware non mascherabile (NMI)"};
        case 3:  return (exc_info_t){"BREAKPOINT (#BP)", "Istruzione INT3 di breakpoint non gestita"};
        case 4:  return (exc_info_t){"OVERFLOW (#OF)", "Overflow aritmetico istruzione INTO"};
        case 5:  return (exc_info_t){"BOUND_RANGE (#BR)", "Indice fuori dai limiti consentiti"};
        case 6:  return (exc_info_t){"INVALID_OPCODE (#UD)", "Istruzione CPU non valida o corrotta"};
        case 7:  return (exc_info_t){"DEVICE_NOT_AVAILABLE (#NM)", "Coprocessore matematico FPU/SSE non disponibile"};
        case 8:  return (exc_info_t){"DOUBLE_FAULT (#DF)", "Doppio errore critico durante gestione eccezione"};
        case 10: return (exc_info_t){"INVALID_TSS (#TS)", "Task State Segment non valido"};
        case 11: return (exc_info_t){"SEGMENT_NOT_PRESENT (#NP)", "Segmento di memoria non presente"};
        case 12: return (exc_info_t){"STACK_SEGMENT_FAULT (#SS)", "Violazione o overflow dello stack di sistema"};
        case 13: return (exc_info_t){"GENERAL_PROTECTION_FAULT (#GP)", "Violazione privilegi o puntatore non canonico"};
        case 14: return (exc_info_t){"PAGE_FAULT (#PF)", "Accesso a pagina virtuale non mappata o protetta"};
        case 16: return (exc_info_t){"X87_FP_ERROR (#MF)", "Errore hardware unita' floating point x87"};
        case 17: return (exc_info_t){"ALIGNMENT_CHECK (#AC)", "Accesso memoria non allineato"};
        case 18: return (exc_info_t){"MACHINE_CHECK (#MC)", "Errore hardware critico rilevato dalla CPU"};
        case 19: return (exc_info_t){"SIMD_FP_EXCEPTION (#XM)", "Eccezione istruzioni SSE/AVX floating point"};
        default: return (exc_info_t){"UNHANDLED_CPU_EXCEPTION", "Eccezione hardware CPU imprevista"};
    }
}

// ─── Risoluzione Simboli KERNEL.SYM ('NSYM') ────────────────────────────────

typedef struct {
    bool        found;
    const char* file;
    const char* func;
    uint32_t    line;
    uint64_t    offset;
} sym_loc_t;

static sym_loc_t resolve_symbol(const void* sym_data, uint32_t sym_size, uint64_t addr) {
    sym_loc_t res = {false, NULL, NULL, 0, 0};
    if (!sym_data || sym_size < sizeof(ksym_header_t)) return res;

    const uint8_t* base = (const uint8_t*)sym_data;
    const ksym_header_t* hdr = (const ksym_header_t*)base;
    if (hdr->magic != 0x4D59534E || hdr->count == 0) return res;

    uint32_t table_bytes = hdr->count * (uint32_t)sizeof(ksym_entry_t);
    if (sizeof(ksym_header_t) + table_bytes > sym_size) return res;
    if (hdr->str_offset + hdr->str_size > sym_size) return res;

    const ksym_entry_t* entries = (const ksym_entry_t*)(base + sizeof(ksym_header_t));
    const char* str_pool = (const char*)(base + hdr->str_offset);

    if (addr < entries[0].addr) return res;

    uint32_t lo = 0;
    uint32_t hi = hdr->count - 1;
    uint32_t best = 0;

    while (lo <= hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        if (entries[mid].addr <= addr) {
            best = mid;
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }

    // Limite ragionevole (entro 4 KB dall'ultima riga nota)
    if (addr - entries[best].addr > 4096) return res;

    res.found  = true;
    res.line   = entries[best].line;
    res.offset = addr - entries[best].addr;
    if (entries[best].file_off < hdr->str_size) res.file = str_pool + entries[best].file_off;
    if (entries[best].func_off < hdr->str_size) res.func = str_pool + entries[best].func_off;
    return res;
}

static const char* path_basename(const char* path) {
    if (!path) return "?";
    const char* last = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

// ─── Helper Rendering Liquid Glass & Anti-Overflow Testi ─────────────────────

static void draw_text_fit(const kernel_api_t* api, int32_t x, int32_t y,
                          int32_t max_w, uint32_t fw, const char* text,
                          uint32_t color, bool tail_keep) {
    if (!text || max_w <= (int32_t)fw) return;
    uint32_t max_chars = (uint32_t)(max_w / (int32_t)fw);
    uint32_t len = str_len(text);
    if (len <= max_chars) {
        api->gfx_draw_string(x, y, text, color, 0, true);
        return;
    }
    char buf[160];
    if (max_chars >= sizeof(buf)) max_chars = sizeof(buf) - 1;
    if (max_chars <= 3) return;

    if (tail_keep) {
        // Mantiene la parte finale (ideale per path lunghi: ".../apps/cmd.c")
        buf[0] = '.';
        buf[1] = '.';
        buf[2] = '.';
        uint32_t keep = max_chars - 3;
        str_copy(buf + 3, text + (len - keep), sizeof(buf) - 3);
    } else {
        uint32_t keep = max_chars - 3;
        for (uint32_t i = 0; i < keep; i++) buf[i] = text[i];
        buf[keep] = '.';
        buf[keep + 1] = '.';
        buf[keep + 2] = '.';
        buf[keep + 3] = '\0';
    }
    api->gfx_draw_string(x, y, buf, color, 0, true);
}

static uint32_t blend_rgb(uint32_t bg, uint32_t fg, uint8_t alpha) {
    uint32_t inv = 255 - alpha;
    uint32_t r = (((bg >> 16) & 0xFF) * inv + ((fg >> 16) & 0xFF) * alpha) / 255;
    uint32_t g = (((bg >> 8) & 0xFF) * inv + ((fg >> 8) & 0xFF) * alpha) / 255;
    uint32_t b = ((bg & 0xFF) * inv + (fg & 0xFF) * alpha) / 255;
    return (r << 16) | (g << 8) | b;
}

static bool inside_rounded(int32_t px, int32_t py, int32_t w, int32_t h, int32_t r) {
    if (r <= 0) return true;
    int32_t cx = -1, cy = -1;
    if (px < r && py < r) { cx = r; cy = r; }
    else if (px >= w - r && py < r) { cx = w - r - 1; cy = r; }
    else if (px < r && py >= h - r) { cx = r; cy = h - r - 1; }
    else if (px >= w - r && py >= h - r) { cx = w - r - 1; cy = h - r - 1; }
    if (cx < 0) return true;
    int32_t dx = px - cx;
    int32_t dy = py - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

static void draw_glass_panel(const kernel_api_t* api, const bsod_params_t* p,
                             int32_t x, int32_t y, int32_t w, int32_t h,
                             int32_t radius, uint32_t tint, uint8_t alpha,
                             uint32_t border_col) {
    if (p->login_bg_pixels && p->login_bg_w > 0 && p->login_bg_h > 0 && api->gfx_draw_pixel) {
        int32_t sw = (int32_t)api->gfx_get_width();
        int32_t sh = (int32_t)api->gfx_get_height();
        if (sw <= 0) sw = 1024;
        if (sh <= 0) sh = 768;
        for (int32_t py = 0; py < h; py++) {
            int32_t sy = y + py;
            if (sy < 0 || sy >= sh) continue;
            int32_t src_y = (sy * p->login_bg_h) / sh;
            for (int32_t px = 0; px < w; px++) {
                int32_t sx = x + px;
                if (sx < 0 || sx >= sw) continue;
                if (!inside_rounded(px, py, w, h, radius)) continue;
                int32_t src_x = (sx * p->login_bg_w) / sw;
                uint32_t bg_px = p->login_bg_pixels[src_y * p->login_bg_w + src_x];
                // Velo Liquid Glass traslucido sopra l'immagine nitida di login
                uint32_t out_px = blend_rgb(bg_px, tint, (uint8_t)(alpha > 110 ? 110 : alpha));
                api->gfx_draw_pixel(sx, sy, out_px);
            }
        }
    } else if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(x, y, w, h, radius, tint, alpha);
    } else {
        api->gfx_fill_rounded_rect(x, y, w, h, radius, 0x001E54);
    }
    if (p->gfx_draw_specular_rim) {
        p->gfx_draw_specular_rim(x, y, w, h, radius);
    }
    api->gfx_draw_rounded_rect(x, y, w, h, radius, border_col);
}

static void draw_sad_badge(const kernel_api_t* api, const bsod_params_t* p,
                           int32_t bx, int32_t by, int32_t bsz) {
    draw_glass_panel(api, p, bx, by, bsz, bsz, 16, 0xFFFFFF, 46, 0x99D9FF);

    // Occhi ":" e arco triste "(" centrati nel badge
    int32_t cx = bx + bsz / 2;
    int32_t cy = by + bsz / 2;
    api->gfx_fill_rounded_rect(cx - 14, cy - 15, 6, 10, 2, 0xFFFFFF);
    api->gfx_fill_rounded_rect(cx - 14, cy + 5, 6, 10, 2, 0xFFFFFF);

    // Arco verticale curvo per "(" (x curvato a sinistra)
    for (int dy = -20; dy <= 20; dy++) {
        int32_t curve = (dy * dy) / 48; // 0 al centro, +8 agli estremi
        api->gfx_fill_rect(cx + 2 + curve, cy + dy, 4, 2, 0xFFFFFF);
    }
}

static bool is_valid_stack_ptr(uint64_t addr) {
    if (addr == 0 || (addr & 0x7) != 0) return false;
    if (addr >= 0x100000ULL && addr < 0x80000000ULL) return true;
    if (addr >= 0xFFFF800000000000ULL && addr < 0xFFFFFFFFFFFFF000ULL) return true;
    return false;
}

static void draw_reg_pair(const kernel_api_t* api, int32_t x, int32_t y,
                          int32_t max_w, uint32_t fw, const char* name,
                          uint64_t val, uint32_t lbl_col, uint32_t val_col) {
    char hex[20];
    fmt_hex64(val, hex, false); // 16 char esatti (piu compatto e leggibile)
    int32_t name_len = (int32_t)str_len(name);
    api->gfx_draw_string(x, y, name, lbl_col, 0, true);
    int32_t vx = x + (name_len + 1) * (int32_t)fw;
    draw_text_fit(api, vx, y, max_w - (vx - x), fw, hex, val_col, false);
}

// ─── Main BSOD Renderer & Halt Loop ──────────────────────────────────────────

static int bsod_render_and_halt(const kernel_api_t* api, bsod_params_t* p) {
    if (!api || !p) return -1;

    uint32_t sw = api->gfx_get_width();
    uint32_t sh = api->gfx_get_height();
    if (sw < 640) sw = 1024;
    if (sh < 480) sh = 768;

    uint32_t fw = p->gfx_get_font_w ? p->gfx_get_font_w() : 8;
    uint32_t fh = p->gfx_get_font_h ? p->gfx_get_font_h() : 16;
    if (fw == 0) fw = 8;
    if (fh == 0) fh = 16;

    // 1. Risoluzione stop code, descrizione e origine file:riga
    exc_info_t einfo = get_exc_info(p->int_no);
    const char* stop_str = p->stop_code ? p->stop_code : einfo.stop_code;
    const char* desc_str = p->message ? p->message : einfo.desc;

    sym_loc_t rip_loc = resolve_symbol(p->sym_data, p->sym_size, p->rip);
    sym_loc_t caller_loc = {false, NULL, NULL, 0, 0};
    bool from_caller = false;
    if (!rip_loc.found && !p->file) {
        uint64_t rbp_scan = p->rbp;
        for (int depth = 0; depth < 6 && is_valid_stack_ptr(rbp_scan); depth++) {
            const uint64_t* fp = (const uint64_t*)rbp_scan;
            uint64_t next_rbp = fp[0];
            uint64_t ret_addr = fp[1];
            if (ret_addr == 0) break;
            sym_loc_t cand = resolve_symbol(p->sym_data, p->sym_size, ret_addr);
            if (cand.found && cand.file) {
                caller_loc = cand;
                from_caller = true;
                break;
            }
            if (next_rbp <= rbp_scan) break;
            rbp_scan = next_rbp;
        }
    }
    const char* orig_file = p->file ? p->file
                          : (rip_loc.found && rip_loc.file) ? rip_loc.file
                          : (from_caller && caller_loc.file) ? caller_loc.file
                          : "Modulo Out-of-Kernel (.DLL)";
    uint32_t orig_line    = (p->line > 0) ? p->line
                          : rip_loc.found ? rip_loc.line
                          : from_caller ? caller_loc.line : 0;
    const char* orig_func = (rip_loc.found && rip_loc.func) ? rip_loc.func
                          : (from_caller && caller_loc.func) ? caller_loc.func
                          : "kernel_code";

    // 2. Sfondo Liquid Glass blu zaffiro
    api->gfx_draw_gradient_v(0, 0, (int32_t)sw, (int32_t)sh, 0x001B4D, 0x00419E);
    // Riflesso diagonale morbido di sfondo
    if (p->gfx_fill_alpha) {
        p->gfx_fill_alpha(0, 0, (int32_t)sw, (int32_t)(sh / 3), 0x3B82F6, 22);
    }

    // 3. Top Header Bar
    int32_t hdr_h = 36;
    if (p->gfx_fill_alpha) {
        p->gfx_fill_alpha(0, 0, (int32_t)sw, hdr_h, 0x000F2E, 200);
    } else {
        api->gfx_fill_rect(0, 0, (int32_t)sw, hdr_h, 0x00143A);
    }
    api->gfx_fill_rect(0, hdr_h - 1, (int32_t)sw, 1, 0x4DA3FF);

    int32_t hdr_ty = (hdr_h - (int32_t)fh) / 2;
    draw_text_fit(api, 20, hdr_ty, (int32_t)(sw / 2), fw,
                  "NewOS V1 (64-bit) - Fatal Error (BSOD.DLL)", 0xFFFFFF, false);

    char right_hdr[48];
    char int_hex[12];
    fmt_hex32((uint32_t)p->int_no, int_hex, true);
    str_copy(right_hdr, "ARCH: x86_64 | STOP: ", sizeof(right_hdr));
    str_cat(right_hdr, int_hex, sizeof(right_hdr));
    int32_t rh_w = (int32_t)(str_len(right_hdr) * fw);
    int32_t rh_x = (int32_t)sw - rh_w - 20;
    if (rh_x < (int32_t)(sw / 2) + 8) rh_x = (int32_t)(sw / 2) + 8;
    draw_text_fit(api, rh_x, hdr_ty, (int32_t)sw - rh_x - 16, fw, right_hdr, 0x99D9FF, false);

    // 4. Hero Block (Badge :( + Titolo + Descrizione)
    int32_t margin = 28;
    int32_t hero_y = hdr_h + 14;
    int32_t badge_sz = 68;
    draw_sad_badge(api, p, margin, hero_y, badge_sz);

    int32_t htext_x = margin + badge_sz + 18;
    int32_t htext_w = (int32_t)sw - htext_x - margin;
    draw_text_fit(api, htext_x, hero_y + 4, htext_w, fw,
                  "Il sistema ha rilevato un errore critico ed e' stato arrestato.",
                  0xFFFFFF, false);

    char exc_line[160];
    str_copy(exc_line, "STOP: ", sizeof(exc_line));
    str_cat(exc_line, stop_str, sizeof(exc_line));
    str_cat(exc_line, " - ", sizeof(exc_line));
    str_cat(exc_line, desc_str, sizeof(exc_line));
    draw_text_fit(api, htext_x, hero_y + 4 + (int32_t)fh + 6, htext_w, fw,
                  exc_line, 0x99D9FF, false);

    // Eventuale decodifica Page Fault (#PF) sulla 3a riga del blocco Hero
    if (p->int_no == 14) {
        char pf_line[96];
        str_copy(pf_line, "Dettaglio #PF: ", sizeof(pf_line));
        str_cat(pf_line, (p->err_code & 1) ? "Protection" : "Not-Present", sizeof(pf_line));
        str_cat(pf_line, (p->err_code & 2) ? " | WRITE" : " | READ", sizeof(pf_line));
        str_cat(pf_line, (p->err_code & 4) ? " | USER" : " | KERNEL", sizeof(pf_line));
        if (p->err_code & 16) str_cat(pf_line, " | EXEC", sizeof(pf_line));
        draw_text_fit(api, htext_x, hero_y + 4 + (int32_t)fh * 2 + 10, htext_w, fw,
                      pf_line, 0xFCD34D, false);
    }

    // 5. Banner Evidenziato ORIGINE (File + Riga + Funzione)
    int32_t pill_y = hero_y + badge_sz + 14;
    int32_t pill_h = 34;
    int32_t pill_w = (int32_t)sw - margin * 2;
    draw_glass_panel(api, p, margin, pill_y, pill_w, pill_h, 10, 0x001438, 170, 0xFBBF24);

    char line_num_str[16];
    char off_str[12];
    if (orig_line > 0) fmt_uint(orig_line, line_num_str);
    else str_copy(line_num_str, "?", sizeof(line_num_str));
    fmt_hex32((uint32_t)rip_loc.offset, off_str, true);

    char orig_banner[180];
    str_copy(orig_banner, "ORIGINE: ", sizeof(orig_banner));
    str_cat(orig_banner, orig_file, sizeof(orig_banner));
    str_cat(orig_banner, "  |  RIGA: ", sizeof(orig_banner));
    str_cat(orig_banner, line_num_str, sizeof(orig_banner));
    if (rip_loc.found || from_caller) {
        str_cat(orig_banner, from_caller ? "  |  CALLER: " : "  |  FUNZ: ", sizeof(orig_banner));
        str_cat(orig_banner, orig_func, sizeof(orig_banner));
        if (rip_loc.found) {
            str_cat(orig_banner, "+", sizeof(orig_banner));
            str_cat(orig_banner, off_str, sizeof(orig_banner));
        }
    }
    draw_text_fit(api, margin + 14, pill_y + (pill_h - (int32_t)fh) / 2,
                  pill_w - 28, fw, orig_banner, 0xFDE68A, false);

    // 6. Due Card Liquid Glass Affiancate (calcolate con margini anti-overflow)
    int32_t footer_h = 46;
    int32_t cards_y  = pill_y + pill_h + 14;
    int32_t cards_h  = (int32_t)sh - cards_y - footer_h - 14;
    int32_t gap      = 16;
    int32_t card_w   = (pill_w - gap) / 2;
    int32_t left_x   = margin;
    int32_t right_x  = margin + card_w + gap;

    // ── Card Sinistra: REGISTRI CPU & CONTROLLO ──
    draw_glass_panel(api, p, left_x, cards_y, card_w, cards_h, 12, 0x001845, 150, 0x60A5FA);
    if (p->gfx_fill_alpha) {
        p->gfx_fill_alpha(left_x + 1, cards_y + 1, card_w - 2, 28, 0xFFFFFF, 26);
    }
    api->gfx_fill_rect(left_x + 1, cards_y + 29, card_w - 2, 1, 0x3B82F6);
    draw_text_fit(api, left_x + 14, cards_y + 7, card_w - 28, fw,
                  "REGISTRI CPU & CONTROLLO", 0x99D9FF, false);

    int32_t pad_x = 14;
    int32_t col_w = (card_w - pad_x * 2 - 12) / 2;
    int32_t c1_x  = left_x + pad_x;
    int32_t c2_x  = left_x + pad_x + col_w + 12;
    int32_t ry    = cards_y + 40;
    int32_t row_step = (cards_h - 50) / 12;
    if (row_step < (int32_t)fh + 2) row_step = (int32_t)fh + 2;
    if (row_step > 24) row_step = 24;

    draw_reg_pair(api, c1_x, ry, col_w, fw, "RIP", p->rip, 0x99D9FF, 0xFFFFFF);
    draw_reg_pair(api, c2_x, ry, col_w, fw, "RSP", p->rsp, 0x99D9FF, 0xFFFFFF);
    ry += row_step;
    draw_reg_pair(api, c1_x, ry, col_w, fw, "RBP", p->rbp, 0x99D9FF, 0xFFFFFF);
    draw_reg_pair(api, c2_x, ry, col_w, fw, "CR2", p->cr2, 0xFCD34D, 0xFDE68A);
    ry += row_step;
    draw_reg_pair(api, c1_x, ry, col_w, fw, "CR3", p->cr3, 0x99D9FF, 0xFFFFFF);
    draw_reg_pair(api, c2_x, ry, col_w, fw, "ERR", p->err_code, 0x99D9FF, 0xFFFFFF);
    ry += row_step + 4;

    api->gfx_fill_rect(left_x + 14, ry, card_w - 28, 1, 0x2563EB);
    ry += 8;

    const char* gpr_l_names[8] = {"RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "R8 ", "R9 "};
    uint64_t    gpr_l_vals[8]  = {p->rax, p->rbx, p->rcx, p->rdx, p->rsi, p->rdi, p->r8, p->r9};
    const char* gpr_r_names[8] = {"R10", "R11", "R12", "R13", "R14", "R15", "CS ", "FLG"};
    uint64_t    gpr_r_vals[8]  = {p->r10, p->r11, p->r12, p->r13, p->r14, p->r15, p->cs, p->rflags};

    int32_t max_ry = cards_y + cards_h - (int32_t)fh - 6;
    for (int i = 0; i < 8 && ry <= max_ry; i++) {
        draw_reg_pair(api, c1_x, ry, col_w, fw, gpr_l_names[i], gpr_l_vals[i], 0x93C5FD, 0xE2E8F0);
        draw_reg_pair(api, c2_x, ry, col_w, fw, gpr_r_names[i], gpr_r_vals[i], 0x93C5FD, 0xE2E8F0);
        ry += row_step;
    }

    // ── Card Destra: STACK BACKTRACE & SORGENTI ──
    draw_glass_panel(api, p, right_x, cards_y, card_w, cards_h, 12, 0x001845, 150, 0x60A5FA);
    if (p->gfx_fill_alpha) {
        p->gfx_fill_alpha(right_x + 1, cards_y + 1, card_w - 2, 28, 0xFFFFFF, 26);
    }
    api->gfx_fill_rect(right_x + 1, cards_y + 29, card_w - 2, 1, 0x3B82F6);
    draw_text_fit(api, right_x + 14, cards_y + 7, card_w - 28, fw,
                  "STACK BACKTRACE & SORGENTI", 0x99D9FF, false);

    int32_t bt_x = right_x + 14;
    int32_t bt_w = card_w - 28;
    int32_t bt_y = cards_y + 40;
    int32_t bt_footer_y = cards_y + cards_h - 32;

    // Frame #0 (RIP corrente) + risalita catena RBP
    uint64_t frames[6];
    int      frame_count = 0;
    frames[frame_count++] = p->rip;

    uint64_t cur_rbp = p->rbp;
    while (frame_count < 6 && is_valid_stack_ptr(cur_rbp)) {
        const uint64_t* fp = (const uint64_t*)cur_rbp;
        uint64_t next_rbp = fp[0];
        uint64_t ret_addr = fp[1];
        if (ret_addr == 0) break;
        frames[frame_count++] = ret_addr;
        if (next_rbp <= cur_rbp) break;
        cur_rbp = next_rbp;
    }

    serial_puts("STACK BACKTRACE:\n");
    for (int f = 0; f < frame_count; f++) {
        if (bt_y + (int32_t)(fh * 2 + 4) > bt_footer_y) break;

        sym_loc_t loc = resolve_symbol(p->sym_data, p->sym_size, frames[f]);
        char addr_hex[20];
        fmt_hex64(frames[f], addr_hex, true);

        char line1[128];
        line1[0] = '#';
        line1[1] = (char)('0' + f);
        line1[2] = ' ';
        line1[3] = '\0';
        str_cat(line1, addr_hex, sizeof(line1));
        str_cat(line1, " ", sizeof(line1));
        str_cat(line1, (loc.found && loc.func) ? loc.func : "<unknown>", sizeof(line1));

        char line2[128];
        if (loc.found && loc.file) {
            char lnum[12];
            fmt_uint(loc.line, lnum);
            str_copy(line2, "   -> ", sizeof(line2));
            str_cat(line2, path_basename(loc.file), sizeof(line2));
            str_cat(line2, ":", sizeof(line2));
            str_cat(line2, lnum, sizeof(line2));
        } else {
            str_copy(line2, "   -> (simbolo non disponibile)", sizeof(line2));
        }

        if (f == 0 && p->gfx_fill_alpha) {
            p->gfx_fill_alpha(bt_x - 4, bt_y - 2, bt_w + 8, (int32_t)(fh * 2 + 4), 0x3B82F6, 48);
        }
        draw_text_fit(api, bt_x, bt_y, bt_w, fw, line1, (f == 0) ? 0xFFFFFF : 0xE2E8F0, false);
        draw_text_fit(api, bt_x, bt_y + (int32_t)fh + 2, bt_w, fw, line2, (f == 0) ? 0xFDE68A : 0x93C5FD, true);
        bt_y += (int32_t)(fh * 2 + 10);

        serial_puts("  ");
        serial_puts(line1);
        serial_puts(" ");
        serial_puts(line2);
        serial_puts("\n");
    }

    // Footer interno Card Destra
    api->gfx_fill_rect(right_x + 14, bt_footer_y, card_w - 28, 1, 0x2563EB);
    draw_text_fit(api, right_x + 14, bt_footer_y + 8, card_w - 28, fw,
                  p->sym_data ? "Simboli: KERNEL.SYM (RAM) | COM1: INVIATO"
                              : "Simboli: Pre-Boot | Seriale COM1: INVIATO",
                  0x99D9FF, false);

    // 7. Barra Azioni Inferiore
    int32_t bar_y = (int32_t)sh - footer_h;
    if (p->gfx_fill_alpha) {
        p->gfx_fill_alpha(0, bar_y, (int32_t)sw, footer_h, 0x000F2E, 200);
    } else {
        api->gfx_fill_rect(0, bar_y, (int32_t)sw, footer_h, 0x00143A);
    }
    api->gfx_fill_rect(0, bar_y, (int32_t)sw, 1, 0x4DA3FF);

    int32_t btn_y = bar_y + 8;
    int32_t btn_h = footer_h - 16;
    draw_glass_panel(api, p, margin, btn_y, 244, btn_h, 8, 0xFFFFFF, 36, 0x99D9FF);
    draw_text_fit(api, margin + 12, btn_y + (btn_h - (int32_t)fh) / 2, 220, fw,
                  "[INVIO / R] Riavvia PC", 0xFFFFFF, false);

    draw_glass_panel(api, p, margin + 258, btn_y, 176, btn_h, 8, 0xFFFFFF, 28, 0x60A5FA);
    draw_text_fit(api, margin + 270, btn_y + (btn_h - (int32_t)fh) / 2, 152, fw,
                  "[S] Spegni", 0xE2E8F0, false);

    const char* guard = "NewOS V1 Liquid Glass Guard";
    int32_t gw = (int32_t)(str_len(guard) * fw);
    int32_t gx = (int32_t)sw - gw - margin;
    if (gx > margin + 448) {
        api->gfx_draw_string(gx, btn_y + (btn_h - (int32_t)fh) / 2, guard, 0x99D9FF, 0, true);
    }

    if (p->gfx_flip) {
        p->gfx_flip();
    }

    // 8. Polling diretto controller tastiera (interrupt disabilitati)
    for (;;) {
        if (io_inb(0x64) & 1) {
            uint8_t sc = io_inb(0x60);
            if (sc == 0x1C || sc == 0x13) { // INVIO o R
                if (api->power_reboot) api->power_reboot();
            } else if (sc == 0x1F) { // S
                if (api->power_shutdown) api->power_shutdown();
            }
        }
        asm volatile("pause");
    }

    return 0;
}
