/**
 * SystemLibrary/compositor/nscomp_entry.c
 * Entry point NOSM + rilocazioni per NSCOMP.DLL
 */
#include "nscomp.h"
#include <kernel_ext_api.h>
#include <screen_modules.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_nscomp_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "NSCOMP",
    .signature = MODULE_AUTH_TAG};
#endif

static volatile uint32_t g_reloc_info[2] = {0x52454C4Fu, 0xCAFEBABEu};

static void nscomp_apply_relocs(void) {
  if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu)
    return;
  uintptr_t base = (uintptr_t)&g_nscomp_header;
  const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
  uint32_t count = tbl[0];
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = tbl[1 + i];
    *(uint64_t *)(base + off) += (uint64_t)base;
  }
  g_reloc_info[0] = 0;
}

static const nscomp_api_t g_nscomp_api = {
    .magic = NSCOMP_MAGIC,
    .version = 0x00020000u,
    .surface_init = nsc_surface_init,
    .surface_free = nsc_surface_free,
    .surface_mark_dirty = nsc_surface_mark_dirty,
    .surface_clear_dirty = nsc_surface_clear_dirty,
    .surface_is_dirty = nsc_surface_is_dirty,
    .blur_box_3x3 = nsc_blur_box_3x3,
    .shadow_alpha_edge = nsc_shadow_alpha_edge,
    .tint_rect = nsc_tint_rect,
    .apply_round_mask = nsc_apply_round_mask,
    .liquid_refraction = nsc_liquid_refraction};

__attribute__((section(".entry"), used)) const nscomp_api_t *
nscomp_dll_entry(const kernel_api_t *api) {
  (void)api;
  nscomp_apply_relocs();
  return &g_nscomp_api;
}
