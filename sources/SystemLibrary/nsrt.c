/**
 * SystemLibrary/nsrt.c
 * NewSoft Runtime (NSRT.DLL) — Implementazione Core Out-of-Kernel
 * Fornisce memoria, I/O sicuro (ACL), Dev Console Log, rete e ciclo
 * event-driven basato su XUIML.DLL per applicazioni e applet di sistema.
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

static char n_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool n_istarts_with(const char* s, const char* pfx) {
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

static void* nsrt_alloc(size_t size) {
    return (g_api && g_api->kmalloc && size > 0) ? g_api->kmalloc(size) : NULL;
}

static void* nsrt_calloc(size_t count, size_t elem_size) {
    size_t tot = count * elem_size;
    uint8_t* p = (uint8_t*)nsrt_alloc(tot);
    if (p) {
        for (size_t i = 0; i < tot; i++) p[i] = 0;
    }
    return p;
}

static void nsrt_free(void* ptr) {
    if (ptr && g_api && g_api->kfree) g_api->kfree(ptr);
}

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
        ? g_api->vfs_write_entire_file_inplace(path, data, size)
        : false;
}

static bool nsrt_mkdir(const char* path, bool sandboxed) {
    if (!g_api || !g_api->vfs_mkdir || !path) return false;
    if (sandboxed && nsrt_is_protected_path(path)) {
        nsrt_log("ACL", "Mkdir negato su C:\\NewOS\\");
        return false;
    }
    return g_api->vfs_mkdir(path);
}

static uint32_t nsrt_dns_resolve(const char* hostname) {
    if (!g_api || !hostname) return 0;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    return (ext && ext->dns_resolve) ? ext->dns_resolve(hostname) : 0;
}

static void nsrt_notify(const char* title, const char* msg) {
    nsrt_log(title ? title : "NSRT", msg ? msg : "");
}

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

static const nsrt_api_t g_nsrt_api = {
    .magic            = NSRT_MAGIC,
    .version          = NSRT_VERSION,
    .alloc            = nsrt_alloc,
    .calloc           = nsrt_calloc,
    .free             = nsrt_free,
    .file_exists      = nsrt_file_exists,
    .file_read        = nsrt_file_read,
    .file_write       = nsrt_file_write,
    .mkdir            = nsrt_mkdir,
    .log              = nsrt_log,
    .get_log_count    = nsrt_get_log_count,
    .get_log_line     = nsrt_get_log_line,
    .dns_resolve      = nsrt_dns_resolve,
    .notify           = nsrt_notify,
    .create_xuiml_app = nsrt_create_xuiml_app,
    .close_app        = nsrt_close_app,
    .set_text         = nsrt_set_text,
    .get_text         = nsrt_get_text,
    .set_int          = nsrt_set_int,
    .get_int          = nsrt_get_int,
    .list_clear       = nsrt_list_clear,
    .list_add         = nsrt_list_add
};

__attribute__((section(".text.entry"), used))
const nsrt_api_t* nsrt_dll_entry(const kernel_api_t* api) {
    nsrt_apply_relocs();
    if (!api) return NULL;
    g_api = api;
    if (g_nsrt_log_count == 0) {
        nsrt_log("NSRT", "NewSoft Runtime v1.0 inizializzato (Out-of-Kernel)");
    }
    return &g_nsrt_api;
}
