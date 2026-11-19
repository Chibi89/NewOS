/**
 * WebBrowser/webbrowser_loader.c
 * Loader out-of-kernel per NewSoft Web Browser (C:\NewOS\SystemLibrary\WEBBROWSER.DLL)
 */
#include "webbrowser.h"
#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    uint32_t cmd; // 0 = launch, 1 = open_url
    const char *url;
} webbrowser_dll_params_t;

typedef int (*wb_dll_entry_fn)(const kernel_api_t *api, webbrowser_dll_params_t *p);

static wb_dll_entry_fn g_wb_entry = NULL;
static uint8_t *g_wb_buf = NULL;

static bool load_webbrowser_dll(void) {
    if (g_wb_entry) return true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\WEBBROWSER.DLL",
        "C:\\NewOS\\Boot\\WEBBROWSER.DLL",
        "/NewOS/SystemLibrary/WEBBROWSER.DLL",
        "WEBBROWSER.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_wb_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "WEBBROWSER")) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_wb_entry = (wb_dll_entry_fn)(exec_buf + h->entry_offset);
                    g_wb_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }
    return (g_wb_entry != NULL);
}

void app_browser_launch(void) {
    if (!load_webbrowser_dll()) return;
    webbrowser_dll_params_t p = { 0, NULL };
    g_wb_entry(screen_mgr_get_api(), &p);
}

void app_browser_open_url(const char *url) {
    if (!load_webbrowser_dll()) return;
    webbrowser_dll_params_t p = { 1, url };
    g_wb_entry(screen_mgr_get_api(), &p);
}
