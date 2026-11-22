#include "dll_libc.h"
#include <drivers/nvme.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <string.h>

__attribute__((section(".header"), used)) module_header_t g_nvme_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "NVME",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void nvme_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_nvme_header;
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
    uint64_t lba;
    uint32_t count;
    void *buf;
    uint64_t ret_u64;
    uint32_t ret_u32;
    bool ret_bool;
    const char *ret_str;
} nvme_dll_params_t;

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, nvme_dll_params_t *p) {
    nvme_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1: p->ret_bool = nvme_init(); break;
        case 2: p->ret_bool = nvme_is_present(); break;
        case 3: p->ret_u64 = nvme_get_capacity_sectors(); break;
        case 4: p->ret_u32 = nvme_get_sector_size(); break;
        case 5: p->ret_str = nvme_get_model(); break;
        case 6: p->ret_bool = nvme_read_sectors(p->lba, p->count, p->buf); break;
        case 7: p->ret_bool = nvme_write_sectors(p->lba, p->count, p->buf); break;
        case 8: p->ret_bool = nvme_deallocate_sectors(p->lba, p->count); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#undef nvme_is_present
#undef nvme_get_model
#undef nvme_get_capacity_sectors

#include "../kernel/drivers/nvme.c"
