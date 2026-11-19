/**
 * kernel/gui/comdlg_loader.c
 * Loader dinamico out-of-kernel per COMDLG.DLL
 * (C:\NewOS\SystemLibrary\COMDLG.DLL) Gestisce Dialoghi Apri/Salva, Operazioni
 * File/Cestino e Trasferimento Stack FTP <-> PC. (C) Newspoint Software -
 * NewSoft Dynamic System Subsystem
 */

#include <fs/vfs.h>
#include <gui/graphics.h>
#include <gui/screen_mgr.h>
#include <gui/wm.h>
#include <include/comdlg.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*comdlg_dll_entry_fn)(const kernel_api_t *api, window_t *win,
                                   comdlg_request_t *req);
typedef int (*recycle_dll_entry_fn)(const kernel_api_t *api, window_t *win,
                                    recycle_request_t *req);

static comdlg_dll_entry_fn g_comdlg_entry = NULL;
static uint8_t *g_comdlg_buf = NULL;
static recycle_dll_entry_fn g_recycle_entry = NULL;
static uint8_t *g_recycle_buf = NULL;

static bool load_recycle_dll(void) {
  if (g_recycle_entry)
    return true;

  const char *prefixes[] = {"C:\\NewOS\\SystemLibrary\\",
                            "C:\\NewOS\\Boot\\",
                            "D:\\NewOS\\SystemLibrary\\",
                            "/NewOS/SystemLibrary/",
                            "/NewOS/Boot/",
                            ""};

  char full_path[96];
  uint32_t sz = 0;

  for (int i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); i++) {
    mini_snprintf(full_path, sizeof(full_path), "%sRECYCLE.DLL", prefixes[i]);
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(full_path, &sz);
    if (buf && sz >= sizeof(module_header_t)) {
      if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY,
                                   "RECYCLE")) {
        uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 16384);
        if (!exec_buf) {
          kfree(buf);
          return false;
        }
        memcpy(exec_buf, buf, sz);
        memset(exec_buf + sz, 0, 16384);
        kfree(buf);

        const module_header_t *h = (const module_header_t *)exec_buf;
        g_recycle_entry = (recycle_dll_entry_fn)(exec_buf + h->entry_offset);
        g_recycle_buf = exec_buf;
        return true;
      }
      kfree(buf);
    }
  }
  return false;
}

static bool load_comdlg_dll(void) {
  if (g_comdlg_entry)
    return true;

  const char *prefixes[] = {"C:\\NewOS\\SystemLibrary\\",
                            "C:\\NewOS\\Boot\\",
                            "D:\\NewOS\\SystemLibrary\\",
                            "/NewOS/SystemLibrary/",
                            "/NewOS/Boot/",
                            ""};

  char full_path[96];
  uint32_t sz = 0;

  for (int i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); i++) {
    mini_snprintf(full_path, sizeof(full_path), "%sCOMDLG.DLL", prefixes[i]);
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(full_path, &sz);
    if (buf && sz >= sizeof(module_header_t)) {
      if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY,
                                   "COMDLG")) {
        // Alloca cuscinetto BSS azzerato (32 KB) per variabili statiche del
        // modulo
        uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
        if (!exec_buf) {
          kfree(buf);
          return false;
        }
        memcpy(exec_buf, buf, sz);
        memset(exec_buf + sz, 0, 32768);
        kfree(buf);

        const module_header_t *h = (const module_header_t *)exec_buf;
        g_comdlg_entry = (comdlg_dll_entry_fn)(exec_buf + h->entry_offset);
        g_comdlg_buf = exec_buf;
        return true;
      }
      kfree(buf);
    }
  }
  return false;
}

bool comdlg_open_file_dialog(const char *initial_dir, comdlg_file_cb_t cb,
                             void *user_data) {
  if (!load_comdlg_dll())
    return false;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t dw = 480, dh = 360;
  int32_t dx = ((int32_t)sw - dw) / 2;
  int32_t dy = ((int32_t)sh - dh) / 2;

  window_t *win =
      wm_create_window("Apri file (COMDLG.DLL)", dx, dy, dw, dh, APP_COMDLG);
  if (!win)
    return false;

  comdlg_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = COMDLG_MODE_OPEN_FILE;
  strncpy(req.initial_dir,
          (initial_dir && initial_dir[0]) ? initial_dir : "C:\\",
          sizeof(req.initial_dir) - 1);
  req.on_complete = cb;
  req.user_data = user_data;

  if (g_comdlg_entry(screen_mgr_get_api(), win, &req) != 0) {
    wm_destroy_window(win->id);
    return false;
  }
  return true;
}

bool comdlg_save_file_dialog(const char *initial_dir, const char *default_name,
                             comdlg_file_cb_t cb, void *user_data) {
  if (!load_comdlg_dll())
    return false;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t dw = 480, dh = 360;
  int32_t dx = ((int32_t)sw - dw) / 2;
  int32_t dy = ((int32_t)sh - dh) / 2;

  window_t *win = wm_create_window("Salva con nome (COMDLG.DLL)", dx, dy, dw,
                                   dh, APP_COMDLG);
  if (!win)
    return false;

  comdlg_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = COMDLG_MODE_SAVE_FILE;
  strncpy(req.initial_dir,
          (initial_dir && initial_dir[0]) ? initial_dir : "C:\\",
          sizeof(req.initial_dir) - 1);
  if (default_name && default_name[0]) {
    strncpy(req.default_name, default_name, sizeof(req.default_name) - 1);
  } else {
    strcpy(req.default_name, "NUOVO.TXT");
  }
  req.on_complete = cb;
  req.user_data = user_data;

  if (g_comdlg_entry(screen_mgr_get_api(), win, &req) != 0) {
    wm_destroy_window(win->id);
    return false;
  }
  return true;
}

bool comdlg_exec_file_op(comdlg_op_type_t op, const char *src_path,
                         const char *dst_path) {
  if (op == COMDLG_OP_RECYCLE) {
    return recycle_delete_file(src_path);
  }
  if (op == COMDLG_OP_RESTORE) {
    return recycle_restore_file(src_path);
  }
  if (op == COMDLG_OP_EMPTY_RECYCLE) {
    return recycle_empty_bin();
  }
  if (!load_comdlg_dll())
    return false;

  comdlg_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = COMDLG_MODE_EXEC_OP;
  req.op_type = (int)op;
  if (src_path)
    strncpy(req.src_path, src_path, sizeof(req.src_path) - 1);
  if (dst_path)
    strncpy(req.dst_path, dst_path, sizeof(req.dst_path) - 1);

  return (g_comdlg_entry(screen_mgr_get_api(), NULL, &req) == 0);
}

void app_comdlg_ftp_stack_launch(void) {
  if (!load_comdlg_dll())
    return;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t dw = 580, dh = 420;
  int32_t dx = ((int32_t)sw - dw) / 2;
  int32_t dy = ((int32_t)sh - dh) / 2;
  if (dx < 8)
    dx = 8;
  if (dy < 8)
    dy = 8;

  window_t *win = wm_create_window("Gestore Stack File & Rete FTP (COMDLG.DLL)",
                                   dx, dy, dw, dh, APP_COMDLG);
  if (!win)
    return;

  comdlg_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = COMDLG_MODE_FTP_STACK_MGR;
  strcpy(req.initial_dir, "C:\\");

  if (g_comdlg_entry(screen_mgr_get_api(), win, &req) != 0) {
    wm_destroy_window(win->id);
  }
}

void app_comdlg_copy_move_launch(int op_type, const char *src_path,
                                 const char *dst_dir) {
  if (!load_comdlg_dll())
    return;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t dw = 460, dh = 260;
  int32_t dx = ((int32_t)sw - dw) / 2;
  int32_t dy = ((int32_t)sh - dh) / 2;
  if (dx < 12)
    dx = 12;
  if (dy < 12)
    dy = 12;

  const char *title = "0% completato";

  window_t *win = wm_create_window(title, dx, dy, dw, dh, APP_COMDLG);
  if (!win)
    return;

  comdlg_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = COMDLG_MODE_COPY_MOVE_DLG;
  req.op_type = op_type;
  if (src_path && src_path[0]) {
    strncpy(req.src_path, src_path, sizeof(req.src_path) - 1);
  }
  if (dst_dir && dst_dir[0]) {
    strncpy(req.dst_path, dst_dir, sizeof(req.dst_path) - 1);
  } else {
    strcpy(req.dst_path, "C:\\User\\Morgan\\Desktop");
  }

  if (g_comdlg_entry(screen_mgr_get_api(), win, &req) != 0) {
    wm_destroy_window(win->id);
  }
}

void app_recycle_settings_launch(void) {
  if (!load_recycle_dll())
    return;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t dw = 500, dh = 395;
  int32_t dx = ((int32_t)sw - dw) / 2;
  int32_t dy = ((int32_t)sh - dh) / 2;
  if (dx < 10)
    dx = 10;
  if (dy < 10)
    dy = 10;

  window_t *win = wm_create_window("Proprieta e Gestione Cestino (RECYCLE.DLL)",
                                   dx, dy, dw, dh, APP_COMDLG);
  if (!win)
    return;

  recycle_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = RECYCLE_MODE_SETTINGS_DLG;

  if (g_recycle_entry(screen_mgr_get_api(), win, &req) != 0) {
    wm_destroy_window(win->id);
  }
}

void app_recycle_quota_prompt_launch(const char *target_path) {
  if (!load_recycle_dll())
    return;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  int32_t dw = 470, dh = 285;
  int32_t dx = ((int32_t)sw - dw) / 2;
  int32_t dy = ((int32_t)sh - dh) / 2;
  if (dx < 12)
    dx = 12;
  if (dy < 12)
    dy = 12;

  window_t *win = wm_create_window("Limite spazio Cestino superato (RECYCLE.DLL)",
                                   dx, dy, dw, dh, APP_COMDLG);
  if (!win)
    return;

  recycle_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = RECYCLE_MODE_QUOTA_PROMPT;
  if (target_path && target_path[0]) {
    strncpy(req.target_path, target_path, sizeof(req.target_path) - 1);
  }

  if (g_recycle_entry(screen_mgr_get_api(), win, &req) != 0) {
    wm_destroy_window(win->id);
  }
}

bool recycle_delete_file(const char *target_path) {
  if (!target_path || !target_path[0])
    return false;
  if (!load_recycle_dll())
    return false;

  recycle_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = RECYCLE_MODE_DELETE_CHECK;
  strncpy(req.target_path, target_path, sizeof(req.target_path) - 1);

  int rc = g_recycle_entry(screen_mgr_get_api(), NULL, &req);
  if (rc == 1) {
    // Limite spazio superato -> Apre automaticamente la finestra che suggerisce l'eliminazione diretta!
    app_recycle_quota_prompt_launch(target_path);
    return true;
  }
  return (rc == 0);
}

bool recycle_restore_file(const char *basename) {
  if (!basename || !basename[0])
    return false;
  if (!load_recycle_dll())
    return false;

  recycle_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = RECYCLE_MODE_RESTORE_FILE;
  strncpy(req.target_path, basename, sizeof(req.target_path) - 1);
  return (g_recycle_entry(screen_mgr_get_api(), NULL, &req) == 0);
}

bool recycle_empty_bin(void) {
  if (!load_recycle_dll())
    return false;

  recycle_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = RECYCLE_MODE_EMPTY_BIN;
  return (g_recycle_entry(screen_mgr_get_api(), NULL, &req) == 0);
}

bool recycle_get_stats(uint32_t *out_used_bytes, uint32_t *out_limit_bytes,
                       uint32_t *out_count) {
  if (!load_recycle_dll())
    return false;

  recycle_request_t req;
  memset(&req, 0, sizeof(req));
  req.mode = RECYCLE_MODE_GET_STATS;
  if (g_recycle_entry(screen_mgr_get_api(), NULL, &req) != 0)
    return false;

  if (out_used_bytes)
    *out_used_bytes = req.out_used_bytes;
  if (out_limit_bytes)
    *out_limit_bytes = req.out_limit_bytes;
  if (out_count)
    *out_count = req.out_file_count;
  return true;
}
