#include "cssengine.h"
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*css_entry_fn)(const kernel_api_t *api, css_params_t *p);

static css_entry_fn g_css_entry = NULL;
static uint8_t *g_css_buf = NULL;

bool css_engine_init(void) {
    if (g_css_entry) return true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\CSSENGINE.DLL",
        "C:\\NewOS\\Boot\\CSSENGINE.DLL",
        "/NewOS/SystemLibrary/CSSENGINE.DLL",
        "CSSENGINE.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_css_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_css_entry = (css_entry_fn)(exec_buf + h->entry_offset);
                    g_css_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }

    if (!g_css_entry) return false;

    css_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = CSS_CMD_INIT;
    return (g_css_entry(screen_mgr_get_api(), &p) == 0);
}

void* css_parse_from_html(const char* html) {
    if (!html || !css_engine_init()) return NULL;
    css_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = CSS_CMD_PARSE_HTML;
    p.html_or_css = html;
    if (g_css_entry(screen_mgr_get_api(), &p) == 0) {
        return p.sheet_handle;
    }
    return NULL;
}

void css_free_sheet(void* sheet) {
    if (sheet) kfree(sheet);
}

void css_resolve_style(void* sheet, const char* tag, const char* id_attr,
                       const char* class_attr, const char* inline_style,
                       css_style_t* out_style) {
    if (!out_style) return;
    memset(out_style, 0, sizeof(*out_style));
    out_style->display = CSS_DISPLAY_INLINE;

    if (!css_engine_init()) return;

    css_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = CSS_CMD_RESOLVE_TAG;
    p.sheet_handle = sheet;
    p.tag_name = tag;
    p.id_attr = id_attr;
    p.class_attr = class_attr;
    p.inline_style = inline_style;
    if (g_css_entry(screen_mgr_get_api(), &p) == 0) {
        *out_style = p.out_style;
    }
}

