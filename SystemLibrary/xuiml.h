/**
 * SystemLibrary/xuiml.h
 * Interfaccia pubblica per il motore UI dichiarativo XUIML.DLL
 * I file .xuiml risiedono in C:\NewOS\SystemLibrary\UI\ e descrivono
 * form, schede, layout e controlli separando la Vista dalla Logica dei .CPL/.DLL.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#ifndef SYSTEM_LIBRARY_XUIML_H
#define SYSTEM_LIBRARY_XUIML_H

#include <types.h>
#include <screen_modules.h>
#include <gui/wm.h>

typedef struct xuiml_doc xuiml_doc_t;

typedef struct {
    uint32_t magic; // 'XUIM' = 0x4D495558
    xuiml_doc_t* (*load)(const char* vfs_path);
    void         (*free)(xuiml_doc_t* doc);
    void         (*render)(xuiml_doc_t* doc, window_t* win);
    const char*  (*mouse)(xuiml_doc_t* doc, window_t* win, int32_t lx, int32_t ly, bool clicked);
    bool         (*key)(xuiml_doc_t* doc, window_t* win, char c);
    void         (*set_text)(xuiml_doc_t* doc, const char* id, const char* text);
    const char*  (*get_text)(xuiml_doc_t* doc, const char* id);
    void         (*set_int)(xuiml_doc_t* doc, const char* id, int val);
    int          (*get_int)(xuiml_doc_t* doc, const char* id);
    void         (*list_clear)(xuiml_doc_t* doc, const char* id);
    void         (*list_add)(xuiml_doc_t* doc, const char* id, const char* item_text);
    int          (*get_active_tab)(xuiml_doc_t* doc);
    void         (*set_active_tab)(xuiml_doc_t* doc, int tab_idx);
} xuiml_api_t;

typedef const xuiml_api_t* (*xuiml_dll_entry_fn)(const kernel_api_t* api);

static inline const xuiml_api_t* xuiml_load_library(const kernel_api_t* api) {
    if (!api || !api->vfs_read_entire_file || !api->screen_mgr_verify_module) return NULL;
    uint32_t sz = 0;
    void* raw = api->vfs_read_entire_file("C:\\NewOS\\SystemLibrary\\XUIML.DLL", &sz);
    if (!raw) raw = api->vfs_read_entire_file("/NewOS/SystemLibrary/XUIML.DLL", &sz);
    if (!raw || sz < sizeof(module_header_t)) {
        if (raw) api->kfree(raw);
        return NULL;
    }
    if (!api->screen_mgr_verify_module(raw, sz, MODULE_TYPE_SYSTEM_LIBRARY, "XUIML")) {
        api->kfree(raw);
        return NULL;
    }
    uint8_t* exec_buf = (uint8_t*)api->kmalloc(sz + 8192);
    if (!exec_buf) { api->kfree(raw); return NULL; }
    for (uint32_t i = 0; i < sz; i++) exec_buf[i] = ((const uint8_t*)raw)[i];
    for (uint32_t i = sz; i < sz + 8192; i++) exec_buf[i] = 0;
    api->kfree(raw);

    const module_header_t* h = (const module_header_t*)exec_buf;
    xuiml_dll_entry_fn entry = (xuiml_dll_entry_fn)(exec_buf + h->entry_offset);
    const xuiml_api_t* xapi = entry(api);
    if (!xapi || xapi->magic != 0x4D495558u) {
        api->kfree(exec_buf);
        return NULL;
    }
    return xapi;
}

#endif
