/**
 * SystemLibrary/studio.h
 * NewOS Studio — Form Designer Visuale .xuiml & SDK IDE (Out-of-Kernel)
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */
#ifndef SYSTEM_LIBRARY_STUDIO_H
#define SYSTEM_LIBRARY_STUDIO_H

#include <types.h>
#include <screen_modules.h>

#define STUDIO_MAGIC 0x4F445453u /* 'STDO' */

typedef struct {
    uint32_t magic;
    void (*open)(void);
} studio_api_t;

typedef const studio_api_t* (*studio_dll_entry_fn)(const kernel_api_t* api);

static inline const studio_api_t* studio_load_library(const kernel_api_t* api) {
    if (!api || !api->vfs_read_entire_file || !api->screen_mgr_verify_module) return NULL;
    uint32_t sz = 0;
    void* raw = api->vfs_read_entire_file("C:\\NewOS\\SystemLibrary\\STUDIO.DLL", &sz);
    if (!raw) raw = api->vfs_read_entire_file("/NewOS/SystemLibrary/STUDIO.DLL", &sz);
    if (!raw || sz < sizeof(module_header_t)) {
        if (raw) api->kfree(raw);
        return NULL;
    }
    if (!api->screen_mgr_verify_module(raw, sz, MODULE_TYPE_SYSTEM_LIBRARY, "STUDIO")) {
        api->kfree(raw);
        return NULL;
    }
    uint8_t* exec_buf = (uint8_t*)api->kmalloc(sz + 8192);
    if (!exec_buf) { api->kfree(raw); return NULL; }
    for (uint32_t i = 0; i < sz; i++) exec_buf[i] = ((const uint8_t*)raw)[i];
    for (uint32_t i = sz; i < sz + 8192; i++) exec_buf[i] = 0;
    api->kfree(raw);

    const module_header_t* h = (const module_header_t*)exec_buf;
    studio_dll_entry_fn entry = (studio_dll_entry_fn)(exec_buf + h->entry_offset);
    const studio_api_t* st = entry(api);
    if (!st || st->magic != STUDIO_MAGIC) {
        api->kfree(exec_buf);
        return NULL;
    }
    return st;
}

#endif
