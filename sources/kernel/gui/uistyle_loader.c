/**
 * kernel/gui/uistyle_loader.c
 * Loader dinamico out-of-kernel per i moduli UI Style in C:\NewOS\Resources\UIStyle\
 */

#include <fs/registry.h>
#include <fs/vfs.h>
#include <gui/graphics.h>
#include <gui/screen_mgr.h>
#include <gui/theme_mgr.h>
#include <gui/uistyle_loader.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*uistyle_entry_fn)(const kernel_api_t *api, int cmd,
                                uistyle_params_t *params);

static int g_active_uistyle = UISTYLE_STANDARD;
static uistyle_entry_fn g_uistyle_entry = NULL;
static uint8_t *g_uistyle_buf = NULL;
static bool g_uistyle_initialized = false;

static bool load_copenhagen_dll(void) {
  if (g_uistyle_entry)
    return true;

  const char *paths[] = {"C:\\NewOS\\Resources\\UIStyle\\COPENHAGEN.DLL",
                         "/NewOS/Resources/UIStyle/COPENHAGEN.DLL",
                         "C:\\NewOS\\SystemLibrary\\COPENHAGEN.DLL"};

  uint32_t sz = 0;
  for (int i = 0; i < 3; i++) {
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
    if (buf && sz >= sizeof(module_header_t)) {
      if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY,
                                   "COPENHAG")) {
        uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
        if (!exec_buf) {
          kfree(buf);
          return false;
        }
        memcpy(exec_buf, buf, sz);
        memset(exec_buf + sz, 0, 32768);
        kfree(buf);

        const module_header_t *h = (const module_header_t *)exec_buf;
        g_uistyle_entry = (uistyle_entry_fn)(exec_buf + h->entry_offset);
        g_uistyle_buf = exec_buf;

        uistyle_params_t p;
        memset(&p, 0, sizeof(p));
        g_uistyle_entry(screen_mgr_get_api(), UISTYLE_CMD_INIT, &p);
        return true;
      }
      kfree(buf);
    }
  }
  return false;
}

void uistyle_init(void) {
  if (g_uistyle_initialized)
    return;
  g_uistyle_initialized = true;

  uint32_t val = 0;
  if (registry_get_value_dword("HKCU\\Software\\NewOS\\Desktop", "UIStyle",
                               &val)) {
    if (val == UISTYLE_COPENHAGEN) {
      if (load_copenhagen_dll()) {
        g_active_uistyle = UISTYLE_COPENHAGEN;
      }
    }
  }
}

int uistyle_get_active(void) {
  if (!g_uistyle_initialized)
    uistyle_init();
  return g_active_uistyle;
}

bool uistyle_set_active(int style_id) {
  if (style_id == UISTYLE_COPENHAGEN) {
    if (!load_copenhagen_dll())
      return false;
    g_active_uistyle = UISTYLE_COPENHAGEN;
    // Allinea il tema delle finestre standard a Liquid Glass per coerenza visiva
    wm_set_theme(THEME_LIQUID_GLASS);
  } else {
    g_active_uistyle = UISTYLE_STANDARD;
  }

  if (!registry_is_read_only()) {
    registry_set_value_dword("HKCU\\Software\\NewOS\\Desktop", "UIStyle",
                             (uint32_t)g_active_uistyle);
    registry_save_to_disk();
  }
  return true;
}

void uistyle_draw_desktop(uint32_t sw, uint32_t sh) {
  if (g_active_uistyle != UISTYLE_COPENHAGEN || !g_uistyle_entry)
    return;
  uistyle_params_t p;
  memset(&p, 0, sizeof(p));
  p.screen_w = sw;
  p.screen_h = sh;
  g_uistyle_entry(screen_mgr_get_api(), UISTYLE_CMD_DRAW_DESKTOP, &p);
}

bool uistyle_handle_desktop_mouse(int32_t mx, int32_t my, bool clicked) {
  if (g_active_uistyle != UISTYLE_COPENHAGEN || !g_uistyle_entry)
    return false;
  uistyle_params_t p;
  memset(&p, 0, sizeof(p));
  p.screen_w = gfx_get_width();
  p.screen_h = gfx_get_height();
  p.mouse_x = mx;
  p.mouse_y = my;
  p.clicked = clicked;
  p.handled = false;
  g_uistyle_entry(screen_mgr_get_api(), UISTYLE_CMD_MOUSE_DESKTOP, &p);
  return p.handled;
}

bool uistyle_open_explorer(const char *path) {
  if (g_active_uistyle != UISTYLE_COPENHAGEN || !g_uistyle_entry)
    return false;
  uistyle_params_t p;
  memset(&p, 0, sizeof(p));
  p.screen_w = gfx_get_width();
  p.screen_h = gfx_get_height();
  p.path_arg = path;
  p.handled = false;
  g_uistyle_entry(screen_mgr_get_api(), UISTYLE_CMD_OPEN_EXPLORER, &p);
  return p.handled;
}

void uistyle_open_workspace(void) {
  if (g_active_uistyle != UISTYLE_COPENHAGEN || !g_uistyle_entry)
    return;
  uistyle_params_t p;
  memset(&p, 0, sizeof(p));
  p.screen_w = gfx_get_width();
  p.screen_h = gfx_get_height();
  g_uistyle_entry(screen_mgr_get_api(), UISTYLE_CMD_OPEN_WORKSPACE, &p);
}
