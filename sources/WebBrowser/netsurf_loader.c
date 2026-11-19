#include "netsurf_core.h"
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*ns_core_entry_fn)(const kernel_api_t *api, netsurf_core_params_t *p);

static ns_core_entry_fn g_ns_entry = NULL;
static uint8_t *g_ns_buf = NULL;

bool netsurf_core_init(void) {
    if (g_ns_entry) return true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\NETSURF_CORE.DLL",
        "C:\\NewOS\\Boot\\NETSURF_CORE.DLL",
        "/NewOS/SystemLibrary/NETSURF_CORE.DLL",
        "NETSURF_CORE.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_ns_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 65536);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 65536);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_ns_entry = (ns_core_entry_fn)(exec_buf + h->entry_offset);
                    g_ns_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }

    if (!g_ns_entry) return false;

    netsurf_core_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = NS_CMD_INIT;
    return (g_ns_entry(screen_mgr_get_api(), &p) == 0);
}

bool netsurf_parse_html5(const char* html, uint32_t html_len,
                         char* out_html, uint32_t out_cap, uint32_t* out_len,
                         char* out_title, bool* has_title, uint32_t* node_count) {
    if (!html || !out_html || out_cap == 0) return false;
    if (!netsurf_core_init()) return false;

    netsurf_core_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = NS_CMD_PARSE_HTML;
    p.html_in = html;
    p.html_len = html_len;
    p.out_html = out_html;
    p.out_html_cap = out_cap;

    if (g_ns_entry(screen_mgr_get_api(), &p) == 0) {
        if (out_len) *out_len = p.out_html_len;
        if (out_title && p.has_title) strcpy(out_title, p.out_title);
        if (has_title) *has_title = p.has_title;
        if (node_count) *node_count = p.node_count;
        return true;
    }
    return false;
}
