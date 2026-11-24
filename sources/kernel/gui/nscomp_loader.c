/**
 * kernel/gui/nscomp_loader.c
 * Client Loader out-of-kernel per NSCOMP.DLL (Compositor & Rifrazione Liquid Glass)
 */
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/graphics.h>
#include <gui/nscomp_loader.h>
#include <gui/screen_mgr.h>
#include <kernel_ext_api.h>
#include <mm/heap.h>
#include <string.h>

#include "../../SystemLibrary/motion/nsmotion.h"

typedef const nscomp_api_t *(*nscomp_entry_fn)(const kernel_api_t *api);
typedef const nsmotion_api_t *(*nsmotion_entry_fn)(const kernel_api_t *api);

static const nscomp_api_t *g_nscomp_api = NULL;
static const nsmotion_api_t *g_nsmotion_api = NULL;

const nscomp_api_t *nscomp_get_api(void) {
  if (g_nscomp_api)
    return g_nscomp_api;
  if (!vfs_is_drive_mounted('C'))
    return NULL;

  const char *paths[] = {"C:\\NewOS\\SystemLibrary\\NSCOMP.DLL",
                         "/NewOS/SystemLibrary/NSCOMP.DLL"};
  nscomp_entry_fn entry = NULL;
  uint32_t sz = 0;
  for (int i = 0; i < 2 && !entry; i++) {
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
    if (buf && sz >= sizeof(module_header_t)) {
      if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY,
                                   "NSCOMP")) {
        const module_header_t *h = (const module_header_t *)buf;
        uint32_t alloc_sz = (h->code_size > sz ? h->code_size : sz) + 8192;
        uint8_t *exec_buf = (uint8_t *)kmalloc(alloc_sz);
        if (exec_buf) {
          memcpy(exec_buf, buf, sz);
          memset(exec_buf + sz, 0, alloc_sz - sz);
          entry = (nscomp_entry_fn)(exec_buf + h->entry_offset);
        }
      }
    }
    if (buf)
      kfree(buf);
  }
  if (entry) {
    const nscomp_api_t *api = entry(NULL);
    if (api && api->magic == NSCOMP_MAGIC)
      g_nscomp_api = api;
  }
  return g_nscomp_api;
}

void nscomp_apply_window_refraction(int32_t x, int32_t y, int32_t w, int32_t h,
                                    int32_t rim_px, int32_t strength_px) {
  const nscomp_api_t *api = nscomp_get_api();
  uint32_t *back = gfx_get_backbuffer();
  if (!api || !api->liquid_refraction || !back || w <= 0 || h <= 0)
    return;

  int32_t sw = (int32_t)gfx_get_width();
  int32_t sh = (int32_t)gfx_get_height();
  int32_t pitch = (int32_t)(gfx_get_pitch_bytes() / 4);
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > sw)
    w = sw - x;
  if (y + h > sh)
    h = sh - y;
  if (w <= 4 || h <= 4)
    return;

  uint32_t *tmp = (uint32_t *)kmalloc((size_t)w * (size_t)h * 4);
  if (!tmp)
    return;
  for (int32_t row = 0; row < h; row++) {
    memcpy(tmp + row * w, back + (y + row) * pitch + x, (size_t)w * 4);
  }
  api->liquid_refraction(back + y * pitch + x, tmp, pitch, w, w, h, rim_px,
                         strength_px);
  kfree(tmp);
}

const void *nsmotion_get_api(void) {
  if (g_nsmotion_api)
    return g_nsmotion_api;
  if (!vfs_is_drive_mounted('C'))
    return NULL;

  const char *paths[] = {"C:\\NewOS\\SystemLibrary\\NSMOTION.DLL",
                         "/NewOS/SystemLibrary/NSMOTION.DLL"};
  nsmotion_entry_fn entry = NULL;
  uint32_t sz = 0;
  for (int i = 0; i < 2 && !entry; i++) {
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
    if (buf && sz >= sizeof(module_header_t)) {
      if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY,
                                   "NSMOTION")) {
        const module_header_t *h = (const module_header_t *)buf;
        uint32_t alloc_sz = (h->code_size > sz ? h->code_size : sz) + 8192;
        uint8_t *exec_buf = (uint8_t *)kmalloc(alloc_sz);
        if (exec_buf) {
          memcpy(exec_buf, buf, sz);
          memset(exec_buf + sz, 0, alloc_sz - sz);
          entry = (nsmotion_entry_fn)(exec_buf + h->entry_offset);
        }
      }
    }
    if (buf)
      kfree(buf);
  }
  if (entry) {
    const nsmotion_api_t *api = entry(NULL);
    if (api && api->magic == NSMOTION_MAGIC)
      g_nsmotion_api = api;
  }
  return g_nsmotion_api;
}
