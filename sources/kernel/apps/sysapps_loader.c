/**
 * kernel/apps/sysapps_loader.c
 * Loader out-of-kernel unificato per SYSAPPS.DLL (Notepad, Calc, Player, Autoplay, Taskmgr, Cmd, Regedit, RulesViewer, WhatsNew)
 */
#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>
#include "../../autoplay/autoplay.h"

typedef struct {
    uint32_t cmd;
    const char *arg1;
    const char *arg2;
    uint32_t uarg1;
    uint32_t uarg2;
} sysapps_params_t;

typedef int (*sysapps_entry_fn)(const kernel_api_t *api, sysapps_params_t *p);

static sysapps_entry_fn g_sysapps_entry = NULL;
static uint8_t *g_sysapps_buf = NULL;

static bool load_sysapps_dll(void) {
    if (g_sysapps_entry) return true;
    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\SYSAPPS.DLL",
        "C:\\NewOS\\Boot\\SYSAPPS.DLL",
        "/NewOS/SystemLibrary/SYSAPPS.DLL",
        "SYSAPPS.DLL"
    };
    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_sysapps_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "SYSAPPS")) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_sysapps_entry = (sysapps_entry_fn)(exec_buf + h->entry_offset);
                    g_sysapps_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }
    return (g_sysapps_entry != NULL);
}

static void call_sysapp(uint32_t cmd, const char *a1, const char *a2, uint32_t u1, uint32_t u2) {
    if (!load_sysapps_dll()) return;
    sysapps_params_t p = { cmd, a1, a2, u1, u2 };
    g_sysapps_entry(screen_mgr_get_api(), &p);
}

void app_notepad_launch(void) { call_sysapp(1, NULL, NULL, 0, 0); }
void app_notepad_open_file(const char *t, const char *c) { call_sysapp(2, t, c, 0, 0); }
void app_calc_launch(void) { call_sysapp(3, NULL, NULL, 0, 0); }
void app_player_launch(void) { call_sysapp(4, NULL, NULL, 0, 0); }
void app_player_open_file(const char *f) { call_sysapp(5, f, NULL, 0, 0); }
void app_player_update_tick(void) { if (g_sysapps_entry) call_sysapp(6, NULL, NULL, 0, 0); }
void app_player_on_close(void) { if (g_sysapps_entry) call_sysapp(7, NULL, NULL, 0, 0); }
void app_autoplay_launch(char d) { call_sysapp(8, NULL, NULL, (uint32_t)(uint8_t)d, 0); }
void app_autoplay_launch_mode(char d, autoplay_mode_t m) { call_sysapp(9, NULL, NULL, (uint32_t)(uint8_t)d, (uint32_t)m); }
void app_taskmgr_launch(void) { call_sysapp(10, NULL, NULL, 0, 0); }
void app_cmd_launch(void) { call_sysapp(11, NULL, NULL, 0, 0); }
void app_regedit_launch(void) { call_sysapp(12, NULL, NULL, 0, 0); }
void app_rules_viewer_launch(void) { call_sysapp(13, NULL, NULL, 0, 0); }
void app_rules_viewer_open_file(const char *f) { call_sysapp(14, f, NULL, 0, 0); }
void app_whatsnew_launch(void) { call_sysapp(15, NULL, NULL, 0, 0); }
