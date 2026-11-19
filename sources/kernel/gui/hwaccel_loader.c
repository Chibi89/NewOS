#include <fs/registry.h>
#include <fs/vfs.h>
#include <gui/graphics.h>
#include <gui/hwaccel_loader.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*hwaccel_entry_fn)(const kernel_api_t *api, hwaccel_params_t *p);

static hwaccel_entry_fn g_hwaccel_entry = NULL;
static uint8_t *g_hwaccel_buf = NULL;
static hwaccel_info_t g_hwaccel_info;
static bool g_hwaccel_ready = false;

static void hwaccel_sync_gfx_scale(void) {
  uint32_t sc = g_hwaccel_info.scale_percent;
  if (sc < 100 || sc > 200)
    sc = 100;
  gfx_set_ui_scale(sc);
}

bool hwaccel_init(uint64_t lfb_base, uint32_t width, uint32_t height) {
  memset(&g_hwaccel_info, 0, sizeof(g_hwaccel_info));
  g_hwaccel_info.lfb_base = lfb_base;
  g_hwaccel_info.detected_dpi = 96;
  g_hwaccel_info.scale_percent = 100;
  strcpy(g_hwaccel_info.gpu_name, "Standard VESA/BGA Framebuffer");
  strcpy(g_hwaccel_info.monitor_name, "Generic PnP Monitor");

  if (!g_hwaccel_entry) {
    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\HWACCEL.DLL",
        "C:\\NewOS\\Boot\\HWACCEL.DLL",
        "/NewOS/SystemLibrary/HWACCEL.DLL",
        "HWACCEL.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_hwaccel_entry; i++) {
      uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
      if (buf && sz >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
          uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
          if (exec_buf) {
            memcpy(exec_buf, buf, sz);
            memset(exec_buf + sz, 0, 32768);
            const module_header_t *h = (const module_header_t *)exec_buf;
            g_hwaccel_entry = (hwaccel_entry_fn)(exec_buf + h->entry_offset);
            g_hwaccel_buf = exec_buf;
          }
        }
      }
      if (buf)
        kfree(buf);
    }
  }

  if (!g_hwaccel_entry) {
    g_hwaccel_ready = false;
    hwaccel_sync_gfx_scale();
    return false;
  }

  hwaccel_params_t p;
  memset(&p, 0, sizeof(p));
  p.cmd = HWACCEL_CMD_INIT;
  p.lfb_base = lfb_base;
  p.width = width;
  p.height = height;
  p.info_out = &g_hwaccel_info;

  int rc = g_hwaccel_entry(screen_mgr_get_api(), &p);
  g_hwaccel_ready = (rc == 0 && g_hwaccel_info.initialized);
  hwaccel_sync_gfx_scale();
  return g_hwaccel_ready;
}

bool hwaccel_is_ready(void) {
  return g_hwaccel_ready && g_hwaccel_entry != NULL;
}

bool hwaccel_blit_32(void *dst, const void *src, uint32_t num_pixels) {
  if (!hwaccel_is_ready() || !dst || !src || num_pixels == 0)
    return false;
  hwaccel_params_t p;
  p.cmd = HWACCEL_CMD_BLIT32;
  p.dst = dst;
  p.src = src;
  p.count_pixels = num_pixels;
  p.info_out = &g_hwaccel_info;
  return (g_hwaccel_entry(screen_mgr_get_api(), &p) == 0);
}

bool hwaccel_fill_32(void *dst, uint32_t color, uint32_t num_pixels) {
  if (!hwaccel_is_ready() || !dst || num_pixels == 0)
    return false;
  hwaccel_params_t p;
  p.cmd = HWACCEL_CMD_FILL32;
  p.dst = dst;
  p.fill_color = color;
  p.count_pixels = num_pixels;
  p.info_out = &g_hwaccel_info;
  return (g_hwaccel_entry(screen_mgr_get_api(), &p) == 0);
}

void hwaccel_on_resolution_changed(uint32_t width, uint32_t height) {
  if (!hwaccel_is_ready())
    return;
  hwaccel_params_t p;
  memset(&p, 0, sizeof(p));
  p.cmd = HWACCEL_CMD_RECALC_DPI;
  p.width = width;
  p.height = height;
  p.info_out = &g_hwaccel_info;
  g_hwaccel_entry(screen_mgr_get_api(), &p);
  hwaccel_sync_gfx_scale();
}

void hwaccel_set_scale(uint32_t mode, uint32_t percent) {
  if (percent < 100 || percent > 200)
    percent = 100;
  if (!hwaccel_is_ready()) {
    g_hwaccel_info.scale_mode = mode;
    g_hwaccel_info.scale_percent = percent;
    hwaccel_sync_gfx_scale();
    return;
  }
  hwaccel_params_t p;
  memset(&p, 0, sizeof(p));
  p.cmd = HWACCEL_CMD_SET_SCALE;
  p.width = gfx_get_width();
  p.height = gfx_get_height();
  p.scale_mode = mode;
  p.scale_percent = percent;
  p.info_out = &g_hwaccel_info;
  g_hwaccel_entry(screen_mgr_get_api(), &p);
  hwaccel_sync_gfx_scale();
}

const hwaccel_info_t *hwaccel_get_info(void) {
  return &g_hwaccel_info;
}
