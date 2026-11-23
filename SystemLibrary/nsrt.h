/**
 * SystemLibrary/nsrt.h
 * NewSoft Runtime (NSRT.DLL) — Runtime Ufficiale per Sviluppatori NewOS V1 (64-bit)
 * Espone memoria, I/O con enforcement ACL, logging diagnostico, rete e
 * binding event-driven con il motore UI dichiarativo XUIML.DLL.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#ifndef SYSTEM_LIBRARY_NSRT_H
#define SYSTEM_LIBRARY_NSRT_H

#include <types.h>
#include <screen_modules.h>

#define NSRT_MAGIC   0x5452534Eu /* 'NSRT' */
#define NSRT_VERSION 0x00010000u /* v1.0 */

typedef struct nsrt_app nsrt_app_t;

typedef void (*nsrt_event_cb)(nsrt_app_t* app, const char* control_id, void* user_ctx);

typedef struct {
    uint32_t magic;
    uint32_t version;

    /* Memoria */
    void* (*alloc)(size_t size);
    void* (*calloc)(size_t count, size_t elem_size);
    void  (*free)(void* ptr);

    /* File System & Sandbox ACL (blocca scritture terze in C:\NewOS\) */
    bool  (*file_exists)(const char* path);
    void* (*file_read)(const char* path, uint32_t* out_size);
    bool  (*file_write)(const char* path, const void* data, uint32_t size, bool sandboxed);
    bool  (*mkdir)(const char* path, bool sandboxed);

    /* Logging & Diagnostica Dev */
    void        (*log)(const char* tag, const char* msg);
    int         (*get_log_count)(void);
    const char* (*get_log_line)(int idx);

    /* Rete & Sistema */
    uint32_t (*dns_resolve)(const char* hostname);
    void     (*notify)(const char* title, const char* msg);

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
} nsrt_api_t;

typedef const nsrt_api_t* (*nsrt_dll_entry_fn)(const kernel_api_t* api);

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

#endif
