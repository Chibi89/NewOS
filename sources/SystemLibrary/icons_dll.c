/**
 * SystemLibrary/icons_dll.c
 * Libreria Dinamica Out-of-Kernel ICONS.DLL (Repository Icone di Sistema MICO)
 * Espone le risorse icone 32x32 e 16x16 ARGB di NewOS V1 fuori dal kernel.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include <screen_modules.h>
#include "icons_data.h"

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_icons_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "ICONS",
    .signature      = MODULE_AUTH_TAG
};
#endif

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void icons_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_icons_header;
    const uint32_t* tbl = (const uint32_t*)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t*)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

__attribute__((section(".text.entry"), used))
const uint8_t* icons_dll_entry(const kernel_api_t* api, uint32_t* out_size) {
    icons_apply_relocs();
    (void)api;
    if (out_size) *out_size = g_icons_mico_size;
    return g_icons_mico_data;
}
