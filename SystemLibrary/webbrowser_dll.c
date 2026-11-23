#include "dll_libc.h"
#include <arch/x86_64/io.h>
#include <drivers/net/net_nic.h>
#include <fs/vfs.h>
#include <gui/fontmgr.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <net/nettls_loader.h>
#include <net/socket.h>
#include <string.h>
#include <user.h>

__attribute__((section(".header"), used)) module_header_t g_webbrowser_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "WEBBROWSER",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

typedef struct {
    uint32_t cmd; // 0 = launch, 1 = open_url
    const char *url;
} webbrowser_dll_params_t;

void app_browser_launch(void);
void app_browser_open_url(const char *url);

static void wb_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_webbrowser_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, webbrowser_dll_params_t *p) {
    wb_apply_relocs();
    if (!api) return -1;
    g_dll_api = api;
    if (p && p->cmd == 1 && p->url) {
        app_browser_open_url(p->url);
    } else {
        app_browser_launch();
    }
    return 0;
}

// Reindirizza tutte le API del kernel tramite g_dll_api
#define kmalloc(s)               g_dll_api->kmalloc(s)
#define kfree(p)                 g_dll_api->kfree(p)
#define wm_create_window(t,x,y,w,h,a) ((window_t *)g_dll_api->wm_create_window((t),(x),(y),(w),(h),(int)(a)))
#define wm_destroy_window(id)    g_dll_api->wm_destroy_window(id)
#define wm_get_theme()           g_dll_api->wm_get_theme()
#define gfx_get_width()          g_dll_api->gfx_get_width()
#define gfx_get_height()         g_dll_api->gfx_get_height()
#define gfx_fill_rect            g_dll_api->gfx_fill_rect
#define gfx_draw_bevel_rect      g_dll_api->gfx_draw_bevel_rect
#define gfx_draw_string          g_dll_api->gfx_draw_string
#define gfx_draw_pixel           g_dll_api->gfx_draw_pixel
#define gfx_get_pixel            g_dll_api->gfx_get_pixel
#define gfx_draw_icon_16         g_dll_api->gfx_draw_icon_16
#define vfs_read_entire_file     g_dll_api->vfs_read_entire_file
#define vfs_write_entire_file_inplace g_dll_api->vfs_write_entire_file_inplace
#define vfs_exists               g_dll_api->vfs_exists
#define vfs_create               g_dll_api->vfs_create
#define vfs_mkdir                g_dll_api->vfs_mkdir
#define screen_mgr_verify_module g_dll_api->screen_mgr_verify_module
#define screen_mgr_get_api()     g_dll_api
#define fontmgr_get_ttf_glyph    g_dll_api->fontmgr_get_ttf_glyph
#define nettls_http_get(u,m,r)   g_dll_api->nettls_http_get((u),(m),(r))
#define nettls_free_response(r)  g_dll_api->nettls_free_response(r)
#define net_nic_get_active       g_dll_api->net_nic_get_active
#define net_format_ip            g_dll_api->net_format_ip
#define net_format_mac           g_dll_api->net_format_mac
#define user_get_current()       g_dll_api->user_get_current()

void *memset(void *dst, int c, size_t n) { return dll_memset(dst, c, n); }
void *memcpy(void *dst, const void *src, size_t n) { return dll_memcpy(dst, src, n); }
#define strlen   dll_strlen
#define strcpy   dll_strcpy
#define strncpy  dll_strncpy
#define strncat  dll_strncat
#define strcmp   dll_strcmp
#define strncmp  dll_strncmp
#define strcasecmp  dll_strcasecmp
#define strncasecmp dll_strncasecmp
#define strchr   dll_strchr
#define strrchr  dll_strrchr
#define strstr   dll_strstr
#define mini_snprintf dll_snprintf

// Include i loader e il browser direttamente dentro WEBBROWSER.DLL
#include "../WebBrowser/css_loader.c"
#include "../WebBrowser/js_loader.c"
#include "../WebBrowser/netsurf_loader.c"
#include "../WebBrowser/imgcodec_loader.c"
#include "../WebBrowser/webbrowser.c"
