/**
 * SystemLibrary/motion/nsmotion_entry.c
 * Entry point NOSM + rilocazioni per NSMOTION.DLL
 */
#include "nsmotion.h"
#include <kernel_ext_api.h>
#include <screen_modules.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_nsmotion_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "NSMOTION",
    .signature = MODULE_AUTH_TAG};
#endif

static volatile uint32_t g_reloc_info[2] = {0x52454C4Fu, 0xCAFEBABEu};

static void nsmotion_apply_relocs(void) {
  if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu)
    return;
  uintptr_t base = (uintptr_t)&g_nsmotion_header;
  const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
  uint32_t count = tbl[0];
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = tbl[1 + i];
    *(uint64_t *)(base + off) += (uint64_t)base;
  }
  g_reloc_info[0] = 0;
}

static const nsmotion_api_t g_nsmotion_api = {
    .magic = NSMOTION_MAGIC,
    .version = 0x00020000u,
    .set_enabled = nsm_set_enabled,
    .is_enabled = nsm_is_enabled,
    .ease_q16 = nsm_ease_q16,
    .bezier_q16 = nsm_bezier_q16,
    .lerp_i32 = nsm_lerp_i32,
    .lerp_argb = nsm_lerp_argb,
    .lerp_vec2 = nsm_lerp_vec2,
    .lerp_rect = nsm_lerp_rect,
    .eval_keyframes = nsm_eval_keyframes,
    .track_start = nsm_track_start,
    .track_step = nsm_track_step,
    .spring_step = nsm_spring_step,
    .spring2d_step = nsm_spring2d_step};

__attribute__((section(".entry"), used)) const nsmotion_api_t *
nsmotion_dll_entry(const kernel_api_t *api) {
  (void)api;
  nsmotion_apply_relocs();
  return &g_nsmotion_api;
}
