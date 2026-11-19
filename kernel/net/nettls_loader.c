#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <net/nettls_loader.h>
#include <string.h>

typedef int (*nettls_entry_fn)(const kernel_api_t *api, nettls_params_t *p);

static nettls_entry_fn g_nettls_entry = NULL;
static uint8_t *g_nettls_buf = NULL;

bool nettls_init(void) {
  if (g_nettls_entry)
    return true;

  const char *paths[] = {
      "C:\\NewOS\\SystemLibrary\\NETTLS.DLL",
      "C:\\NewOS\\Boot\\NETTLS.DLL",
      "/NewOS/SystemLibrary/NETTLS.DLL",
      "NETTLS.DLL"
  };

  uint32_t sz = 0;
  for (int i = 0; i < 4 && !g_nettls_entry; i++) {
    uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
    if (buf && sz >= sizeof(module_header_t)) {
      if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
        uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 65536);
        if (exec_buf) {
          memcpy(exec_buf, buf, sz);
          memset(exec_buf + sz, 0, 65536);
          const module_header_t *h = (const module_header_t *)exec_buf;
          g_nettls_entry = (nettls_entry_fn)(exec_buf + h->entry_offset);
          g_nettls_buf = exec_buf;
        }
      }
    }
    if (buf)
      kfree(buf);
  }

  if (!g_nettls_entry)
    return false;

  nettls_params_t p;
  memset(&p, 0, sizeof(p));
  p.cmd = NETTLS_CMD_INIT;
  return (g_nettls_entry(screen_mgr_get_api(), &p) == 0);
}

bool nettls_http_get(const char *url, uint32_t max_body_bytes, nettls_response_t *out_resp) {
  if (!url || !out_resp)
    return false;
  memset(out_resp, 0, sizeof(*out_resp));
  out_resp->max_body_bytes = max_body_bytes ? max_body_bytes : (256U * 1024U);

  if (!nettls_init()) {
    strcpy(out_resp->error_msg, "Modulo NETTLS.DLL non disponibile");
    return false;
  }

  nettls_params_t p;
  p.cmd = NETTLS_CMD_HTTP_GET;
  p.url = url;
  p.resp = out_resp;
  return (g_nettls_entry(screen_mgr_get_api(), &p) == 0);
}

void nettls_free_response(nettls_response_t *resp) {
  if (!resp)
    return;
  if (resp->body) {
    kfree(resp->body);
    resp->body = NULL;
  }
  resp->body_len = 0;
}

