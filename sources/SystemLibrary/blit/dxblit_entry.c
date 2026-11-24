/**
 * SystemLibrary/blit/dxblit_entry.c
 * Entry point NOSM + rilocazioni per DXBLIT.DLL
 */
#include "dxblit.h"
#include <kernel_ext_api.h>
#include <screen_modules.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_dxblit_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "DXBLIT",
    .signature = MODULE_AUTH_TAG};
#endif

static volatile uint32_t g_reloc_info[2] = {0x52454C4Fu, 0xCAFEBABEu};

static void dxblit_apply_relocs(void) {
  if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu)
    return;
  uintptr_t base = (uintptr_t)&g_dxblit_header;
  const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
  uint32_t count = tbl[0];
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = tbl[1 + i];
    *(uint64_t *)(base + off) += (uint64_t)base;
  }
  g_reloc_info[0] = 0;
}

static const dxblit_api_t g_dxblit_api = {
    .magic = DXBLIT_MAGIC,
    .version = 0x00020000u,
    .copy_rect = dxblit_copy_rect,
    .fill_rect = dxblit_fill_rect,
    .alpha_blend_rect = dxblit_alpha_blend_rect,
    .scale_nearest = dxblit_scale_nearest,
    .gradient_v = dxblit_gradient_v,
    .convert_565_to_8888 = dxblit_convert_565_to_8888,
    .dirty_reset = dxblit_dirty_reset,
    .dirty_add = dxblit_dirty_add,
    .dirty_count = dxblit_dirty_count,
    .dirty_get = dxblit_dirty_get};

__attribute__((section(".entry"), used)) const dxblit_api_t *
dxblit_dll_entry(const kernel_api_t *api) {
  (void)api;
  dxblit_apply_relocs();
  return &g_dxblit_api;
}
