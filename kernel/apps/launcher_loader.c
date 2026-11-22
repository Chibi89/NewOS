#include <launcher.h>
#include <apps/explorer.h>
#include <apps/installer.h>
#include <apps/player.h>
#include <apps/power_props.h>
#include <apps/taskmgr.h>
#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*launcher_entry_fn)(const kernel_api_t* api, launcher_params_t* p);

static launcher_entry_fn g_launcher_entry = NULL;
static uint8_t*          g_launcher_buf   = NULL;
static char              g_last_handler[64] = "";

static bool ensure_launcher_loaded(void) {
    if (g_launcher_entry) return true;

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\LAUNCHER.DLL",
        "C:\\NewOS\\Boot\\LAUNCHER.DLL",
        "/NewOS/SystemLibrary/LAUNCHER.DLL",
        "LAUNCHER.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_launcher_entry; i++) {
        uint8_t* buf = (uint8_t*)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "LAUNCHER.DLL")) {
                const module_header_t* h = (const module_header_t*)buf;
                uint32_t alloc_sz = sz + 16384;
                if (h->code_size > sz) alloc_sz = h->code_size + 16384;
                uint8_t* exec_buf = (uint8_t*)kmalloc(alloc_sz);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    if (alloc_sz > sz) memset(exec_buf + sz, 0, alloc_sz - sz);
                    g_launcher_entry = (launcher_entry_fn)(exec_buf + h->entry_offset);
                    g_launcher_buf   = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }

    return (g_launcher_entry != NULL);
}

void launcher_init(void) {
    ensure_launcher_loaded();
}

bool launcher_open_typed(int type, const char* target, const char* args, const char* content_hint) {
    if (!target || !target[0]) return false;

    if (ensure_launcher_loaded()) {
        launcher_params_t p;
        memset(&p, 0, sizeof(p));
        p.cmd          = LAUNCHER_CMD_OPEN;
        p.type         = (uint32_t)type;
        p.target       = target;
        p.args         = args;
        p.content_hint = content_hint;
        int rc = g_launcher_entry(screen_mgr_get_api(), &p);
        if (p.resolved_handler[0]) {
            strncpy(g_last_handler, p.resolved_handler, sizeof(g_last_handler) - 1);
            g_last_handler[sizeof(g_last_handler) - 1] = '\0';
        }
        return (rc == 0 && p.result == 1);
    }

    // Fallback minimo di emergenza solo se LAUNCHER.DLL non fosse disponibile
    if (strcasecmp(target, "cmd") == 0) { app_cmd_launch(); return true; }
    if (strcasecmp(target, "notepad") == 0) { app_notepad_launch(); return true; }
    if (strcasecmp(target, "explorer") == 0) { app_explorer_open_this_pc(); return true; }
    if (strcasecmp(target, "installer") == 0) { app_installer_launch(); return true; }
    return false;
}

bool launcher_open(const char* target, const char* args) {
    return launcher_open_typed(LAUNCH_TYPE_AUTO, target, args, NULL);
}

void launcher_get_last_handler(char* out, size_t max_len) {
    if (!out || max_len == 0) return;
    strncpy(out, g_last_handler, max_len - 1);
    out[max_len - 1] = '\0';
}
