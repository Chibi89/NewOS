#include "dll_libc.h"
#include <drivers/ata.h>
#include <drivers/usb/usb_msc.h>
#include <fs/ntfs.h>
#include <fs/udf.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <mm/heap.h>
#include <string.h>

__attribute__((section(".header"), used)) module_header_t g_fs_ext_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "FS_EXT",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void fs_ext_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_fs_ext_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

typedef struct {
    uint32_t cmd;
    const void *dev;
    void *out_ver;
    bool *out_bridge;
    bool ret_bool;
} fs_ext_params_t;

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, fs_ext_params_t *p) {
    fs_ext_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1:
            ntfs_init();
            udf_init();
            p->ret_bool = true;
            break;
        case 2:
            p->ret_bool = udf_probe_device((const ata_device_t *)p->dev,
                                           (udf_version_t *)p->out_ver,
                                           p->out_bridge);
            break;
        default:
            return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#include "../kernel/fs/ntfs.c"
#include "../kernel/fs/udf.c"
