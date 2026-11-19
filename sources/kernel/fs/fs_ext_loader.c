#include <drivers/ata.h>
#include <fs/udf.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    uint32_t cmd;
    const void *dev;
    void *out_ver;
    bool *out_bridge;
    bool ret_bool;
} fs_ext_params_t;

typedef int (*fs_ext_entry_fn)(const kernel_api_t *api, fs_ext_params_t *p);

static fs_ext_entry_fn g_fs_ext_entry = NULL;
static bool g_fs_ext_registered = false;

static bool ensure_fs_ext_loaded(void) {
    if (g_fs_ext_entry) {
        if (!g_fs_ext_registered) {
            g_fs_ext_registered = true;
            fs_ext_params_t p;
            memset(&p, 0, sizeof(p));
            p.cmd = 1;
            g_fs_ext_entry(screen_mgr_get_api(), &p);
        }
        return true;
    }
    if (!vfs_is_drive_mounted('C')) return false;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\FS_EXT.DLL",
        "/NewOS/SystemLibrary/FS_EXT.DLL"
    };
    for (int i = 0; i < 2 && !g_fs_ext_entry; i++) {
        uint32_t sz = 0;
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_fs_ext_entry = (fs_ext_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    if (g_fs_ext_entry && !g_fs_ext_registered) {
        g_fs_ext_registered = true;
        fs_ext_params_t p;
        memset(&p, 0, sizeof(p));
        p.cmd = 1;
        g_fs_ext_entry(screen_mgr_get_api(), &p);
    }
    return (g_fs_ext_entry != NULL);
}

void ntfs_init(void) {
    ensure_fs_ext_loaded();
}

void udf_init(void) {
    ensure_fs_ext_loaded();
}

bool udf_probe_device(const ata_device_t *dev, udf_version_t *out_ver, bool *out_is_bridge) {
    if (!ensure_fs_ext_loaded()) return false;
    fs_ext_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 2;
    p.dev = dev;
    p.out_ver = out_ver;
    p.out_bridge = out_is_bridge;
    g_fs_ext_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}
