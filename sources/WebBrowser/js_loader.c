#include "jsengine.h"
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*js_entry_fn)(const kernel_api_t *api, js_params_t *p);

static js_entry_fn g_js_entry = NULL;
static uint8_t *g_js_buf = NULL;

bool js_engine_init(void) {
    if (g_js_entry) return true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\JSENGINE.DLL",
        "C:\\NewOS\\Boot\\JSENGINE.DLL",
        "/NewOS/SystemLibrary/JSENGINE.DLL",
        "JSENGINE.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_js_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_js_entry = (js_entry_fn)(exec_buf + h->entry_offset);
                    g_js_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }

    if (!g_js_entry) return false;

    js_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = JS_CMD_INIT;
    return (g_js_entry(screen_mgr_get_api(), &p) == 0);
}

bool js_run_page_scripts(const char* html, const char* url,
                         char* out_html, uint32_t out_cap, uint32_t* out_len,
                         char* out_title, bool* has_title,
                         uint32_t* scripts_run, uint32_t* dom_ops) {
    if (!html || !out_html || out_cap == 0) return false;
    if (!js_engine_init()) return false;

    js_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = JS_CMD_RUN_PAGE;
    p.html_in = html;
    p.current_url = url;
    p.out_html = out_html;
    p.out_html_cap = out_cap;

    if (g_js_entry(screen_mgr_get_api(), &p) == 0) {
        if (out_len) *out_len = p.out_html_len;
        if (out_title && p.has_title) strcpy(out_title, p.out_title);
        if (has_title) *has_title = p.has_title;
        if (scripts_run) *scripts_run = p.scripts_run;
        if (dom_ops) *dom_ops = p.dom_ops;
        return true;
    }
    return false;
}

