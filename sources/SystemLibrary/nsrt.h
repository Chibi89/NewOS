/**
 * SystemLibrary/nsrt.h
 * NewSoft Runtime (NSRT.DLL) — Runtime Ufficiale per Sviluppatori NewOS V1 (64-bit)
 * Espone memoria, stringhe/formattazione freestanding, I/O con enforcement ACL,
 * logging diagnostico, audio, rete, registro, dialoghi COMDLG, grafica 2D e
 * binding event-driven con il motore UI dichiarativo XUIML.DLL.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#ifndef SYSTEM_LIBRARY_NSRT_H
#define SYSTEM_LIBRARY_NSRT_H

#ifndef NEWOS_SDK_TYPES_H
#include <types.h>
#include <screen_modules.h>
#else
#ifndef SCREEN_MODULES_H
typedef struct kernel_api_s kernel_api_t;
#endif
#endif

#define NSRT_MAGIC   0x5452534Eu /* 'NSRT' */
#define NSRT_VERSION 0x00030000u /* v3.0 */

typedef struct nsrt_app nsrt_app_t;

typedef void (*nsrt_event_cb)(nsrt_app_t* app, const char* control_id, void* user_ctx);

/* Data e ora RTC compatibile binariamente con rtc_time_t */
typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint32_t year;
} ns_datetime_t;

/* Stato mouse SDK compatibile binariamente con mouse_state_t */
typedef struct {
    int32_t x;
    int32_t y;
    bool    left_button;
    bool    right_button;
    bool    middle_button;
} ns_mouse_state_t;

/* Voce directory SDK compatibile binariamente con vfs_dirent_t */
typedef struct {
    char     name[128];
    uint64_t size;
    uint16_t attributes;
    uint16_t permissions;
    uint32_t mtime;
    bool     is_directory;
} ns_dirent_t;

/* Telemetria sistema compatta per app SDK */
typedef struct {
    uint32_t screen_w;
    uint32_t screen_h;
    uint64_t uptime_ticks;
    uint32_t free_ram_kb;
    uint32_t total_ram_kb;
    int32_t  drive_count;
    char     username[32];
    char     computer_name[32];
} ns_sys_info_t;

typedef struct {
    uint32_t magic;
    uint32_t version;

    /* Memoria & Stringhe Freestanding */
    void* (*alloc)(size_t size);
    void* (*calloc)(size_t count, size_t elem_size);
    void* (*realloc)(void* ptr, size_t old_size, size_t new_size);
    void  (*free)(void* ptr);
    void* (*memcpy)(void* dst, const void* src, size_t n);
    void* (*memset)(void* dst, int c, size_t n);
    int   (*memcmp)(const void* a, const void* b, size_t n);
    size_t (*strlen)(const char* s);
    int   (*strcmp)(const char* a, const char* b);
    int   (*strncmp)(const char* a, const char* b, size_t n);
    char* (*strcpy)(char* dst, const char* src);
    char* (*strncpy)(char* dst, const char* src, size_t n);
    int   (*snprintf)(char* buf, size_t size, const char* fmt, ...);

    /* File System & Sandbox ACL (blocca scritture terze in C:\NewOS\) */
    bool     (*file_exists)(const char* path);
    void*    (*file_read)(const char* path, uint32_t* out_size);
    bool     (*file_write)(const char* path, const void* data, uint32_t size, bool sandboxed);
    bool     (*file_delete)(const char* path, bool sandboxed);
    uint64_t (*file_size)(const char* path);
    bool     (*mkdir)(const char* path, bool sandboxed);
    int32_t  (*readdir)(const char* dir_path, ns_dirent_t* out_entries, int32_t max_entries);
    bool     (*is_protected)(const char* path);
    int      (*drive_list)(char* out_letters, int max_drives);

    /* Logging, Timing, Input & Sistema */
    void        (*log)(const char* tag, const char* msg);
    int         (*get_log_count)(void);
    const char* (*get_log_line)(int idx);
    void        (*serial_print)(const char* msg);
    uint64_t    (*get_ticks)(void);
    void        (*sleep_ms)(uint32_t ms);
    bool        (*get_sys_info)(ns_sys_info_t* out_info);
    void        (*get_rtc_time)(ns_datetime_t* out_dt);
    void        (*mouse_get_state)(ns_mouse_state_t* out_state);
    bool        (*keyboard_has_char)(void);
    char        (*keyboard_get_char)(void);
    uint32_t    (*crc32)(const void* data, size_t len);

    /* Audio & Multimedia */
    void    (*play_sound)(int sound_id);
    bool    (*play_wav)(const char* path);
    void    (*stop_sound)(void);
    void    (*set_volume)(uint8_t vol);
    uint8_t (*get_volume)(void);
    bool    (*ac97_is_available)(void);

    /* Rete, Browser & Notifiche */
    uint32_t (*dns_resolve)(const char* hostname);
    bool     (*net_is_connected)(void);
    bool     (*net_get_ip)(char* out_buf, size_t max_len);
    void     (*open_url)(const char* url);
    void     (*notify)(const char* title, const char* msg);

    /* Registro (Lettura globale + Scrittura solo nei settori non critici a sessione autenticata) */
    bool (*reg_read_sz)(const char* path, const char* val_name, char* out_buf, size_t max_len);
    bool (*reg_read_dword)(const char* path, const char* val_name, uint32_t* out_val);
    bool (*reg_write_sz)(const char* path, const char* val_name, const char* val, bool sandboxed);
    bool (*reg_write_dword)(const char* path, const char* val_name, uint32_t val, bool sandboxed);
    bool (*reg_is_non_critical)(const char* path);

    /* Dialoghi di Sistema (COMDLG.DLL) & Clipboard */
    bool        (*open_file_dialog)(const char* init_dir, void* cb, void* userdata);
    bool        (*save_file_dialog)(const char* init_dir, const char* def_name, void* cb, void* userdata);
    void        (*clipboard_set_text)(const char* text);
    const char* (*clipboard_get_text)(void);

    /* Primitivi Grafiche 2D */
    uint32_t (*screen_w)(void);
    uint32_t (*screen_h)(void);
    void (*gfx_fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void (*gfx_draw_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void (*gfx_fill_rounded)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color);
    void (*gfx_draw_text)(int32_t x, int32_t y, const char* str, uint32_t color, uint32_t bg, bool transparent);
    void (*gfx_draw_icon16)(int32_t x, int32_t y, int icon_id);
    void (*gfx_draw_icon32)(int32_t x, int32_t y, int icon_id);

    /* Finestre & UI Event-Driven (XUIML.DLL) */
    nsrt_app_t* (*create_xuiml_app)(const char* title, int32_t w, int32_t h,
                                    const char* xuiml_path, nsrt_event_cb on_event, void* user_ctx);
    void        (*close_app)(nsrt_app_t* app);
    void        (*set_text)(nsrt_app_t* app, const char* id, const char* text);
    const char* (*get_text)(nsrt_app_t* app, const char* id);
    void        (*set_int)(nsrt_app_t* app, const char* id, int val);
    int         (*get_int)(nsrt_app_t* app, const char* id);
    void        (*list_clear)(nsrt_app_t* app, const char* id);
    void        (*list_add)(nsrt_app_t* app, const char* id, const char* item);
    void*       (*get_window_handle)(nsrt_app_t* app);
} nsrt_api_t;

typedef const nsrt_api_t* (*nsrt_dll_entry_fn)(const kernel_api_t* api);

#ifdef SCREEN_MODULES_H
static inline const nsrt_api_t* nsrt_load_library(const kernel_api_t* api) {
    if (!api || !api->vfs_read_entire_file || !api->screen_mgr_verify_module) return NULL;
    uint32_t sz = 0;
    void* raw = api->vfs_read_entire_file("C:\\NewOS\\SystemLibrary\\NSRT.DLL", &sz);
    if (!raw) raw = api->vfs_read_entire_file("/NewOS/SystemLibrary/NSRT.DLL", &sz);
    if (!raw || sz < sizeof(module_header_t)) {
        if (raw) api->kfree(raw);
        return NULL;
    }
    if (!api->screen_mgr_verify_module(raw, sz, MODULE_TYPE_SYSTEM_LIBRARY, "NSRT")) {
        api->kfree(raw);
        return NULL;
    }
    uint8_t* exec_buf = (uint8_t*)api->kmalloc(sz + 8192);
    if (!exec_buf) { api->kfree(raw); return NULL; }
    for (uint32_t i = 0; i < sz; i++) exec_buf[i] = ((const uint8_t*)raw)[i];
    for (uint32_t i = sz; i < sz + 8192; i++) exec_buf[i] = 0;
    api->kfree(raw);

    const module_header_t* h = (const module_header_t*)exec_buf;
    nsrt_dll_entry_fn entry = (nsrt_dll_entry_fn)(exec_buf + h->entry_offset);
    const nsrt_api_t* rt = entry(api);
    if (!rt || rt->magic != NSRT_MAGIC) {
        api->kfree(exec_buf);
        return NULL;
    }
    return rt;
}
#endif /* SCREEN_MODULES_H */

#endif
