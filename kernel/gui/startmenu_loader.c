#include <gui/startmenu.h>
#include <gui/screen_mgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

#define STARTMENU_CMD_DRAW   0
#define STARTMENU_CMD_MOUSE  1
#define STARTMENU_CMD_KEY    2

typedef struct {
    uint32_t cmd;
    int32_t  sw;
    int32_t  sh;
    int32_t  mx;
    int32_t  my;
    bool     clicked;
    char     key;
    int32_t  result; // 0=none, 1=handled (keep open), 2=handled (close), 3=logout
} startmenu_params_t;

typedef int (*startmenu_entry_fn)(const kernel_api_t *api, startmenu_params_t *p);

static startmenu_entry_fn g_sm_entry = NULL;
static uint8_t *g_sm_buf = NULL;
static bool g_sm_tried = false;

static bool ensure_startmenu_loaded(void) {
    if (g_sm_entry) return true;
    if (g_sm_tried) return false;
    g_sm_tried = true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\STARTMENU.DLL",
        "C:\\NewOS\\Boot\\STARTMENU.DLL",
        "/NewOS/SystemLibrary/STARTMENU.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 3 && !g_sm_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "STARTMENU")) {
                const module_header_t *h = (const module_header_t *)buf;
                uint32_t alloc_sz = (h->code_size > sz ? h->code_size : sz) + 32768;
                uint8_t *exec_buf = (uint8_t *)kmalloc(alloc_sz);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, alloc_sz - sz);
                    g_sm_entry = (startmenu_entry_fn)(exec_buf + h->entry_offset);
                    g_sm_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }
    if (!g_sm_entry) g_sm_tried = false;
    return (g_sm_entry != NULL);
}

void startmenu_draw(int32_t sw, int32_t sh) {
    if (!ensure_startmenu_loaded()) return;
    startmenu_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = STARTMENU_CMD_DRAW;
    p.sw = sw;
    p.sh = sh;
    g_sm_entry(screen_mgr_get_api(), &p);
}

int startmenu_handle_mouse(int32_t sw, int32_t sh, int32_t mx, int32_t my, bool clicked) {
    if (!ensure_startmenu_loaded()) return 0;
    startmenu_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = STARTMENU_CMD_MOUSE;
    p.sw = sw;
    p.sh = sh;
    p.mx = mx;
    p.my = my;
    p.clicked = clicked;
    g_sm_entry(screen_mgr_get_api(), &p);
    return p.result;
}

bool startmenu_handle_key(char c) {
    if (!ensure_startmenu_loaded()) return false;
    startmenu_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = STARTMENU_CMD_KEY;
    p.key = c;
    g_sm_entry(screen_mgr_get_api(), &p);
    return (p.result != 0);
}
