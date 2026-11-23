/**
 * SystemLibrary/nsrt.c
 * NewSoft Runtime (NSRT.DLL) — Implementazione Core Out-of-Kernel v2.0
 * Fornisce memoria, stringhe/snprintf freestanding, I/O sicuro (ACL),
 * logging diagnostico, audio, rete, registro, dialoghi COMDLG, grafica 2D
 * e ciclo event-driven basato su XUIML.DLL.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include "xuiml.h"
#include "nsrt.h"
#include <kernel_ext_api.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_nsrt_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "NSRT",
    .signature      = MODULE_AUTH_TAG
};
#endif

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };
static const kernel_api_t* g_api = NULL;
static const xuiml_api_t*  g_xapi = NULL;

static void nsrt_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_nsrt_header;
    const uint32_t* tbl = (const uint32_t*)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t*)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

#define NSRT_MAX_LOGS 8
static char g_nsrt_logs[NSRT_MAX_LOGS][56];
static int  g_nsrt_log_count = 0;

struct nsrt_app {
    window_t*          win;
    const xuiml_api_t* xapi;
    xuiml_doc_t*       doc;
    nsrt_event_cb      on_event;
    void*              user_ctx;
};

/* --- Helper stringa e ACL --- */
static char n_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool n_istarts_with(const char* s, const char* pfx) {
    if (!s || !pfx) return false;
    while (*pfx) {
        if (n_tolower(*s) != n_tolower(*pfx)) return false;
        s++; pfx++;
    }
    return true;
}

static bool nsrt_is_protected_path(const char* path) {
    if (!path) return true;
    return n_istarts_with(path, "C:\\NewOS\\") ||
           n_istarts_with(path, "C:/NewOS/")  ||
           n_istarts_with(path, "/NewOS/");
}

static bool nsrt_is_reg_writable_sandbox(const char* path) {
    if (!path) return false;
    return n_istarts_with(path, "HKLM\\Software\\NewSoft\\") ||
           n_istarts_with(path, "HKCU\\Software\\") ||
           n_istarts_with(path, "HKCU\\ControlPanel\\") ||
           n_istarts_with(path, "HKCU\\Environment\\");
}

/* --- Memoria & Stringhe Freestanding --- */
static void* nsrt_alloc(size_t size) {
    return (g_api && g_api->kmalloc && size > 0) ? g_api->kmalloc(size) : NULL;
}

static void* nsrt_calloc(size_t count, size_t elem_size) {
    size_t tot = count * elem_size;
    volatile uint8_t* p = (volatile uint8_t*)nsrt_alloc(tot);
    if (p) {
        for (size_t i = 0; i < tot; i++) p[i] = 0;
    }
    return (void*)p;
}

static void* nsrt_memcpy(void* dst, const void* src, size_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (!d || !s) return dst;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void* nsrt_memset(void* dst, int c, size_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    if (!d) return dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

static int nsrt_memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static void nsrt_free(void* ptr) {
    if (ptr && g_api && g_api->kfree) g_api->kfree(ptr);
}

static void* nsrt_realloc(void* ptr, size_t old_size, size_t new_size) {
    if (!ptr) return nsrt_alloc(new_size);
    if (new_size == 0) { nsrt_free(ptr); return NULL; }
    void* nptr = nsrt_alloc(new_size);
    if (!nptr) return NULL;
    size_t copy_sz = old_size < new_size ? old_size : new_size;
    nsrt_memcpy(nptr, ptr, copy_sz);
    nsrt_free(ptr);
    return nptr;
}

static size_t nsrt_strlen(const char* s) {
    size_t len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

static int nsrt_strcmp(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int nsrt_strncmp(const char* a, const char* b, size_t n) {
    if (!a) a = "";
    if (!b) b = "";
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i] || !b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

static char* nsrt_strcpy(char* dst, const char* src) {
    if (!dst) return NULL;
    char* ret = dst;
    if (src) {
        while (*src) *dst++ = *src++;
    }
    *dst = '\0';
    return ret;
}

static char* nsrt_strncpy(char* dst, const char* src, size_t n) {
    if (!dst || n == 0) return dst;
    size_t i = 0;
    if (src) {
        for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
    return dst;
}

static int nsrt_snprintf(char* buf, size_t size, const char* fmt, ...) {
    if (!buf || size == 0 || !fmt) return 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    size_t pos = 0;
    size_t lim = size - 1;

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            if (pos < lim) buf[pos] = fmt[i];
            pos++;
            continue;
        }
        i++;
        if (!fmt[i]) break;
        if (fmt[i] == '%') {
            if (pos < lim) buf[pos] = '%';
            pos++;
        } else if (fmt[i] == 'c') {
            char c = (char)__builtin_va_arg(ap, int);
            if (pos < lim) buf[pos] = c;
            pos++;
        } else if (fmt[i] == 's') {
            const char* s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s) {
                if (pos < lim) buf[pos] = *s;
                pos++; s++;
            }
        } else if (fmt[i] == 'd' || fmt[i] == 'i') {
            int64_t v = (int64_t)__builtin_va_arg(ap, int);
            if (v < 0) {
                if (pos < lim) buf[pos] = '-';
                pos++;
                v = -v;
            }
            char tmp[24];
            int ti = 0;
            uint64_t uv = (uint64_t)v;
            if (uv == 0) tmp[ti++] = '0';
            while (uv > 0 && ti < 23) { tmp[ti++] = (char)('0' + (uv % 10)); uv /= 10; }
            while (ti > 0) {
                ti--;
                if (pos < lim) buf[pos] = tmp[ti];
                pos++;
            }
        } else if (fmt[i] == 'u') {
            uint64_t uv = (uint64_t)__builtin_va_arg(ap, unsigned int);
            char tmp[24];
            int ti = 0;
            if (uv == 0) tmp[ti++] = '0';
            while (uv > 0 && ti < 23) { tmp[ti++] = (char)('0' + (uv % 10)); uv /= 10; }
            while (ti > 0) {
                ti--;
                if (pos < lim) buf[pos] = tmp[ti];
                pos++;
            }
        } else if (fmt[i] == 'x' || fmt[i] == 'X' || fmt[i] == 'p') {
            uint64_t uv = (fmt[i] == 'p')
                ? (uint64_t)(uintptr_t)__builtin_va_arg(ap, void*)
                : (uint64_t)__builtin_va_arg(ap, unsigned int);
            const char* digits = (fmt[i] == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[24];
            int ti = 0;
            if (fmt[i] == 'p') {
                if (pos < lim) buf[pos] = '0'; pos++;
                if (pos < lim) buf[pos] = 'x'; pos++;
            }
            if (uv == 0) tmp[ti++] = '0';
            while (uv > 0 && ti < 23) { tmp[ti++] = digits[uv & 0xF]; uv >>= 4; }
            while (ti > 0) {
                ti--;
                if (pos < lim) buf[pos] = tmp[ti];
                pos++;
            }
        } else {
            if (pos < lim) buf[pos] = fmt[i];
            pos++;
        }
    }
    buf[pos < lim ? pos : lim] = '\0';
    __builtin_va_end(ap);
    return (int)pos;
}

/* --- Logging & Diagnostica --- */
static void nsrt_log(const char* tag, const char* msg) {
    char line[56];
    size_t p = 0;
    line[p++] = '[';
    if (tag) {
        for (int i = 0; tag[i] && p < 16; i++) line[p++] = tag[i];
    }
    line[p++] = ']';
    line[p++] = ' ';
    if (msg) {
        for (int i = 0; msg[i] && p < sizeof(line) - 1; i++) line[p++] = msg[i];
    }
    line[p] = '\0';

    if (g_nsrt_log_count < NSRT_MAX_LOGS) {
        dll_strcpy(g_nsrt_logs[g_nsrt_log_count++], line, 56);
    } else {
        for (int i = 0; i < NSRT_MAX_LOGS - 1; i++) {
            dll_strcpy(g_nsrt_logs[i], g_nsrt_logs[i + 1], 56);
        }
        dll_strcpy(g_nsrt_logs[NSRT_MAX_LOGS - 1], line, 56);
    }
}

static int nsrt_get_log_count(void) { return g_nsrt_log_count; }
static const char* nsrt_get_log_line(int idx) {
    return (idx >= 0 && idx < g_nsrt_log_count) ? g_nsrt_logs[idx] : "";
}
static void nsrt_serial_print(const char* msg) {
    if (g_api && g_api->serial_puts && msg) g_api->serial_puts(msg);
}

/* --- File System & Sandbox ACL --- */
static bool nsrt_file_exists(const char* path) {
    return (g_api && g_api->vfs_exists && path) ? g_api->vfs_exists(path) : false;
}

static void* nsrt_file_read(const char* path, uint32_t* out_size) {
    return (g_api && g_api->vfs_read_entire_file && path) ? g_api->vfs_read_entire_file(path, out_size) : NULL;
}

static bool nsrt_file_write(const char* path, const void* data, uint32_t size, bool sandboxed) {
    if (!g_api || !path || !data) return false;
    if (sandboxed && nsrt_is_protected_path(path)) {
        nsrt_log("ACL", "Scrittura negata su C:\\NewOS\\");
        return false;
    }
    if (g_api->vfs_exists && !g_api->vfs_exists(path) && g_api->vfs_create) {
        g_api->vfs_create(path);
    }
    return g_api->vfs_write_entire_file_inplace
        ? (g_api->vfs_write_entire_file_inplace(path, data, size) >= 0)
        : false;
}

static bool nsrt_file_delete(const char* path, bool sandboxed) {
    if (!g_api || !g_api->vfs_unlink || !path) return false;
    if (sandboxed && nsrt_is_protected_path(path)) {
        nsrt_log("ACL", "Eliminazione negata su C:\\NewOS\\");
        return false;
    }
    return g_api->vfs_unlink(path);
}

static uint64_t nsrt_file_size(const char* path) {
    return (g_api && g_api->vfs_file_size && path) ? g_api->vfs_file_size(path) : 0;
}

static bool nsrt_mkdir(const char* path, bool sandboxed) {
    if (!g_api || !g_api->vfs_mkdir || !path) return false;
    if (sandboxed && nsrt_is_protected_path(path)) {
        nsrt_log("ACL", "Mkdir negato su C:\\NewOS\\");
        return false;
    }
    return g_api->vfs_mkdir(path);
}

static int32_t nsrt_readdir(const char* dir_path, ns_dirent_t* out_entries, int32_t max_entries) {
    if (!g_api || !g_api->vfs_readdir || !dir_path || !out_entries || max_entries <= 0) return -1;
    return g_api->vfs_readdir(dir_path, (void*)out_entries, max_entries);
}

static int nsrt_drive_list(char* out_letters, int max_drives) {
    if (!g_api || !out_letters || max_drives <= 0) return 0;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    return (ext && ext->vfs_list_drives) ? ext->vfs_list_drives(out_letters, max_drives) : 0;
}

/* --- Timing & Sistema --- */
static uint64_t nsrt_get_ticks(void) {
    return (g_api && g_api->timer_get_ticks) ? g_api->timer_get_ticks() : 0;
}

static void nsrt_sleep_ms(uint32_t ms) {
    if (g_api && g_api->timer_sleep) g_api->timer_sleep(ms);
}

static bool nsrt_get_sys_info(ns_sys_info_t* out_info) {
    if (!g_api || !out_info) return false;
    nsrt_memset(out_info, 0, sizeof(ns_sys_info_t));
    out_info->screen_w = g_api->gfx_get_width ? g_api->gfx_get_width() : 0;
    out_info->screen_h = g_api->gfx_get_height ? g_api->gfx_get_height() : 0;
    out_info->uptime_ticks = g_api->timer_get_ticks ? g_api->timer_get_ticks() : 0;

    char drives[16];
    out_info->drive_count = nsrt_drive_list(drives, 16);

    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    if (ext && ext->exfat_get_free_clusters && ext->exfat_get_allocated_clusters) {
        uint32_t free_c = ext->exfat_get_free_clusters();
        uint32_t alloc_c = ext->exfat_get_allocated_clusters();
        out_info->free_ram_kb = free_c * 4;
        out_info->total_ram_kb = (free_c + alloc_c) * 4;
    }

    if (g_api->user_get_current) {
        user_profile_t* up = g_api->user_get_current();
        if (up) {
            nsrt_strncpy(out_info->username, up->username, sizeof(out_info->username));
            nsrt_strncpy(out_info->computer_name, up->computer_name, sizeof(out_info->computer_name));
        }
    }
    return true;
}

/* --- Audio & Multimedia --- */
static void nsrt_play_sound(int sound_id) {
    if (g_api && g_api->sound_play) g_api->sound_play(sound_id);
}

static bool nsrt_play_wav(const char* path) {
    return (g_api && g_api->sound_play_wav_file && path) ? g_api->sound_play_wav_file(path) : false;
}

static void nsrt_stop_sound(void) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    if (ext && ext->sound_stop) ext->sound_stop();
}

static void nsrt_set_volume(uint8_t vol) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    if (ext && ext->sound_set_volume) ext->sound_set_volume(vol);
}

static uint8_t nsrt_get_volume(void) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    return (ext && ext->sound_get_volume) ? ext->sound_get_volume() : 100;
}

/* --- Rete, Browser & Notifiche --- */
static uint32_t nsrt_dns_resolve(const char* hostname) {
    if (!g_api || !hostname) return 0;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    return (ext && ext->dns_resolve) ? ext->dns_resolve(hostname) : 0;
}

static bool nsrt_net_is_connected(void) {
    return (g_api && g_api->net_nic_get_active) ? (g_api->net_nic_get_active() != NULL) : false;
}

static bool nsrt_net_get_ip(char* out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return false;
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    if (ext && ext->dhcp_get_state && ext->dhcp_state_to_str) {
        const char* st = ext->dhcp_state_to_str(ext->dhcp_get_state());
        nsrt_strncpy(out_buf, st ? st : "N/D", max_len);
        return true;
    }
    nsrt_strncpy(out_buf, nsrt_net_is_connected() ? "NIC Attiva" : "Offline", max_len);
    return nsrt_net_is_connected();
}

static void nsrt_open_url(const char* url) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    if (ext && ext->app_browser_open_url && url) ext->app_browser_open_url(url);
}

static void nsrt_notify(const char* title, const char* msg) {
    nsrt_log(title ? title : "NSRT", msg ? msg : "");
}

/* --- Registro --- */
static bool nsrt_reg_read_sz(const char* path, const char* val_name, char* out_buf, size_t max_len) {
    return (g_api && g_api->registry_get_value_sz && path && val_name && out_buf)
        ? g_api->registry_get_value_sz(path, val_name, out_buf, max_len)
        : false;
}

static bool nsrt_reg_read_dword(const char* path, const char* val_name, uint32_t* out_val) {
    return (g_api && g_api->registry_get_value_dword && path && val_name && out_val)
        ? g_api->registry_get_value_dword(path, val_name, out_val)
        : false;
}

static bool nsrt_reg_write_sz(const char* path, const char* val_name, const char* val, bool sandboxed) {
    if (!g_api || !g_api->registry_set_value_sz || !path || !val_name || !val) return false;
    if (g_api->registry_is_read_only && g_api->registry_is_read_only()) {
        nsrt_log("ACL", "RegWrite bloccato: registro in sola lettura");
        return false;
    }
    if (sandboxed && !nsrt_is_reg_writable_sandbox(path)) {
        nsrt_log("ACL", "RegWrite negato su settore critico");
        return false;
    }
    return g_api->registry_set_value_sz(path, val_name, val);
}

static bool nsrt_reg_write_dword(const char* path, const char* val_name, uint32_t val, bool sandboxed) {
    if (!g_api || !g_api->registry_set_value_dword || !path || !val_name) return false;
    if (g_api->registry_is_read_only && g_api->registry_is_read_only()) {
        nsrt_log("ACL", "RegWriteDword bloccato: registro in sola lettura");
        return false;
    }
    if (sandboxed && !nsrt_is_reg_writable_sandbox(path)) {
        nsrt_log("ACL", "RegWriteDword negato su settore critico");
        return false;
    }
    return g_api->registry_set_value_dword(path, val_name, val);
}

/* --- Dialoghi COMDLG & Clipboard --- */
static bool nsrt_open_file_dialog(const char* init_dir, void* cb, void* userdata) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    return (ext && ext->comdlg_open_file_dialog) ? ext->comdlg_open_file_dialog(init_dir, cb, userdata) : false;
}

static bool nsrt_save_file_dialog(const char* init_dir, const char* def_name, void* cb, void* userdata) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    return (ext && ext->comdlg_save_file_dialog) ? ext->comdlg_save_file_dialog(init_dir, def_name, cb, userdata) : false;
}

static void nsrt_clipboard_set_text(const char* text) {
    if (g_api && g_api->clipboard_set_text && text) g_api->clipboard_set_text(text);
}

static const char* nsrt_clipboard_get_text(void) {
    return (g_api && g_api->clipboard_get_text) ? g_api->clipboard_get_text() : "";
}

/* --- Grafica 2D --- */
static uint32_t nsrt_screen_w(void) { return (g_api && g_api->gfx_get_width) ? g_api->gfx_get_width() : 0; }
static uint32_t nsrt_screen_h(void) { return (g_api && g_api->gfx_get_height) ? g_api->gfx_get_height() : 0; }
static void nsrt_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (g_api && g_api->gfx_fill_rect) g_api->gfx_fill_rect(x, y, w, h, color);
}
static void nsrt_gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (g_api && g_api->gfx_draw_rect) g_api->gfx_draw_rect(x, y, w, h, color);
}
static void nsrt_gfx_fill_rounded(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color) {
    if (g_api && g_api->gfx_fill_rounded_rect) g_api->gfx_fill_rounded_rect(x, y, w, h, radius, color);
}
static void nsrt_gfx_draw_text(int32_t x, int32_t y, const char* str, uint32_t color, uint32_t bg, bool transparent) {
    if (g_api && g_api->gfx_draw_string && str) g_api->gfx_draw_string(x, y, str, color, bg, transparent);
}
static void nsrt_gfx_draw_icon16(int32_t x, int32_t y, int icon_id) {
    if (g_api && g_api->gfx_draw_icon_16) g_api->gfx_draw_icon_16(x, y, icon_id);
}
static void nsrt_gfx_draw_icon32(int32_t x, int32_t y, int icon_id) {
    if (g_api && g_api->gfx_draw_icon_32) g_api->gfx_draw_icon_32(x, y, icon_id);
}

/* --- Finestre & UI Event-Driven (XUIML.DLL) --- */
static void nsrt_win_draw(window_t* win) {
    nsrt_app_t* app = (nsrt_app_t*)win->app_data;
    if (app && app->xapi && app->doc) app->xapi->render(app->doc, win);
}

static void nsrt_win_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    nsrt_app_t* app = (nsrt_app_t*)win->app_data;
    if (!app || !app->xapi || !app->doc || !clicked) return;
    const char* id = app->xapi->mouse(app->doc, win, lx, ly, clicked);
    if (id && id[0] && app->on_event) {
        app->on_event(app, id, app->user_ctx);
    }
}

static void nsrt_win_key(window_t* win, char c) {
    nsrt_app_t* app = (nsrt_app_t*)win->app_data;
    if (app && app->xapi && app->doc) app->xapi->key(app->doc, win, c);
}

static nsrt_app_t* nsrt_create_xuiml_app(const char* title, int32_t w, int32_t h,
                                         const char* xuiml_path, nsrt_event_cb on_event, void* user_ctx) {
    if (!g_api || !xuiml_path) return NULL;
    if (!g_xapi) g_xapi = xuiml_load_library(g_api);
    if (!g_xapi) return NULL;

    xuiml_doc_t* doc = g_xapi->load(xuiml_path);
    if (!doc) return NULL;

    uint32_t sw = g_api->gfx_get_width ? g_api->gfx_get_width() : 1024;
    uint32_t sh = g_api->gfx_get_height ? g_api->gfx_get_height() : 768;
    int32_t wx = ((int32_t)sw - w) / 2;
    int32_t wy = ((int32_t)sh - h) / 2 - 12;
    if (wx < 10) wx = 10;
    if (wy < 10) wy = 10;

    window_t* win = (window_t*)g_api->wm_create_window(title ? title : "NewSoft App", wx, wy, w, h, APP_ABOUT);
    if (!win) { g_xapi->free(doc); return NULL; }

    nsrt_app_t* app = (nsrt_app_t*)nsrt_calloc(1, sizeof(nsrt_app_t));
    if (!app) { g_api->wm_destroy_window(win->id); g_xapi->free(doc); return NULL; }

    app->win      = win;
    app->xapi     = g_xapi;
    app->doc      = doc;
    app->on_event = on_event;
    app->user_ctx = user_ctx;

    win->app_data = app;
    win->on_draw  = nsrt_win_draw;
    win->on_mouse = nsrt_win_mouse;
    win->on_key   = nsrt_win_key;

    nsrt_log("APP", title ? title : "Avviata app XUIML");
    return app;
}

static void nsrt_close_app(nsrt_app_t* app) {
    if (!app || !g_api) return;
    if (app->win && g_api->wm_destroy_window) g_api->wm_destroy_window(app->win->id);
}

static void nsrt_set_text(nsrt_app_t* app, const char* id, const char* text) {
    if (app && app->xapi && app->doc) app->xapi->set_text(app->doc, id, text);
}
static const char* nsrt_get_text(nsrt_app_t* app, const char* id) {
    return (app && app->xapi && app->doc) ? app->xapi->get_text(app->doc, id) : "";
}
static void nsrt_set_int(nsrt_app_t* app, const char* id, int val) {
    if (app && app->xapi && app->doc) app->xapi->set_int(app->doc, id, val);
}
static int nsrt_get_int(nsrt_app_t* app, const char* id) {
    return (app && app->xapi && app->doc) ? app->xapi->get_int(app->doc, id) : 0;
}
static void nsrt_list_clear(nsrt_app_t* app, const char* id) {
    if (app && app->xapi && app->doc) app->xapi->list_clear(app->doc, id);
}
static void nsrt_list_add(nsrt_app_t* app, const char* id, const char* item) {
    if (app && app->xapi && app->doc) app->xapi->list_add(app->doc, id, item);
}
static void nsrt_get_rtc_time(ns_datetime_t* out_dt) {
    if (!out_dt) return;
    if (g_api && g_api->rtc_get_time) {
        g_api->rtc_get_time((void*)out_dt);
    } else {
        nsrt_memset(out_dt, 0, sizeof(ns_datetime_t));
    }
}

static void nsrt_mouse_get_state(ns_mouse_state_t* out_state) {
    if (!out_state) return;
    if (g_api && g_api->mouse_get_state) {
        g_api->mouse_get_state((mouse_state_t*)out_state);
    } else {
        nsrt_memset(out_state, 0, sizeof(ns_mouse_state_t));
    }
}

static bool nsrt_keyboard_has_char(void) {
    return (g_api && g_api->keyboard_has_char) ? g_api->keyboard_has_char() : false;
}

static char nsrt_keyboard_get_char(void) {
    return (g_api && g_api->keyboard_get_char) ? g_api->keyboard_get_char() : 0;
}

static uint32_t nsrt_crc32(const void* data, size_t len) {
    if (!data || len == 0) return 0;
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static bool nsrt_ac97_is_available(void) {
    const kernel_ext_api_t* ext = g_api ? (const kernel_ext_api_t*)g_api->ext_api : NULL;
    return (ext && ext->ac97_is_available) ? ext->ac97_is_available() : false;
}

static void* nsrt_get_window_handle(nsrt_app_t* app) {
    return app ? (void*)app->win : NULL;
}

static const nsrt_api_t g_nsrt_api = {
    .magic            = NSRT_MAGIC,
    .version          = NSRT_VERSION,
    .alloc            = nsrt_alloc,
    .calloc           = nsrt_calloc,
    .realloc          = nsrt_realloc,
    .free             = nsrt_free,
    .memcpy           = nsrt_memcpy,
    .memset           = nsrt_memset,
    .memcmp           = nsrt_memcmp,
    .strlen           = nsrt_strlen,
    .strcmp           = nsrt_strcmp,
    .strncmp          = nsrt_strncmp,
    .strcpy           = nsrt_strcpy,
    .strncpy          = nsrt_strncpy,
    .snprintf         = nsrt_snprintf,
    .file_exists      = nsrt_file_exists,
    .file_read        = nsrt_file_read,
    .file_write       = nsrt_file_write,
    .file_delete      = nsrt_file_delete,
    .file_size        = nsrt_file_size,
    .mkdir            = nsrt_mkdir,
    .readdir          = nsrt_readdir,
    .is_protected     = nsrt_is_protected_path,
    .drive_list       = nsrt_drive_list,
    .log              = nsrt_log,
    .get_log_count    = nsrt_get_log_count,
    .get_log_line     = nsrt_get_log_line,
    .serial_print     = nsrt_serial_print,
    .get_ticks        = nsrt_get_ticks,
    .sleep_ms         = nsrt_sleep_ms,
    .get_sys_info     = nsrt_get_sys_info,
    .get_rtc_time     = nsrt_get_rtc_time,
    .mouse_get_state  = nsrt_mouse_get_state,
    .keyboard_has_char = nsrt_keyboard_has_char,
    .keyboard_get_char = nsrt_keyboard_get_char,
    .crc32            = nsrt_crc32,
    .play_sound       = nsrt_play_sound,
    .play_wav         = nsrt_play_wav,
    .stop_sound       = nsrt_stop_sound,
    .set_volume       = nsrt_set_volume,
    .get_volume       = nsrt_get_volume,
    .ac97_is_available = nsrt_ac97_is_available,
    .dns_resolve      = nsrt_dns_resolve,
    .net_is_connected = nsrt_net_is_connected,
    .net_get_ip       = nsrt_net_get_ip,
    .open_url         = nsrt_open_url,
    .notify           = nsrt_notify,
    .reg_read_sz      = nsrt_reg_read_sz,
    .reg_read_dword   = nsrt_reg_read_dword,
    .reg_write_sz     = nsrt_reg_write_sz,
    .reg_write_dword  = nsrt_reg_write_dword,
    .reg_is_non_critical = nsrt_is_reg_writable_sandbox,
    .open_file_dialog = nsrt_open_file_dialog,
    .save_file_dialog = nsrt_save_file_dialog,
    .clipboard_set_text = nsrt_clipboard_set_text,
    .clipboard_get_text = nsrt_clipboard_get_text,
    .screen_w         = nsrt_screen_w,
    .screen_h         = nsrt_screen_h,
    .gfx_fill_rect    = nsrt_gfx_fill_rect,
    .gfx_draw_rect    = nsrt_gfx_draw_rect,
    .gfx_fill_rounded = nsrt_gfx_fill_rounded,
    .gfx_draw_text    = nsrt_gfx_draw_text,
    .gfx_draw_icon16  = nsrt_gfx_draw_icon16,
    .gfx_draw_icon32  = nsrt_gfx_draw_icon32,
    .create_xuiml_app = nsrt_create_xuiml_app,
    .close_app        = nsrt_close_app,
    .set_text         = nsrt_set_text,
    .get_text         = nsrt_get_text,
    .set_int          = nsrt_set_int,
    .get_int          = nsrt_get_int,
    .list_clear       = nsrt_list_clear,
    .list_add         = nsrt_list_add,
    .get_window_handle = nsrt_get_window_handle
};

__attribute__((section(".entry"), used))
const nsrt_api_t* nsrt_dll_entry(const kernel_api_t* api) {
    nsrt_apply_relocs();
    g_api = api;
    if (g_nsrt_log_count == 0) {
        nsrt_log("NSRT", "NewSoft Runtime v3.0 inizializzato");
    }
    return &g_nsrt_api;
}
