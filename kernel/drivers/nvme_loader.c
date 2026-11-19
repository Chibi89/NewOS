#include <drivers/nvme.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    uint32_t cmd;
    uint64_t lba;
    uint32_t count;
    void *buf;
    uint64_t ret_u64;
    uint32_t ret_u32;
    bool ret_bool;
    const char *ret_str;
} nvme_dll_params_t;

typedef int (*nvme_entry_fn)(const kernel_api_t *api, nvme_dll_params_t *p);

static nvme_entry_fn g_nvme_entry = NULL;
static bool g_nvme_inited = false;

static bool ensure_nvme_loaded(void) {
    if (g_nvme_entry) return true;
    if (!vfs_is_drive_mounted('C')) return false;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\NVME.DLL",
        "/NewOS/SystemLibrary/NVME.DLL"
    };
    for (int i = 0; i < 2 && !g_nvme_entry; i++) {
        uint32_t sz = 0;
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_nvme_entry = (nvme_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    return (g_nvme_entry != NULL);
}

bool nvme_init(void) {
    if (!ensure_nvme_loaded()) return false;
    if (!g_nvme_inited) {
        g_nvme_inited = true;
        nvme_dll_params_t p;
        memset(&p, 0, sizeof(p));
        p.cmd = 1;
        g_nvme_entry(screen_mgr_get_api(), &p);
        return p.ret_bool;
    }
    return nvme_is_present();
}

bool nvme_is_present(void) {
    if (!g_nvme_entry) return false;
    nvme_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 2;
    g_nvme_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

uint64_t nvme_get_capacity_sectors(void) {
    if (!nvme_init()) return 0;
    nvme_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 3;
    g_nvme_entry(screen_mgr_get_api(), &p);
    return p.ret_u64;
}

uint32_t nvme_get_sector_size(void) {
    if (!nvme_init()) return 512;
    nvme_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 4;
    g_nvme_entry(screen_mgr_get_api(), &p);
    return p.ret_u32;
}

const char *nvme_get_model(void) {
    if (!nvme_init()) return "NVMe non presente";
    nvme_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 5;
    g_nvme_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "NVMe Controller";
}

bool nvme_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    if (!nvme_init()) return false;
    nvme_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 6;
    p.lba = lba;
    p.count = count;
    p.buf = buffer;
    g_nvme_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool nvme_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    if (!nvme_init()) return false;
    nvme_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 7;
    p.lba = lba;
    p.count = count;
    p.buf = (void *)buffer;
    g_nvme_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}
