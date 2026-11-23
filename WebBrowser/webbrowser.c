/**
 * WebBrowser/webbrowser.c
 * NewSoft Web Browser per NewOS V1 (64-bit)
 * (C) Copyright Newspoint Software - NewSoft - NewOS V1
 * @credits: Basato sui principi e architettura del motore NetSurf (C89/C99)
 * @thanks: NetSurf Project (http://www.netsurf-browser.org/)
 */

#include "webbrowser.h"
#include "cssengine.h"
#include "imgcodec.h"
#include "jsengine.h"
#include "netsurf_core.h"
#include <arch/x86_64/io.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <fs/vfs.h>
#include <gui/fontmgr.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <net/dns.h>
#include <net/nettls_loader.h>
#include <net/socket.h>
#include <string.h>

#define MAX_HISTORY 8
#define MAX_URL_LEN 512
#define MAX_TITLE_LEN 64
#define MAX_STATUS_LEN 96
#define MAX_HTML_SIZE 1572864
#define MAX_ELEMENTS 2048

typedef enum {
  ELEM_TEXT = 0,
  ELEM_H1,
  ELEM_H2,
  ELEM_H3,
  ELEM_LINK,
  ELEM_HR,
  ELEM_LIST_ITEM,
  ELEM_PRE,
  ELEM_BLOCKQUOTE,
  ELEM_HEADER,
  ELEM_NAV,
  ELEM_VIDEO,
  ELEM_BUTTON,
  ELEM_IMG
} elem_type_t;

#define MAX_CACHED_IMGS 16

typedef struct {
  char url[256];
  uint32_t *pixels;
  int w;
  int h;
  bool attempted;
} browser_img_t;

typedef struct {
  elem_type_t type;
  char text[120];
  char href[256];
  char badge[16];
  uint32_t color;
  uint32_t bg_color;
  uint32_t border_color;
  bool has_bg;
  bool has_border;
  bool has_shadow;
  bool no_underline;
  bool is_bold;
  uint8_t border_radius;
  uint8_t font_size;
  uint8_t text_align;
  uint8_t text_deco;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  int16_t img_idx;
} browser_elem_t;

#define MAX_BROWSER_TABS 5
#define MAX_DOWNLOADS 8
#define MAX_PERSIST_HIST 20

typedef struct {
  char url[MAX_URL_LEN];
  char title[32];
  int32_t scroll_y;
} browser_tab_t;

typedef struct {
  char filename[48];
  char url[MAX_URL_LEN];
  uint32_t size;
  bool done;
} browser_download_t;

typedef struct {
  char current_url[MAX_URL_LEN];
  char edit_url[MAX_URL_LEN];
  char page_title[MAX_TITLE_LEN];
  char status_msg[MAX_STATUS_LEN];
  bool url_focused;
  int cursor_pos;

  // Cronologia di navigazione
  char history_back[MAX_HISTORY][MAX_URL_LEN];
  int history_back_count;
  char history_fwd[MAX_HISTORY][MAX_URL_LEN];
  int history_fwd_count;

  // Multi-Tab, Download Manager e Cronologia persistente
  browser_tab_t tabs[MAX_BROWSER_TABS];
  int num_tabs;
  int active_tab;
  browser_download_t downloads[MAX_DOWNLOADS];
  int num_downloads;
  char visit_history[MAX_PERSIST_HIST][MAX_URL_LEN];
  int visit_count;

  // Buffer pagina (allocato dinamicamente fino a 512 KB)
  char *raw_html;
  uint32_t raw_html_len;

  // Elementi di layout calcolati (allocati dinamicamente fino a 2048)
  browser_elem_t *elems;
  int num_elems;
  int32_t content_height;
  int32_t scroll_y;
  int hover_elem;

  // Stato UI
  bool is_loading;
  bool is_secure_local;
  int quick_fav_hover;
  bool is_offline;
  uint32_t page_bg_color;
  bool has_page_bg;

  // Cache immagini della pagina corrente (decodificate da IMGCODEC.DLL)
  browser_img_t imgs[MAX_CACHED_IMGS];
  int num_imgs;
} browser_state_t;

static void serial_log(const char *s);

static void browser_clear_images(browser_state_t *state) {
  if (!state) return;
  for (int i = 0; i < MAX_CACHED_IMGS; i++) {
    if (state->imgs[i].pixels) {
      imgcodec_free(state->imgs[i].pixels);
      state->imgs[i].pixels = NULL;
    }
    state->imgs[i].url[0] = '\0';
    state->imgs[i].w = 0;
    state->imgs[i].h = 0;
    state->imgs[i].attempted = false;
  }
  state->num_imgs = 0;
}

static void browser_ensure_user_dirs(void) {
  if (!vfs_exists("C:\\User")) vfs_mkdir("C:\\User");
  if (!vfs_exists("C:\\User\\Admin")) vfs_mkdir("C:\\User\\Admin");
  if (!vfs_exists("C:\\User\\Admin\\Downloads")) vfs_mkdir("C:\\User\\Admin\\Downloads");
  if (!vfs_exists("C:\\User\\Admin\\AppData")) vfs_mkdir("C:\\User\\Admin\\AppData");
}

static void browser_record_history(browser_state_t *state, const char *url) {
  if (!state || !url || !url[0]) return;
  if (strcmp(url, "about:history") == 0 || strcmp(url, "about:downloads") == 0) return;

  if (state->visit_count == 0 || strcmp(state->visit_history[0], url) != 0) {
    int limit = (state->visit_count < MAX_PERSIST_HIST) ? state->visit_count : (MAX_PERSIST_HIST - 1);
    for (int i = limit; i > 0; i--) {
      strcpy(state->visit_history[i], state->visit_history[i - 1]);
    }
    strncpy(state->visit_history[0], url, MAX_URL_LEN - 1);
    state->visit_history[0][MAX_URL_LEN - 1] = '\0';
    if (state->visit_count < MAX_PERSIST_HIST) state->visit_count++;
  }

  browser_ensure_user_dirs();
  char *buf = (char *)kmalloc(4096);
  if (buf) {
    buf[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < state->visit_count; i++) {
      size_t ulen = strlen(state->visit_history[i]);
      if (pos + ulen + 2 >= 4096) break;
      memcpy(buf + pos, state->visit_history[i], ulen);
      pos += ulen;
      buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    if (!vfs_exists("C:\\User\\Admin\\AppData\\browser_history.txt")) {
      vfs_create("C:\\User\\Admin\\AppData\\browser_history.txt");
    }
    vfs_write_entire_file_inplace("C:\\User\\Admin\\AppData\\browser_history.txt", buf, (uint32_t)pos);
    kfree(buf);
  }
}

static void browser_save_current_download(browser_state_t *state) {
  if (!state || !state->raw_html || state->raw_html_len == 0) return;
  browser_ensure_user_dirs();

  char fname[48];
  const char *src = state->current_url;
  const char *slash = strrchr(src, '/');
  const char *cand = (slash && slash[1]) ? (slash + 1) : "pagina";
  size_t fi = 0;
  while (cand[fi] && cand[fi] != '?' && cand[fi] != '&' && fi < 32) {
    char c = cand[fi];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
      fname[fi] = c;
    } else {
      fname[fi] = '_';
    }
    fi++;
  }
  if (fi == 0) {
    strcpy(fname, "pagina");
    fi = 6;
  }
  fname[fi] = '\0';
  if (!strchr(fname, '.')) {
    strncat(fname, ".htm", sizeof(fname) - strlen(fname) - 1);
  }

  char full_path[128];
  mini_snprintf(full_path, sizeof(full_path), "C:\\User\\Admin\\Downloads\\%s", fname);
  if (!vfs_exists(full_path)) vfs_create(full_path);
  bool ok = vfs_write_entire_file_inplace(full_path, state->raw_html, state->raw_html_len);

  if (ok) {
    int idx = (state->num_downloads < MAX_DOWNLOADS) ? state->num_downloads++ : (MAX_DOWNLOADS - 1);
    if (idx == MAX_DOWNLOADS - 1 && state->num_downloads == MAX_DOWNLOADS) {
      for (int i = 0; i < MAX_DOWNLOADS - 1; i++) state->downloads[i] = state->downloads[i + 1];
    }
    strncpy(state->downloads[idx].filename, fname, sizeof(state->downloads[idx].filename) - 1);
    strncpy(state->downloads[idx].url, state->current_url, MAX_URL_LEN - 1);
    state->downloads[idx].size = state->raw_html_len;
    state->downloads[idx].done = true;
    mini_snprintf(state->status_msg, sizeof(state->status_msg),
                  "Download completato: C:\\User\\Admin\\Downloads\\%s (%u B)",
                  fname, (unsigned)state->raw_html_len);
  } else {
    mini_snprintf(state->status_msg, sizeof(state->status_msg),
                  "Errore salvataggio in Downloads");
  }
}

static void generate_downloads_page(browser_state_t *state) {
  char *p = state->raw_html;
  size_t cap = MAX_HTML_SIZE;
  int len = mini_snprintf(p, cap,
      "<html><head><title>Gestore Download - NewOS</title></head>"
      "<body bgcolor=\"#F8FAFC\">"
      "<h1>Gestore Download (C:\\User\\Admin\\Downloads\\)</h1>"
      "<p>File scaricati nella sessione corrente e salvati su disco VFS:</p><hr>");
  if (state->num_downloads == 0) {
    len += mini_snprintf(p + len, cap - len,
        "<p><i>Nessun download completato. Usa il pulsante 'Salva' nella toolbar per salvare una pagina o risorsa.</i></p>");
  } else {
    for (int i = state->num_downloads - 1; i >= 0; i--) {
      len += mini_snprintf(p + len, cap - len,
          "<p><b>[100%% Completato] %s</b> — %u byte<br>"
          "Origine: %s<br>Percorso: C:\\User\\Admin\\Downloads\\%s</p><hr>",
          state->downloads[i].filename, (unsigned)state->downloads[i].size,
          state->downloads[i].url, state->downloads[i].filename);
    }
  }
  len += mini_snprintf(p + len, cap - len,
      "<p><a href=\"about:home\">Torna alla Home</a></p></body></html>");
  state->raw_html_len = (uint32_t)len;
}

static void generate_history_page(browser_state_t *state) {
  char *p = state->raw_html;
  size_t cap = MAX_HTML_SIZE;
  int len = mini_snprintf(p, cap,
      "<html><head><title>Cronologia & Cache - NewOS</title></head>"
      "<body bgcolor=\"#F8FAFC\">"
      "<h1>Cronologia Navigazione (C:\\User\\Admin\\AppData\\)</h1>"
      "<p>URL visitati salvati su disco:</p><hr>");
  if (state->visit_count == 0) {
    len += mini_snprintf(p + len, cap - len, "<p><i>Cronologia vuota.</i></p>");
  } else {
    for (int i = 0; i < state->visit_count; i++) {
      len += mini_snprintf(p + len, cap - len,
          "<p>%d. <a href=\"%s\">%s</a></p>",
          i + 1, state->visit_history[i], state->visit_history[i]);
    }
  }
  len += mini_snprintf(p + len, cap - len,
      "<hr><p><a href=\"about:home\">Torna alla Home</a></p></body></html>");
  state->raw_html_len = (uint32_t)len;
}

static void resolve_img_url(const char *base_url, const char *src, char *out, size_t out_sz) {
  out[0] = '\0';
  if (!src || !src[0]) return;
  if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
    strncpy(out, src, out_sz - 1);
    out[out_sz - 1] = '\0';
    return;
  }
  if (src[0] == '/' && src[1] == '/') {
    mini_snprintf(out, out_sz, "https:%s", src);
    return;
  }
  if (!base_url || !base_url[0]) return;
  const char *scheme_end = strstr(base_url, "://");
  if (!scheme_end) return;
  const char *host_start = scheme_end + 3;
  const char *path_start = strchr(host_start, '/');
  size_t origin_len = path_start ? (size_t)(path_start - base_url) : strlen(base_url);
  if (origin_len >= out_sz - 2) origin_len = out_sz - 2;
  if (src[0] == '/') {
    memcpy(out, base_url, origin_len);
    out[origin_len] = '\0';
    size_t rem = out_sz - 1 - origin_len;
    strncat(out, src, rem);
  } else {
    memcpy(out, base_url, origin_len);
    out[origin_len] = '/';
    out[origin_len + 1] = '\0';
    size_t rem = out_sz - 2 - origin_len;
    strncat(out, src, rem);
  }
}

static int browser_alloc_image_slot(browser_state_t *state, const char *abs_url) {
  if (!state || !abs_url || !abs_url[0]) return -1;
  for (int i = 0; i < state->num_imgs && i < 3; i++) {
    if (strcmp(state->imgs[i].url, abs_url) == 0) return i;
  }
  if (state->num_imgs >= MAX_CACHED_IMGS) return -1;
  int idx = state->num_imgs++;
  strncpy(state->imgs[idx].url, abs_url, sizeof(state->imgs[idx].url) - 1);
  state->imgs[idx].url[sizeof(state->imgs[idx].url) - 1] = '\0';
  state->imgs[idx].pixels = NULL;
  state->imgs[idx].w = 0;
  state->imgs[idx].h = 0;
  state->imgs[idx].attempted = false;
  return idx;
}

static void browser_fetch_images(browser_state_t *state) {
  if (!state || state->num_imgs <= 0) return;
  if (strncmp(state->current_url, "http://", 7) != 0 &&
      strncmp(state->current_url, "https://", 8) != 0) {
    return;
  }
  for (int i = 0; i < state->num_imgs && i < 6; i++) {
    if (state->imgs[i].attempted || !state->imgs[i].url[0]) continue;
    state->imgs[i].attempted = true;

    int target_w = 224, target_h = 126;
    for (int e = 0; e < state->num_elems; e++) {
      if (state->elems[e].type == ELEM_IMG && state->elems[e].img_idx == i) {
        if (state->elems[e].w > 0) target_w = state->elems[e].w;
        if (state->elems[e].h > 0) target_h = state->elems[e].h;
        break;
      }
    }

    nettls_response_t resp;
    memset(&resp, 0, sizeof(resp));
    if (nettls_http_get(state->imgs[i].url, 524288, &resp) &&
        resp.body && resp.body_len > 16) {
      uint32_t *pix = NULL;
      int iw = 0, ih = 0, fmt = 0;
      if (imgcodec_decode((const uint8_t *)resp.body, resp.body_len,
                          target_w, target_h, &pix, &iw, &ih, &fmt) && pix) {
        state->imgs[i].pixels = pix;
        state->imgs[i].w = iw;
        state->imgs[i].h = ih;
      }
    }
    nettls_free_response(&resp);
  }
}

static void serial_log(const char *s) {
  if (!s)
    return;
  while (*s)
    outb(0x3F8, *s++);
}

static void browser_fill_round_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                                    int32_t r, uint32_t color) {
  if (w <= 0 || h <= 0) return;
  if (r <= 0) { gfx_fill_rect(x, y, w, h, color); return; }
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  for (int32_t py = 0; py < h; py++) {
    int32_t dy = (py < r) ? (r - 1 - py) : ((py >= h - r) ? (py - (h - r)) : 0);
    for (int32_t px = 0; px < w; px++) {
      int32_t dx = (px < r) ? (r - 1 - px) : ((px >= w - r) ? (px - (w - r)) : 0);
      if (dx > 0 && dy > 0 && (dx * dx + dy * dy > r * r)) continue;
      gfx_draw_pixel(x + px, y + py, color);
    }
  }
}

static void browser_stroke_round_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                                      int32_t r, uint32_t color) {
  if (w <= 2 || h <= 2) return;
  if (r <= 0) {
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
    return;
  }
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  for (int32_t py = 0; py < h; py++) {
    int32_t dy = (py < r) ? (r - 1 - py) : ((py >= h - r) ? (py - (h - r)) : 0);
    for (int32_t px = 0; px < w; px++) {
      int32_t dx = (px < r) ? (r - 1 - px) : ((px >= w - r) ? (px - (w - r)) : 0);
      int32_t d2 = dx * dx + dy * dy;
      if (dx > 0 && dy > 0) {
        if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) gfx_draw_pixel(x + px, y + py, color);
      } else if (px == 0 || px == w - 1 || py == 0 || py == h - 1) {
        if (!(dx > 0 && dy > 0)) gfx_draw_pixel(x + px, y + py, color);
      }
    }
  }
}

static uint32_t parse_hex_color(const char *s);

static void parse_inline_style_attrs(const char *sty, uint32_t *col, bool *has_col,
                                     uint32_t *bg, bool *has_bg,
                                     uint32_t *bdr_col, bool *has_bdr,
                                     uint8_t *rad, uint8_t *fs,
                                     bool *no_ul, bool *bold) {
  if (!sty || !sty[0]) return;
  const char *p = sty;
  while (*p) {
    while (*p == ' ' || *p == ';') p++;
    if (!*p) break;
    if (strncmp(p, "color:", 6) == 0) {
      *col = parse_hex_color(p + 6);
      if (has_col) *has_col = true;
    } else if (strncmp(p, "background-color:", 17) == 0) {
      *bg = parse_hex_color(p + 17);
      *has_bg = true;
    } else if (strncmp(p, "text-decoration:none", 20) == 0) {
      *no_ul = true;
    } else if (strncmp(p, "font-weight:bold", 16) == 0) {
      *bold = true;
    } else if (strncmp(p, "font-size:", 10) == 0) {
      int v = 0; const char *q = p + 10;
      while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; }
      if (v >= 10 && v <= 32) *fs = (uint8_t)v;
    } else if (strncmp(p, "border-radius:", 14) == 0) {
      const char *q = p + 14;
      if (strncmp(q, "50%", 3) == 0) *rad = 50;
      else {
        int v = 0;
        while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; }
        if (v > 0 && v < 50) *rad = (uint8_t)v;
      }
    } else if (strncmp(p, "border:", 7) == 0) {
      const char *hx = strchr(p + 7, '#');
      const char *sc = strchr(p + 7, ';');
      if (hx && (!sc || hx < sc)) {
        *bdr_col = parse_hex_color(hx);
        *has_bdr = true;
      }
    }
    while (*p && *p != ';') p++;
  }
}

static int32_t browser_draw_ttf_string(int32_t x, int32_t y, const char *s, int px_h,
                                       uint32_t fg, int32_t max_x, bool underline) {
  if (!s) return x;
  int32_t cur_x = x;
  int32_t baseline = y + (px_h * 4) / 5;
  uint8_t fg_r = (uint8_t)((fg >> 16) & 0xFF);
  uint8_t fg_g = (uint8_t)((fg >> 8) & 0xFF);
  uint8_t fg_b = (uint8_t)(fg & 0xFF);
  static uint8_t alpha_buf[48 * 48];

  while (*s) {
    const uint8_t *us = (const uint8_t *)s;
    uint32_t cp = 0;
    int bytes = 1;
    if (us[0] < 0x80) { cp = us[0]; }
    else if ((us[0] & 0xE0) == 0xC0 && us[1]) { cp = ((us[0] & 0x1F) << 6) | (us[1] & 0x3F); bytes = 2; }
    else if ((us[0] & 0xF0) == 0xE0 && us[1] && us[2]) { cp = ((us[0] & 0x0F) << 12) | ((us[1] & 0x3F) << 6) | (us[2] & 0x3F); bytes = 3; }
    else if ((us[0] & 0xF8) == 0xF0 && us[1] && us[2] && us[3]) { cp = ((us[0] & 0x07) << 18) | ((us[1] & 0x3F) << 12) | ((us[2] & 0x3F) << 6) | (us[3] & 0x3F); bytes = 4; }
    else { cp = us[0]; }

    int gw = 0, gh = 0, xoff = 0, yoff = 0, adv = 0;
    if (fontmgr_get_ttf_glyph(cp, px_h, alpha_buf, sizeof(alpha_buf), &gw, &gh, &xoff, &yoff, &adv)) {
      if (cur_x + adv > max_x) break;
      for (int py = 0; py < gh; py++) {
        int32_t dy = baseline + yoff + py;
        for (int px = 0; px < gw; px++) {
          uint8_t a = alpha_buf[py * gw + px];
          if (a < 8) continue;
          int32_t dx = cur_x + xoff + px;
          if (a >= 248) {
            gfx_draw_pixel(dx, dy, fg);
          } else {
            uint32_t bg = gfx_get_pixel(dx, dy);
            uint32_t bg_r = (bg >> 16) & 0xFF;
            uint32_t bg_g = (bg >> 8) & 0xFF;
            uint32_t bg_b = bg & 0xFF;
            uint32_t r = (fg_r * a + bg_r * (255u - a)) / 255u;
            uint32_t g = (fg_g * a + bg_g * (255u - a)) / 255u;
            uint32_t b = (fg_b * a + bg_b * (255u - a)) / 255u;
            gfx_draw_pixel(dx, dy, (r << 16) | (g << 8) | b);
          }
        }
      }
      if (underline) {
        for (int lx = 0; lx < adv; lx++) gfx_draw_pixel(cur_x + lx, baseline + 2, fg);
      }
      cur_x += adv;
    } else {
      // Fallback al font bitmap UNICODE.FNT per script non coperti dal TTF
      char chbuf[5];
      for (int k = 0; k < bytes; k++) chbuf[k] = s[k];
      chbuf[bytes] = '\0';
      if (cur_x + 8 > max_x) break;
      gfx_draw_string(cur_x, y, chbuf, fg, 0, true);
      if (underline) {
        for (int lx = 0; lx < 8; lx++) gfx_draw_pixel(cur_x + lx, y + 14, fg);
      }
      cur_x += 8;
    }
    s += bytes;
  }
  return cur_x;
}

static void browser_fetch_external_css(browser_state_t *state) {
  if (!state || !state->raw_html || state->raw_html_len == 0 ||
      state->raw_html_len > 200000)
    return;
  if (strncmp(state->current_url, "http://", 7) != 0 &&
      strncmp(state->current_url, "https://", 8) != 0)
    return;

  const char *p = state->raw_html;
  int fetched = 0;
  while (*p && fetched < 2) {
    const char *lk = strstr(p, "<link");
    if (!lk) lk = strstr(p, "<LINK");
    if (!lk) break;
    const char *gt = strchr(lk, '>');
    if (!gt) break;

    char rel[32] = "", href[256] = "";
    const char *q = lk + 5;
    while (q < gt) {
      if ((q[0] == 'r' || q[0] == 'R') && (q[1] == 'e' || q[1] == 'E') &&
          (q[2] == 'l' || q[2] == 'L') && q[3] == '=') {
        q += 4;
        char qc = (*q == '"' || *q == '\'') ? *q++ : 0;
        size_t i = 0;
        while (q < gt && (qc ? (*q != qc) : (*q != ' ' && *q != '>')) && i + 1 < sizeof(rel)) {
          rel[i++] = *q++;
        }
        rel[i] = '\0';
      } else if ((q[0] == 'h' || q[0] == 'H') && (q[1] == 'r' || q[1] == 'R') &&
                 (q[2] == 'e' || q[2] == 'E') && (q[3] == 'f' || q[3] == 'F') && q[4] == '=') {
        q += 5;
        char qc = (*q == '"' || *q == '\'') ? *q++ : 0;
        size_t i = 0;
        while (q < gt && (qc ? (*q != qc) : (*q != ' ' && *q != '>')) && i + 1 < sizeof(href)) {
          href[i++] = *q++;
        }
        href[i] = '\0';
      } else {
        q++;
      }
    }

    if (strstr(rel, "stylesheet") && href[0]) {
      char abs_css[256];
      resolve_img_url(state->current_url, href, abs_css, sizeof(abs_css));
      if (abs_css[0]) {
        nettls_response_t cresp;
        memset(&cresp, 0, sizeof(cresp));
        if (nettls_http_get(abs_css, 49152, &cresp) && cresp.body && cresp.body_len > 0) {
          uint32_t add_len = cresp.body_len;
          if (state->raw_html_len + add_len + 20 < 260000) {
            memcpy(state->raw_html + state->raw_html_len, "<style>", 7);
            state->raw_html_len += 7;
            memcpy(state->raw_html + state->raw_html_len, cresp.body, add_len);
            state->raw_html_len += add_len;
            memcpy(state->raw_html + state->raw_html_len, "</style>", 8);
            state->raw_html_len += 8;
            state->raw_html[state->raw_html_len] = '\0';
          }
        }
        nettls_free_response(&cresp);
        fetched++;
      }
    }
    p = gt + 1;
  }
}

// Built-in Home Page HTML (Fallback compatto)
static const char *g_builtin_home_html =
    "<html><title>Home</title><body><h1>NewSoft Web Browser</h1>"
    "<p><a href=\"http://youtube.it\">YouTube</a> | "
    "<a href=\"about:network\">Rete</a> | "
    "<a href=\"about:browser\">Info</a></p></body></html>";

static const char *g_builtin_about_html =
    "<html><title>Info</title><body><h1>NewSoft Web Browser</h1>"
    "<p>NetSurf (C89/C99)</p>"
    "<p><a href=\"about:home\">Home</a></p></body></html>";

// Funzioni di utilità per il parsing HTML
static int browser_strncasecmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c1 = s1[i];
    char c2 = s2[i];
    if (c1 >= 'a' && c1 <= 'z')
      c1 -= 32;
    if (c2 >= 'a' && c2 <= 'z')
      c2 -= 32;
    if (c1 != c2 || c1 == '\0')
      return (unsigned char)c1 - (unsigned char)c2;
  }
  return 0;
}
static void str_strip_tags(char *s) {
  if (!s)
    return;
  char *src = s;
  char *dst = s;
  bool in_tag = false;
  while (*src) {
    if (*src == '<')
      in_tag = true;
    else if (*src == '>')
      in_tag = false;
    else if (!in_tag)
      *dst++ = *src;
    src++;
  }
  *dst = '\0';
}

static void extract_attribute(const char *tag_start, const char *attr_name,
                              char *out, size_t max_len) {
  out[0] = '\0';
  if (!tag_start || !attr_name || max_len == 0)
    return;

  size_t alen = strlen(attr_name);
  const char *p = tag_start;
  char in_q = 0;

  while (*p && (in_q || *p != '>')) {
    if (*p == '"' || *p == '\'') {
      if (in_q == *p) in_q = 0;
      else if (!in_q) in_q = *p;
      p++;
      continue;
    }
    if (!in_q && (p > tag_start) &&
        (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') &&
        browser_strncasecmp(p, attr_name, alen) == 0) {
      const char *eq = p + alen;
      while (*eq == ' ' || *eq == '\t') eq++;
      if (*eq == '=') {
        eq++;
        while (*eq == ' ' || *eq == '\t') eq++;
        char quote = 0;
        if (*eq == '"' || *eq == '\'') {
          quote = *eq;
          eq++;
        }
        size_t i = 0;
        while (*eq && i < max_len - 1) {
          if (quote && *eq == quote) break;
          if (!quote && (*eq == ' ' || *eq == '\t' || *eq == '>')) break;
          out[i++] = *eq++;
        }
        out[i] = '\0';
        return;
      }
    }
    p++;
  }
}

static uint32_t parse_hex_color(const char *s) {
  if (!s || !*s)
    return 0;
  if (s[0] == '#') {
    uint32_t val = 0;
    for (int i = 1; i <= 6 && s[i]; i++) {
      char c = s[i];
      uint32_t nibble = 0;
      if (c >= '0' && c <= '9')
        nibble = c - '0';
      else if (c >= 'a' && c <= 'f')
        nibble = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        nibble = c - 'A' + 10;
      else
        break;
      val = (val << 4) | nibble;
    }
    return val;
  }
  if (browser_strncasecmp(s, "red", 3) == 0)
    return 0xCC181E;
  if (browser_strncasecmp(s, "blue", 4) == 0)
    return 0x0000CC;
  if (browser_strncasecmp(s, "green", 5) == 0)
    return 0x008000;
  if (browser_strncasecmp(s, "gray", 4) == 0)
    return 0x606060;
  if (browser_strncasecmp(s, "black", 5) == 0)
    return 0x000000;
  return 0;
}

// Genera una pagina dinamica con lo stato della rete (about:network)
static void generate_network_page(browser_state_t *state) {
  net_nic_t *nic = net_nic_get_active();
  char ip_s[20] = "0.0.0.0", mask_s[20] = "0.0.0.0", gw_s[20] = "0.0.0.0",
       dns_s[20] = "0.0.0.0", mac_s[24] = "00:00:00:00:00:00";

  if (nic && (nic->link_up || nic->ip != 0)) {
    net_format_ip(nic->ip, ip_s, sizeof(ip_s));
    net_format_ip(nic->netmask, mask_s, sizeof(mask_s));
    net_format_ip(nic->gateway, gw_s, sizeof(gw_s));
    net_format_ip(nic->dns, dns_s, sizeof(dns_s));
    net_format_mac(nic->mac, mac_s, sizeof(mac_s));
  }

  mini_snprintf(
      state->raw_html, MAX_HTML_SIZE,
      "<html><head><title>Rete</title></head><body>"
      "<h1>Diagnostica Rete</h1><hr><ul>"
      "<li><b>NIC:</b> %s</li><li><b>MAC:</b> %s</li>"
      "<li><b>IP:</b> %s</li><li><b>Subnet:</b> %s</li>"
      "<li><b>GW:</b> %s</li><li><b>DNS:</b> %s</li>"
      "</ul><hr><p><a href=\"about:home\">&lt;&lt; Home</a></p></body></html>",
      (nic && nic->name[0]) ? nic->name : "E1000", mac_s, ip_s, mask_s, gw_s,
      dns_s);

  state->raw_html_len = (uint32_t)strlen(state->raw_html);
}

// Genera una pagina con il riepilogo delle regole di sistema (about:rules)
static void generate_rules_page(browser_state_t *state) {
  mini_snprintf(
      state->raw_html, MAX_HTML_SIZE,
      "<html><head><title>Regole</title></head><body>"
      "<h1>Regole NewOS V1</h1><hr><ul>"
      "<li>01: File System C:\\NewOS e C:\\ProgramFile</li>"
      "<li>02: Registro AES-256</li>"
      "<li>03: Policy Installatori</li>"
      "<li>04: Privilegi Utente</li>"
      "<li>05: Rete e Priorita' 95.141.32.1</li>"
      "</ul><hr><p><a href=\"about:home\">&lt;&lt; Home</a></p></body></html>");
  state->raw_html_len = (uint32_t)strlen(state->raw_html);
}

// Genera una pagina di errore HTTP o di connessione
static void generate_error_page(browser_state_t *state, const char *title,
                                const char *msg, const char *target) {
  mini_snprintf(state->raw_html, MAX_HTML_SIZE,
                "<html><title>%s</title><body><h1>%s</h1><p>%s</p><p>%s</p>"
                "<p><a href=\"about:home\">Home</a></p></body></html>",
                title, title, msg, target ? target : "");
  state->raw_html_len = (uint32_t)strlen(state->raw_html);
}

// Helper per confrontare il nome del tag HTML seguito da delimitatore
static bool is_tag_name(const char *tag_start, const char *name) {
  size_t n = strlen(name);
  if (browser_strncasecmp(tag_start, name, n) != 0)
    return false;
  char c = tag_start[n];
  return (c == '\0' || c == '>' || c == ' ' || c == '\t' || c == '\r' ||
          c == '\n' || c == '/');
}

// Helper per saltare blocchi invisibili (<script>, <style>, <svg>, ecc.)
static const char *skip_until_close_tag(const char *ptr,
                                        const char *close_tag) {
  size_t clen = strlen(close_tag);
  while (*ptr) {
    if (*ptr == '<' && browser_strncasecmp(ptr + 1, close_tag, clen) == 0) {
      char after = ptr[1 + clen];
      if (after == '>' || after == ' ' || after == '\t' || after == '\r' ||
          after == '\n') {
        while (*ptr && *ptr != '>')
          ptr++;
        if (*ptr == '>')
          ptr++;
        return ptr;
      }
    }
    ptr++;
  }
  return ptr;
}

// Motore di parsing e formattazione HTML + CSSENGINE.DLL
static void browser_parse_html(browser_state_t *state, int32_t viewport_w) {
  state->num_elems = 0;
  state->content_height = 0;
  state->scroll_y = 0;
  state->hover_elem = -1;
  state->page_bg_color = 0xFFFFFF;
  state->has_page_bg = false;

  // Titolo predefinito
  strcpy(state->page_title, "NewSoft Web Browser");

  const char *p = state->raw_html;
  if (!p || *p == '\0')
    return;

  void *css_sheet = css_parse_from_html(p);

  // 1. Estrazione del tag <title>...</title> (con trim spazi/newline)
  const char *t_start = strstr(p, "<title");
  if (!t_start)
    t_start = strstr(p, "<TITLE");
  if (t_start) {
    while (*t_start && *t_start != '>')
      t_start++;
    if (*t_start == '>')
      t_start++;
    const char *t_end = strstr(t_start, "</title>");
    if (!t_end)
      t_end = strstr(t_start, "</TITLE>");
    if (t_end) {
      while (t_start < t_end && (*t_start == ' ' || *t_start == '\r' ||
                                 *t_start == '\n' || *t_start == '\t'))
        t_start++;
      while (t_end > t_start && (t_end[-1] == ' ' || t_end[-1] == '\r' ||
                                 t_end[-1] == '\n' || t_end[-1] == '\t'))
        t_end--;
      size_t t_len = (size_t)(t_end - t_start);
      if (t_len >= sizeof(state->page_title))
        t_len = sizeof(state->page_title) - 1;
      if (t_len > 0) {
        strncpy(state->page_title, t_start, t_len);
        state->page_title[t_len] = '\0';
      }
    }
  }

  // 2. Tokenizzazione del body
  const char *body = strstr(p, "<body");
  if (!body)
    body = strstr(p, "<BODY");
  if (body) {
    while (*body && *body != '>')
      body++;
    if (*body == '>')
      body++;
  } else {
    body = p;
  }

  int32_t cur_y = 14;
  int32_t content_left_x = 12;
  int32_t max_text_chars = (viewport_w - 40) / 8;
  if (max_text_chars < 20)
    max_text_chars = 20;

  // Stato griglia 2D e layout moderno (flex, sidebar, topbar, chips, card)
  bool in_inline_row = false;
  bool in_topbar = false;
  bool in_chips = false;
  bool in_sidebar = false;
  bool in_meta = false;
  int meta_depth = 0;
  int32_t sidebar_top_y = 14;
  int32_t inline_x = 12;
  bool in_grid = false;
  bool in_card = false;
  int card_div_depth = 0;
  int grid_cols = (viewport_w >= 700) ? 3 : ((viewport_w >= 460) ? 2 : 1);
  int32_t card_w = (viewport_w - 36 - (grid_cols - 1) * 14) / grid_cols;
  if (card_w < 160) card_w = 160;
  int card_col = 0;
  int32_t card_x = 12;
  int32_t grid_row_y = 14;
  int32_t grid_next_row_y = 14;

  const char *ptr = body;
  char text_buf[512];
  int text_pos = 0;

  elem_type_t cur_type = ELEM_TEXT;
  char cur_href[256] = "";
  uint32_t cur_color = 0;
  uint32_t base_text_color = 0;
  uint32_t cur_bg_color = 0;
  uint32_t cur_bdr_color = 0;
  bool cur_has_bg = false;
  bool cur_has_bdr = false;
  bool cur_no_ul = false;
  bool cur_bold = false;
  bool cur_shadow = false;
  uint8_t cur_rad = 0;
  uint8_t cur_fs = 0;
  uint8_t cur_align = CSS_ALIGN_LEFT;
  uint8_t cur_deco = CSS_TEXT_DECO_DEFAULT;
  uint8_t cur_transform = CSS_TEXT_TRANSFORM_NONE;

  if (body != p) {
    // Applica stile CSS su <body> (background-color e color)
    const char *btag = strstr(p, "<body");
    if (!btag) btag = strstr(p, "<BODY");
    if (btag) {
      char bid[48] = "", bcls[64] = "", bsty[128] = "";
      extract_attribute(btag + 1, "id", bid, sizeof(bid));
      extract_attribute(btag + 1, "class", bcls, sizeof(bcls));
      extract_attribute(btag + 1, "style", bsty, sizeof(bsty));
      css_style_t bst;
      css_resolve_style(css_sheet, "body", bid, bcls, bsty, &bst);
      if (bst.has_bg) {
        state->page_bg_color = bst.bg_color;
        state->has_page_bg = true;
      }
      if (bst.has_color) {
        base_text_color = bst.color;
        cur_color = bst.color;
      }
    }
  }

  while (*ptr && state->num_elems < MAX_ELEMENTS - 4) {
    if (*ptr == '<') {
      // Commento HTML <!-- ... --> gestito PRIMA della ricerca '>'
      if (strncmp(ptr + 1, "!--", 3) == 0) {
        const char *c_end = strstr(ptr + 4, "-->");
        ptr = c_end ? (c_end + 3) : (ptr + strlen(ptr));
        continue;
      }

      // Flush del testo accumulato prima del tag
      if (text_pos > 0) {
        text_buf[text_pos] = '\0';
        int start_i = 0;
        int wrap_chars = in_card ? ((in_meta ? (card_w - 32) : card_w) / 7) : ((viewport_w - content_left_x - 28) / 7);
        if (wrap_chars < 14) wrap_chars = 14;
        int card_lines = 0;
        while (start_i < text_pos && state->num_elems < MAX_ELEMENTS - 2) {
          if (in_card && in_meta && cur_rad != 50 && card_lines >= 2) break;
          int len = text_pos - start_i;
          if (len > wrap_chars) {
            int split_at = start_i + wrap_chars;
            while (split_at > start_i && text_buf[split_at] != ' ')
              split_at--;
            if (split_at == start_i) {
              split_at = start_i + wrap_chars;
              while (split_at > start_i && (((uint8_t)text_buf[split_at] & 0xC0) == 0x80))
                split_at--;
              if (split_at == start_i) split_at = start_i + wrap_chars;
            }
            len = split_at - start_i;
          }

          browser_elem_t *el = &state->elems[state->num_elems++];
          memset(el, 0, sizeof(*el));
          el->img_idx = -1;
          el->type = cur_type;
          if (len >= (int)sizeof(el->text))
            len = (int)sizeof(el->text) - 1;
          strncpy(el->text, text_buf + start_i, len);
          el->text[len] = '\0';
          if (cur_transform == CSS_TEXT_TRANSFORM_UPPERCASE) {
            for (int ti = 0; el->text[ti]; ti++) {
              if (el->text[ti] >= 'a' && el->text[ti] <= 'z') el->text[ti] -= 32;
            }
          } else if (cur_transform == CSS_TEXT_TRANSFORM_LOWERCASE) {
            for (int ti = 0; el->text[ti]; ti++) {
              if (el->text[ti] >= 'A' && el->text[ti] <= 'Z') el->text[ti] += 32;
            }
          } else if (cur_transform == CSS_TEXT_TRANSFORM_CAPITALIZE) {
            bool nw = true;
            for (int ti = 0; el->text[ti]; ti++) {
              if (el->text[ti] == ' ') nw = true;
              else if (nw && el->text[ti] >= 'a' && el->text[ti] <= 'z') {
                el->text[ti] -= 32;
                nw = false;
              } else nw = false;
            }
          }
          strncpy(el->href, cur_href, sizeof(el->href) - 1);
          el->href[sizeof(el->href) - 1] = '\0';
          el->color = cur_color;
          el->bg_color = cur_bg_color;
          el->border_color = cur_bdr_color;
          el->has_bg = cur_has_bg;
          el->has_border = cur_has_bdr;
          el->has_shadow = cur_shadow;
          el->no_underline = cur_no_ul;
          el->is_bold = cur_bold;
          el->border_radius = cur_rad;
          el->font_size = cur_fs;
          el->text_align = cur_align;
          el->text_deco = cur_deco;
          bool is_pill = (el->has_bg || el->has_border || el->border_radius > 0);
          el->w = (el->border_radius == 50) ? 24 : (is_pill ? (len * 7 + 14) : (len * 7));
          el->h = (cur_type == ELEM_H1) ? 22 : ((cur_type == ELEM_H2) ? 18 : (is_pill ? 24 : 17));
          if (in_inline_row && inline_x + el->w > viewport_w - 28 && inline_x > content_left_x + 4) {
            inline_x = content_left_x;
            cur_y += 28;
          }
          if (in_sidebar) {
            el->x = 10;
            el->y = cur_y;
            el->w = 116;
            el->h = 24;
            cur_y += 26;
          } else if (in_meta && cur_rad == 50) {
            el->x = card_x;
            el->y = cur_y;
          } else if (in_meta) {
            el->x = card_x + 30;
            el->y = cur_y;
            cur_y += el->h + 1;
            card_lines++;
          } else if (in_inline_row) {
            el->x = inline_x;
            el->y = cur_y;
            inline_x += el->w + ((in_topbar || in_chips) ? 8 : 6);
          } else if (in_card) {
            el->x = card_x + (cur_fs == 12 ? 30 : 0);
            el->y = cur_y;
            cur_y += el->h + 2;
          } else {
            el->x = content_left_x + ((cur_type == ELEM_LIST_ITEM) ? 14 : 0);
            if (cur_align == CSS_ALIGN_CENTER && el->w < (viewport_w - content_left_x - 24)) {
              el->x = content_left_x + ((viewport_w - content_left_x - 24) - el->w) / 2;
            } else if (cur_align == CSS_ALIGN_RIGHT && el->w < (viewport_w - content_left_x - 24)) {
              el->x = viewport_w - 24 - el->w;
            }
            el->y = cur_y;
            cur_y += el->h + 2;
          }
          start_i += len;
          while (start_i < text_pos && text_buf[start_i] == ' ')
            start_i++;
        }
        text_pos = 0;
      }

      // Scansione tag quote-aware
      const char *tag_start = ptr + 1;
      char in_q = 0;
      ptr++;
      while (*ptr && (in_q || *ptr != '>')) {
        if ((*ptr == '"' || *ptr == '\'')) {
          if (in_q == *ptr)
            in_q = 0;
          else if (!in_q)
            in_q = *ptr;
        }
        ptr++;
      }
      if (*ptr == '>')
        ptr++;

      if (is_tag_name(tag_start, "script")) {
        ptr = skip_until_close_tag(ptr, "/script");
        continue;
      } else if (is_tag_name(tag_start, "style")) {
        ptr = skip_until_close_tag(ptr, "/style");
        continue;
      } else if (is_tag_name(tag_start, "svg")) {
        ptr = skip_until_close_tag(ptr, "/svg");
        continue;
      } else if (is_tag_name(tag_start, "noscript")) {
        ptr = skip_until_close_tag(ptr, "/noscript");
        continue;
      } else if (is_tag_name(tag_start, "select")) {
        ptr = skip_until_close_tag(ptr, "/select");
        continue;
      } else if (is_tag_name(tag_start, "template")) {
        ptr = skip_until_close_tag(ptr, "/template");
        continue;
      } else if (is_tag_name(tag_start, "head")) {
        ptr = skip_until_close_tag(ptr, "/head");
        continue;
      }

      if (is_tag_name(tag_start, "h1")) {
        cur_type = ELEM_H1;
        cur_y += 6;
      } else if (is_tag_name(tag_start, "/h1")) {
        cur_type = ELEM_TEXT;
        cur_y += 6;
      } else if (is_tag_name(tag_start, "h2")) {
        cur_type = ELEM_H2;
        cur_y += 4;
      } else if (is_tag_name(tag_start, "/h2")) {
        cur_type = ELEM_TEXT;
        cur_y += 4;
      } else if (is_tag_name(tag_start, "h3") || is_tag_name(tag_start, "h4")) {
        cur_type = ELEM_H3;
        cur_y += 2;
      } else if (is_tag_name(tag_start, "/h3") ||
                 is_tag_name(tag_start, "/h4")) {
        cur_type = ELEM_TEXT;
        cur_y += 2;
      } else if (is_tag_name(tag_start, "hr")) {
        browser_elem_t *el = &state->elems[state->num_elems++];
        memset(el, 0, sizeof(*el));
        el->img_idx = -1;
        el->type = ELEM_HR;
        el->x = in_sidebar ? 10 : content_left_x;
        el->y = cur_y;
        el->w = in_sidebar ? 114 : (viewport_w - content_left_x - 24);
        el->h = 6;
        cur_y += 10;
      } else if (is_tag_name(tag_start, "br")) {
        cur_y += 14;
      } else if (is_tag_name(tag_start, "div")) {
        char dcls[64] = "", dsty[256] = "";
        extract_attribute(tag_start, "class", dcls, sizeof(dcls));
        extract_attribute(tag_start, "style", dsty, sizeof(dsty));
        if (strstr(dcls, "yt-topbar")) {
          in_topbar = true;
          in_inline_row = true;
          inline_x = 12;
        } else if (strstr(dcls, "yt-hdr")) {
          in_inline_row = true;
          inline_x = content_left_x;
        } else if (strstr(dcls, "yt-sidebar")) {
          in_sidebar = true;
          sidebar_top_y = cur_y;
        } else if (strstr(dcls, "yt-main")) {
          content_left_x = 138;
          card_w = (viewport_w - content_left_x - 24 - (grid_cols - 1) * 14) / grid_cols;
          if (card_w < 140) card_w = 140;
        } else if (strstr(dcls, "yt-chips")) {
          in_chips = true;
          in_inline_row = true;
          inline_x = content_left_x;
        } else if (strstr(dcls, "yt-layout")) {
          /* wrapper layout 2 colonne */
        } else if (strstr(dcls, "yt-grid")) {
          in_grid = true;
          in_card = false;
          in_meta = false;
          meta_depth = 0;
          in_inline_row = false;
          card_col = 0;
          cur_y += 8;
          grid_row_y = cur_y;
          grid_next_row_y = cur_y;
        } else if (in_grid && strstr(dcls, "yt-card")) {
          if (in_card) {
            if (cur_y + 12 > grid_next_row_y)
              grid_next_row_y = cur_y + 12;
            card_col++;
            if (card_col >= grid_cols) {
              card_col = 0;
              grid_row_y = grid_next_row_y;
            }
          }
          in_card = true;
          in_meta = false;
          meta_depth = 0;
          card_div_depth = 1;
          card_x = content_left_x + card_col * (card_w + 14);
          cur_y = grid_row_y;
        } else if (in_card) {
          card_div_depth++;
          if (strstr(dcls, "yt-meta")) {
            in_meta = true;
            meta_depth = 1;
          } else if (in_meta) {
            meta_depth++;
          }
          if (dsty[0]) {
            parse_inline_style_attrs(dsty, &cur_color, NULL, &cur_bg_color, &cur_has_bg,
                                     &cur_bdr_color, &cur_has_bdr, &cur_rad, &cur_fs,
                                     &cur_no_ul, &cur_bold);
          }
        } else if (dsty[0]) {
          parse_inline_style_attrs(dsty, &cur_color, NULL, &cur_bg_color, &cur_has_bg,
                                   &cur_bdr_color, &cur_has_bdr, &cur_rad, &cur_fs,
                                   &cur_no_ul, &cur_bold);
          if (!in_sidebar && strstr(dsty, "display:flex")) {
            in_inline_row = true;
            inline_x = content_left_x;
          }
        } else {
          cur_y += 4;
        }
        if (cur_type != ELEM_LINK)
          cur_type = ELEM_TEXT;
      } else if (is_tag_name(tag_start, "/div")) {
        if (in_card) {
          card_div_depth--;
          if (in_meta) {
            meta_depth--;
            if (meta_depth <= 0) {
              in_meta = false;
              meta_depth = 0;
              cur_y += 2;
            }
          }
          if (card_div_depth <= 0) {
            if (cur_y + 12 > grid_next_row_y)
              grid_next_row_y = cur_y + 12;
            in_card = false;
            in_meta = false;
            meta_depth = 0;
            card_div_depth = 0;
            card_col++;
            if (card_col >= grid_cols) {
              card_col = 0;
              grid_row_y = grid_next_row_y;
            }
            cur_y = grid_next_row_y;
          }
        } else if (in_chips) {
          in_chips = false;
          in_inline_row = false;
          cur_y += 30;
        } else if (in_topbar) {
          in_topbar = false;
          in_inline_row = false;
          cur_y += 34;
          if (state->num_elems < MAX_ELEMENTS) {
            browser_elem_t *hr = &state->elems[state->num_elems++];
            memset(hr, 0, sizeof(*hr));
            hr->img_idx = -1;
            hr->type = ELEM_HR;
            hr->x = 8; hr->y = cur_y - 4; hr->w = viewport_w - 28; hr->h = 2;
          }
        } else if (in_sidebar) {
          in_sidebar = false;
          content_left_x = 138;
          cur_y = sidebar_top_y;
        } else if (in_inline_row) {
          in_inline_row = false;
          cur_y += 26;
        } else if (in_grid) {
          in_grid = false;
          cur_y = grid_next_row_y + 6;
        } else {
          cur_y += 4;
        }
        cur_color = base_text_color; cur_has_bg = false; cur_has_bdr = false;
        cur_shadow = false;
        cur_rad = 0; cur_fs = 0; cur_no_ul = false; cur_bold = false;
        cur_align = CSS_ALIGN_LEFT; cur_deco = CSS_TEXT_DECO_DEFAULT;
        cur_transform = CSS_TEXT_TRANSFORM_NONE;
        if (cur_type != ELEM_LINK)
          cur_type = ELEM_TEXT;
      } else if (is_tag_name(tag_start, "p") ||
                 is_tag_name(tag_start, "tr")) {
        cur_y += 4;
        if (cur_type != ELEM_LINK)
          cur_type = ELEM_TEXT;
      } else if (is_tag_name(tag_start, "/p") ||
                 is_tag_name(tag_start, "/tr")) {
        cur_y += 4;
        if (cur_type != ELEM_LINK)
          cur_type = ELEM_TEXT;
      } else if (is_tag_name(tag_start, "td") || is_tag_name(tag_start, "th")) {
        if (text_pos > 0 && text_pos < (int)sizeof(text_buf) - 2 &&
            text_buf[text_pos - 1] != ' ') {
          text_buf[text_pos++] = ' ';
        }
      } else if (is_tag_name(tag_start, "li")) {
        cur_type = ELEM_LIST_ITEM;
      } else if (is_tag_name(tag_start, "/li")) {
        cur_type = ELEM_TEXT;
      } else if (is_tag_name(tag_start, "a")) {
        cur_type = ELEM_LINK;
        extract_attribute(tag_start, "href", cur_href, sizeof(cur_href));
        char asty[256] = "";
        extract_attribute(tag_start, "style", asty, sizeof(asty));
        parse_inline_style_attrs(asty, &cur_color, NULL, &cur_bg_color, &cur_has_bg,
                                 &cur_bdr_color, &cur_has_bdr, &cur_rad, &cur_fs,
                                 &cur_no_ul, &cur_bold);
      } else if (is_tag_name(tag_start, "/a")) {
        cur_type = ELEM_TEXT;
        cur_href[0] = '\0';
        cur_color = base_text_color; cur_has_bg = false; cur_has_bdr = false;
        cur_shadow = false;
        cur_rad = 0; cur_fs = 0; cur_no_ul = false; cur_bold = false;
        cur_align = CSS_ALIGN_LEFT; cur_deco = CSS_TEXT_DECO_DEFAULT;
        cur_transform = CSS_TEXT_TRANSFORM_NONE;
      } else if (is_tag_name(tag_start, "span")) {
        char ssty[256] = "";
        extract_attribute(tag_start, "style", ssty, sizeof(ssty));
        parse_inline_style_attrs(ssty, &cur_color, NULL, &cur_bg_color, &cur_has_bg,
                                 &cur_bdr_color, &cur_has_bdr, &cur_rad, &cur_fs,
                                 &cur_no_ul, &cur_bold);
      } else if (is_tag_name(tag_start, "/span")) {
        cur_color = base_text_color; cur_has_bg = false; cur_has_bdr = false;
        cur_shadow = false;
        cur_rad = 0; cur_fs = 0; cur_no_ul = false; cur_bold = false;
        cur_align = CSS_ALIGN_LEFT; cur_deco = CSS_TEXT_DECO_DEFAULT;
        cur_transform = CSS_TEXT_TRANSFORM_NONE;
      } else if (is_tag_name(tag_start, "pre")) {
        cur_type = ELEM_PRE;
        cur_y += 4;
      } else if (is_tag_name(tag_start, "/pre")) {
        cur_type = ELEM_TEXT;
        cur_y += 4;
      } else if (is_tag_name(tag_start, "font")) {
        char cbuf[32] = "";
        extract_attribute(tag_start, "color", cbuf, sizeof(cbuf));
        if (cbuf[0])
          cur_color = parse_hex_color(cbuf);
      } else if (is_tag_name(tag_start, "/font")) {
        cur_color = 0;
      } else if (is_tag_name(tag_start, "small")) {
        cur_color = 0x606060;
      } else if (is_tag_name(tag_start, "/small")) {
        cur_color = base_text_color;
      } else if (is_tag_name(tag_start, "img")) {
        char src[256] = "", alt[96] = "", wbuf[16] = "", hbuf[16] = "", dur[16] = "";
        extract_attribute(tag_start, "src", src, sizeof(src));
        extract_attribute(tag_start, "alt", alt, sizeof(alt));
        extract_attribute(tag_start, "width", wbuf, sizeof(wbuf));
        extract_attribute(tag_start, "height", hbuf, sizeof(hbuf));
        extract_attribute(tag_start, "data-dur", dur, sizeof(dur));
        if (src[0] && state->num_elems < MAX_ELEMENTS) {
          char abs_src[256];
          resolve_img_url(state->current_url, src, abs_src, sizeof(abs_src));
          int iw = 0, ih = 0;
          for (int k = 0; wbuf[k] >= '0' && wbuf[k] <= '9'; k++) iw = iw * 10 + (wbuf[k] - '0');
          for (int k = 0; hbuf[k] >= '0' && hbuf[k] <= '9'; k++) ih = ih * 10 + (hbuf[k] - '0');
          if (iw <= 0) iw = in_card ? card_w : 180;
          if (ih <= 0) ih = (iw * 9) / 16;
          if (in_card && iw > card_w) {
            ih = (ih * card_w) / iw;
            iw = card_w;
          } else if (iw > viewport_w - 40) {
            ih = (ih * (viewport_w - 40)) / iw;
            iw = viewport_w - 40;
          }
          if (ih < 16) ih = 16;
          if (ih > 320) ih = 320;
          int idx = browser_alloc_image_slot(state, abs_src);
          if (idx >= 0) {
            browser_elem_t *el = &state->elems[state->num_elems++];
            memset(el, 0, sizeof(*el));
            el->type = ELEM_IMG;
            el->img_idx = (int16_t)idx;
            strncpy(el->href, cur_href, sizeof(el->href) - 1);
            strncpy(el->badge, dur, sizeof(el->badge) - 1);
            strncpy(el->text, alt, sizeof(el->text) - 1);
            el->x = in_card ? card_x : content_left_x;
            el->y = cur_y;
            el->w = iw;
            el->h = ih;
            cur_y += ih + 6;
          }
        } else if (alt[0] && state->num_elems < MAX_ELEMENTS) {
          browser_elem_t *el = &state->elems[state->num_elems++];
          memset(el, 0, sizeof(*el));
          el->img_idx = -1;
          el->type = ELEM_TEXT;
          mini_snprintf(el->text, sizeof(el->text), "[IMG: %s]", alt);
          el->color = 0x555555;
          el->x = in_card ? card_x : 12;
          el->y = cur_y;
          el->w = (int32_t)strlen(el->text) * 8;
          el->h = 16;
          cur_y += 18;
        }
      } else if (is_tag_name(tag_start, "input")) {
        char itype[24] = "", ival[80] = "", iph[80] = "";
        extract_attribute(tag_start, "type", itype, sizeof(itype));
        if (browser_strncasecmp(itype, "hidden", 6) != 0) {
          extract_attribute(tag_start, "value", ival, sizeof(ival));
          extract_attribute(tag_start, "placeholder", iph, sizeof(iph));
          const char *lbl = ival[0] ? ival : (iph[0] ? iph : itype);
          if (lbl[0] && state->num_elems < MAX_ELEMENTS) {
            browser_elem_t *el = &state->elems[state->num_elems++];
            memset(el, 0, sizeof(*el));
            el->img_idx = -1;
            bool is_sub = (browser_strncasecmp(itype, "submit", 6) == 0);
            el->type = is_sub ? ELEM_BUTTON : ELEM_TEXT;
            mini_snprintf(el->text, sizeof(el->text), is_sub ? "%s" : "[ %s ]", lbl);
            char isty[256] = "";
            extract_attribute(tag_start, "style", isty, sizeof(isty));
            parse_inline_style_attrs(isty, &el->color, NULL, &el->bg_color, &el->has_bg,
                                     &el->border_color, &el->has_border, &el->border_radius,
                                     &el->font_size, &el->no_underline, &el->is_bold);
            if (!el->color) el->color = is_sub ? 0x0F0F0F : 0x606060;
            el->w = (int32_t)strlen(el->text) * 8 + ((el->has_border || el->border_radius) ? 16 : 0);
            el->h = in_topbar ? 24 : 18;
            el->x = in_inline_row ? inline_x : content_left_x;
            el->y = cur_y;
            if (in_inline_row) {
              inline_x += el->w + (is_sub ? 22 : 8);
            } else {
              cur_y += 22;
            }
          }
        }
      } else if (is_tag_name(tag_start, "video")) {
        browser_elem_t *el = &state->elems[state->num_elems++];
        el->type = ELEM_VIDEO;
        el->text[0] = '\0';
        el->href[0] = '\0';
        el->color = 0;
        el->x = 16;
        el->y = cur_y;
        el->w = (viewport_w > 540) ? 500 : (viewport_w - 40);
        el->h = 150;
        cur_y += el->h + 10;
      } else if (is_tag_name(tag_start, "button")) {
        cur_type = ELEM_BUTTON;
      } else if (is_tag_name(tag_start, "/button")) {
        cur_type = ELEM_TEXT;
      } else if (is_tag_name(tag_start, "header") ||
                 is_tag_name(tag_start, "/header") ||
                 is_tag_name(tag_start, "nav") ||
                 is_tag_name(tag_start, "/nav") ||
                 is_tag_name(tag_start, "section") ||
                 is_tag_name(tag_start, "/section") ||
                 is_tag_name(tag_start, "article") ||
                 is_tag_name(tag_start, "/article") ||
                 is_tag_name(tag_start, "main") ||
                 is_tag_name(tag_start, "/main") ||
                 is_tag_name(tag_start, "footer") ||
                 is_tag_name(tag_start, "/footer")) {
        cur_y += 4;
      }

      // Risoluzione stile CSS sul tag aperto (color, display: none, margin_top)
      if (tag_start[0] != '/') {
        char tname[24] = "";
        size_t ti = 0;
        while (tag_start[ti] && tag_start[ti] != '>' &&
               tag_start[ti] != ' ' && tag_start[ti] != '\t' &&
               tag_start[ti] != '\r' && tag_start[ti] != '\n' &&
               tag_start[ti] != '/' && ti + 1 < sizeof(tname)) {
          tname[ti] = tag_start[ti];
          ti++;
        }
        tname[ti] = '\0';
        if (tname[0] && css_sheet) {
          char tid[48] = "", tcls[64] = "", tsty[128] = "";
          extract_attribute(tag_start, "id", tid, sizeof(tid));
          extract_attribute(tag_start, "class", tcls, sizeof(tcls));
          extract_attribute(tag_start, "style", tsty, sizeof(tsty));
          if (tid[0] || tcls[0] || tsty[0]) {
            css_style_t st;
            css_resolve_style(css_sheet, tname, tid, tcls, tsty, &st);
            if ((st.display == CSS_DISPLAY_NONE || st.opacity == 0) &&
                !is_tag_name(tag_start, "input") &&
                !is_tag_name(tag_start, "img") &&
                !is_tag_name(tag_start, "br") &&
                !is_tag_name(tag_start, "hr")) {
              char ctag[28];
              mini_snprintf(ctag, sizeof(ctag), "/%s", tname);
              ptr = skip_until_close_tag(ptr, ctag);
              continue;
            }
            if (st.has_color) cur_color = st.color;
            else if (is_tag_name(tag_start, "p") || is_tag_name(tag_start, "div"))
              cur_color = base_text_color;
            if (st.has_bg) { cur_bg_color = st.bg_color; cur_has_bg = true; }
            if (st.has_border) { cur_bdr_color = st.border_color; cur_has_bdr = true; }
            if (st.has_shadow) cur_shadow = true;
            if (st.border_radius > 0) cur_rad = (uint8_t)st.border_radius;
            if (st.font_size > 0) cur_fs = (uint8_t)st.font_size;
            if (st.bold) cur_bold = true;
            if (st.no_underline) cur_no_ul = true;
            if (st.text_align != CSS_ALIGN_LEFT) cur_align = st.text_align;
            if (st.text_deco != CSS_TEXT_DECO_DEFAULT) cur_deco = st.text_deco;
            if (st.text_transform != CSS_TEXT_TRANSFORM_NONE) cur_transform = st.text_transform;
            if (((st.display == CSS_DISPLAY_FLEX && st.flex_direction != CSS_FLEX_DIR_COL) ||
                 st.display == CSS_DISPLAY_INLINE_FLEX ||
                 st.display == CSS_DISPLAY_GRID || st.display == CSS_DISPLAY_INLINE_GRID) &&
                !in_grid && !in_card && !in_inline_row && !in_topbar && !in_sidebar && !in_chips) {
              in_inline_row = true;
              inline_x = content_left_x;
            }
            if (st.margin_top > 0) cur_y += st.margin_top;
          }
        }
      } else if (is_tag_name(tag_start, "/span") ||
                 is_tag_name(tag_start, "/div") ||
                 is_tag_name(tag_start, "/p") ||
                 is_tag_name(tag_start, "/a") ||
                 is_tag_name(tag_start, "/h1") ||
                 is_tag_name(tag_start, "/h2") ||
                 is_tag_name(tag_start, "/h3")) {
        cur_color = base_text_color;
        cur_has_bg = false;
      }
      continue;
    }

    // Gestione entita' HTML complete (testuali + numeriche)
    if (*ptr == '&') {
      if (browser_strncasecmp(ptr, "&nbsp;", 6) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = ' ';
        ptr += 6;
        continue;
      } else if (browser_strncasecmp(ptr, "&lt;", 4) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = '<';
        ptr += 4;
        continue;
      } else if (browser_strncasecmp(ptr, "&gt;", 4) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = '>';
        ptr += 4;
        continue;
      } else if (browser_strncasecmp(ptr, "&amp;", 5) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = '&';
        ptr += 5;
        continue;
      } else if (browser_strncasecmp(ptr, "&quot;", 6) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = '"';
        ptr += 6;
        continue;
      } else if (browser_strncasecmp(ptr, "&apos;", 6) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = '\'';
        ptr += 6;
        continue;
      } else if (browser_strncasecmp(ptr, "&ndash;", 7) == 0 ||
                 browser_strncasecmp(ptr, "&mdash;", 7) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = '-';
        ptr += 7;
        continue;
      } else if (browser_strncasecmp(ptr, "&hellip;", 8) == 0) {
        if (text_pos + 3 < (int)sizeof(text_buf)) {
          text_buf[text_pos++] = '.';
          text_buf[text_pos++] = '.';
          text_buf[text_pos++] = '.';
        }
        ptr += 8;
        continue;
      } else if (browser_strncasecmp(ptr, "&euro;", 6) == 0) {
        if (text_pos + 3 < (int)sizeof(text_buf)) {
          text_buf[text_pos++] = (char)0xE2; text_buf[text_pos++] = (char)0x82; text_buf[text_pos++] = (char)0xAC;
        }
        ptr += 6; continue;
      } else if (browser_strncasecmp(ptr, "&agrave;", 8) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC3; text_buf[text_pos++] = (char)0xA0; }
        ptr += 8; continue;
      } else if (browser_strncasecmp(ptr, "&egrave;", 8) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC3; text_buf[text_pos++] = (char)0xA8; }
        ptr += 8; continue;
      } else if (browser_strncasecmp(ptr, "&eacute;", 8) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC3; text_buf[text_pos++] = (char)0xA9; }
        ptr += 8; continue;
      } else if (browser_strncasecmp(ptr, "&igrave;", 8) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC3; text_buf[text_pos++] = (char)0xAC; }
        ptr += 8; continue;
      } else if (browser_strncasecmp(ptr, "&ograve;", 8) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC3; text_buf[text_pos++] = (char)0xB2; }
        ptr += 8; continue;
      } else if (browser_strncasecmp(ptr, "&ugrave;", 8) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC3; text_buf[text_pos++] = (char)0xB9; }
        ptr += 8; continue;
      } else if (browser_strncasecmp(ptr, "&rsquo;", 7) == 0 || browser_strncasecmp(ptr, "&lsquo;", 7) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = '\'';
        ptr += 7; continue;
      } else if (browser_strncasecmp(ptr, "&rdquo;", 7) == 0 || browser_strncasecmp(ptr, "&ldquo;", 7) == 0) {
        if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = '"';
        ptr += 7; continue;
      } else if (browser_strncasecmp(ptr, "&bull;", 6) == 0) {
        if (text_pos + 3 < (int)sizeof(text_buf)) {
          text_buf[text_pos++] = (char)0xE2; text_buf[text_pos++] = (char)0x80; text_buf[text_pos++] = (char)0xA2;
        }
        ptr += 6;
        continue;
      } else if (browser_strncasecmp(ptr, "&laquo;", 7) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC2; text_buf[text_pos++] = (char)0xAB; }
        ptr += 7; continue;
      } else if (browser_strncasecmp(ptr, "&raquo;", 7) == 0) {
        if (text_pos + 2 < (int)sizeof(text_buf)) { text_buf[text_pos++] = (char)0xC2; text_buf[text_pos++] = (char)0xBB; }
        ptr += 7; continue;
      } else if (ptr[1] == '#') {
        const char *ep = ptr + 2;
        uint32_t code = 0;
        if (*ep == 'x' || *ep == 'X') {
          ep++;
          while (*ep && *ep != ';' && (ep - ptr) < 10) {
            char c = *ep++;
            code <<= 4;
            if (c >= '0' && c <= '9')
              code |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
              code |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
              code |= (uint32_t)(c - 'A' + 10);
          }
        } else {
          while (*ep >= '0' && *ep <= '9' && (ep - ptr) < 10) {
            code = code * 10 + (uint32_t)(*ep++ - '0');
          }
        }
        if (*ep == ';') {
          if (code >= 32 && code < 128) {
            if (text_pos < (int)sizeof(text_buf) - 1) text_buf[text_pos++] = (char)code;
          } else if (code >= 128 && code < 0x800 && text_pos + 2 < (int)sizeof(text_buf)) {
            text_buf[text_pos++] = (char)(0xC0 | (code >> 6));
            text_buf[text_pos++] = (char)(0x80 | (code & 0x3F));
          } else if (code >= 0x800 && code < 0x10000 && text_pos + 3 < (int)sizeof(text_buf)) {
            text_buf[text_pos++] = (char)(0xE0 | (code >> 12));
            text_buf[text_pos++] = (char)(0x80 | ((code >> 6) & 0x3F));
            text_buf[text_pos++] = (char)(0x80 | (code & 0x3F));
          }
          ptr = ep + 1;
          continue;
        }
      }
    }

    // Carattere di testo normale
    if (*ptr == '\n' || *ptr == '\r' || *ptr == '\t') {
      if (*ptr != '\t' && cur_type == ELEM_PRE) {
        if (text_pos > 0) {
          text_buf[text_pos] = '\0';
          browser_elem_t *el = &state->elems[state->num_elems++];
          el->type = ELEM_PRE;
          strncpy(el->text, text_buf, sizeof(el->text) - 1);
          el->href[0] = '\0';
          el->x = 16;
          el->y = cur_y;
          el->w = text_pos * 8;
          el->h = 16;
          cur_y += 18;
          text_pos = 0;
        }
      } else if (text_pos > 0 && text_buf[text_pos - 1] != ' ') {
        if (text_pos < (int)sizeof(text_buf) - 1)
          text_buf[text_pos++] = ' ';
      }
      ptr++;
      continue;
    }

    // Compattamento spazi multipli fuori da <pre>
    if (*ptr == ' ' && cur_type != ELEM_PRE &&
        (text_pos == 0 || text_buf[text_pos - 1] == ' ')) {
      ptr++;
      continue;
    }

    if (text_pos < (int)sizeof(text_buf) - 1) {
      text_buf[text_pos++] = *ptr;
    }
    ptr++;
  }

  // Flush finale residuo
  if (text_pos > 0 && state->num_elems < MAX_ELEMENTS) {
    text_buf[text_pos] = '\0';
    browser_elem_t *el = &state->elems[state->num_elems++];
    el->type = cur_type;
    strncpy(el->text, text_buf, sizeof(el->text) - 1);
    strncpy(el->href, cur_href, sizeof(el->href) - 1);
    el->x = 12;
    el->y = cur_y;
    el->w = text_pos * 8;
    el->h = 16;
    cur_y += el->h + 4;
  }

  state->content_height = cur_y + 20;
  css_free_sheet(css_sheet);
}

// Funzione di caricamento URL (about:, file:/// o http://)
static void browser_load_url(browser_state_t *state, const char *url,
                             int32_t viewport_w) {
  if (!url || url[0] == '\0')
    url = BROWSER_DEFAULT_HOME;

  serial_log("[BROWSER] Caricamento URL: ");
  serial_log(url);
  serial_log("\n");

  strncpy(state->current_url, url, sizeof(state->current_url) - 1);
  strncpy(state->edit_url, url, sizeof(state->edit_url) - 1);
  state->is_loading = true;
  state->is_secure_local = false;
  state->is_offline = false;
  mini_snprintf(state->status_msg, sizeof(state->status_msg),
                "Connessione in corso a %s...", url);

  // 1. Schemi interni about:
  if (strncmp(url, "about:", 6) == 0) {
    state->is_secure_local = true;
    const char *page = url + 6;
    if (strcmp(page, "network") == 0) {
      generate_network_page(state);
    } else if (strcmp(page, "downloads") == 0) {
      generate_downloads_page(state);
    } else if (strcmp(page, "history") == 0) {
      generate_history_page(state);
    } else if (strcmp(page, "rules") == 0) {
      generate_rules_page(state);
    } else if (strcmp(page, "browser") == 0 || strcmp(page, "info") == 0) {
      strncpy(state->raw_html, g_builtin_about_html, MAX_HTML_SIZE - 1);
      state->raw_html[MAX_HTML_SIZE - 1] = '\0';
      state->raw_html_len = (uint32_t)strlen(state->raw_html);
    } else { // about:home o default
      // Prova a caricare C:\NewOS\Resources\Web\HOME.HTM se presente
      uint32_t fsz = 0;
      char *disk_home = (char *)vfs_read_entire_file(
          "C:\\NewOS\\Resources\\Web\\HOME.HTM", &fsz);
      if (!disk_home)
        disk_home =
            (char *)vfs_read_entire_file("/NewOS/Resources/Web/HOME.HTM", &fsz);
      if (!disk_home)
        disk_home = (char *)vfs_read_entire_file("WebBrowser/home.htm", &fsz);

      if (disk_home && fsz > 0) {
        if (fsz >= MAX_HTML_SIZE)
          fsz = MAX_HTML_SIZE - 1;
        memcpy(state->raw_html, disk_home, fsz);
        state->raw_html[fsz] = '\0';
        state->raw_html_len = fsz;
        kfree(disk_home);
      } else {
        strncpy(state->raw_html, g_builtin_home_html, MAX_HTML_SIZE - 1);
        state->raw_html[MAX_HTML_SIZE - 1] = '\0';
        state->raw_html_len = (uint32_t)strlen(state->raw_html);
      }
    }

    browser_parse_html(state, viewport_w);

    // Esecuzione JS + SPA Hydration via JSENGINE.DLL (out-of-kernel)
    char *js_out = (char *)kmalloc(32768);
    if (js_out) {
      uint32_t js_len = 0, srun = 0, dops = 0;
      char js_title[64] = "";
      bool has_js_title = false;
      if (js_run_page_scripts(state->raw_html, state->current_url, js_out, 32768,
                              &js_len, js_title, &has_js_title, &srun, &dops)) {
        if (js_len > 0 && state->num_elems < 10) {
          // Pagina SPA o generata via JS: renderizza il DOM prodotto da JSENGINE.DLL
          uint32_t cpy = (js_len < MAX_HTML_SIZE - 1) ? js_len : (MAX_HTML_SIZE - 1);
          memcpy(state->raw_html, js_out, cpy);
          state->raw_html[cpy] = '\0';
          state->raw_html_len = cpy;
          char saved_title[64];
          strncpy(saved_title, state->page_title, sizeof(saved_title) - 1);
          saved_title[sizeof(saved_title) - 1] = '\0';
          browser_parse_html(state, viewport_w);
          if (has_js_title && js_title[0]) {
            strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
          } else if (strcmp(state->page_title, "NewSoft Web Browser") == 0 && saved_title[0]) {
            strncpy(state->page_title, saved_title, sizeof(state->page_title) - 1);
          }
        } else if (has_js_title && js_title[0]) {
          strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
        }
      }
      kfree(js_out);
    }

    browser_fetch_images(state);
    serial_log("[POST_LOAD]\n");

    state->is_loading = false;
    mini_snprintf(state->status_msg, sizeof(state->status_msg), "Pronto");
    return;
  }

  // 2. File locali file:/// oppure percorsi Windows C:
  if (strncmp(url, "file:///", 8) == 0 ||
      (url[1] == ':' && (url[2] == '\\' || url[2] == '/'))) {
    state->is_secure_local = true;
    const char *fpath = url;
    if (strncmp(url, "file:///", 8) == 0)
      fpath = url + 8;

    uint32_t fsz = 0;
    char *file_data = (char *)vfs_read_entire_file(fpath, &fsz);
    if (file_data && fsz > 0) {
      if (fsz >= MAX_HTML_SIZE)
        fsz = MAX_HTML_SIZE - 1;
      memcpy(state->raw_html, file_data, fsz);
      state->raw_html[fsz] = '\0';
      state->raw_html_len = fsz;
      kfree(file_data);

      // Se non e' HTML ma testo semplice, impacchetta in <pre>
      if (!strstr(state->raw_html, "<html") &&
          !strstr(state->raw_html, "<HTML")) {
        char *temp = (char *)kmalloc(MAX_HTML_SIZE);
        if (temp) {
          mini_snprintf(temp, MAX_HTML_SIZE,
                        "<html><head><title>%s</title></head>"
                        "<body bgcolor=\"#FFFFFF\"><pre>%s</pre></body></html>",
                        fpath, state->raw_html);
          strncpy(state->raw_html, temp, MAX_HTML_SIZE - 1);
          state->raw_html[MAX_HTML_SIZE - 1] = '\0';
          state->raw_html_len = (uint32_t)strlen(state->raw_html);
          kfree(temp);
        }
      }

      browser_parse_html(state, viewport_w);
      state->is_loading = false;
      mini_snprintf(state->status_msg, sizeof(state->status_msg),
                    "File caricato");
    } else {
      generate_error_page(state, "404", "File non trovato", fpath);
      browser_parse_html(state, viewport_w);
      state->is_loading = false;
      mini_snprintf(state->status_msg, sizeof(state->status_msg),
                    "Non trovato");
    }
    return;
  }

  // 2.5 Gestione pagina locale opzionale about:youtube
  if (strcmp(url, "about:youtube") == 0) {
    state->is_secure_local = false;
    uint32_t fsz = 0;
    char *file_data = (char *)vfs_read_entire_file(
        "C:\\NewOS\\Resources\\Web\\YOUTUBE.HTM", &fsz);
    if (!file_data)
      file_data = (char *)vfs_read_entire_file("/SYSTEM/WEB/YOUTUBE.HTM", &fsz);
    if (!file_data)
      file_data = (char *)vfs_read_entire_file("WebBrowser/youtube.htm", &fsz);

    if (file_data && fsz > 0) {
      if (fsz >= MAX_HTML_SIZE)
        fsz = MAX_HTML_SIZE - 1;
      memcpy(state->raw_html, file_data, fsz);
      state->raw_html[fsz] = '\0';
      state->raw_html_len = fsz;
      kfree(file_data);
    } else {
      mini_snprintf(
          state->raw_html, MAX_HTML_SIZE,
          "<html><title>YouTube</title><body>"
          "<header><h1><font color=\"#CC181E\">> YouTube</font> IT</h1>"
          "<p><button>Cerca</button></p></header><hr>"
          "<main><article><video width=\"520\" height=\"150\"></video>"
          "<h2>YouTube HTML5</h2>"
          "<p>Canale: <font color=\"#0000CC\">NewSoft</font></p>"
          "</article></main></body></html>");
      state->raw_html_len = (uint32_t)strlen(state->raw_html);
    }

    strncpy(state->current_url, "http://www.youtube.it/",
            sizeof(state->current_url) - 1);
    strncpy(state->edit_url, "http://www.youtube.it/",
            sizeof(state->edit_url) - 1);
    browser_parse_html(state, viewport_w);

    // Esecuzione JS + SPA Hydration via JSENGINE.DLL (out-of-kernel)
    char *js_out = (char *)kmalloc(32768);
    if (js_out) {
      uint32_t js_len = 0, srun = 0, dops = 0;
      char js_title[64] = "";
      bool has_js_title = false;
      if (js_run_page_scripts(state->raw_html, state->current_url, js_out, 32768,
                              &js_len, js_title, &has_js_title, &srun, &dops)) {
        if (js_len > 0 && state->num_elems < 10) {
          // Pagina SPA o generata via JS: renderizza il DOM prodotto da JSENGINE.DLL
          uint32_t cpy = (js_len < MAX_HTML_SIZE - 1) ? js_len : (MAX_HTML_SIZE - 1);
          memcpy(state->raw_html, js_out, cpy);
          state->raw_html[cpy] = '\0';
          state->raw_html_len = cpy;
          char saved_title[64];
          strncpy(saved_title, state->page_title, sizeof(saved_title) - 1);
          saved_title[sizeof(saved_title) - 1] = '\0';
          browser_parse_html(state, viewport_w);
          if (has_js_title && js_title[0]) {
            strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
          } else if (strcmp(state->page_title, "NewSoft Web Browser") == 0 && saved_title[0]) {
            strncpy(state->page_title, saved_title, sizeof(state->page_title) - 1);
          }
        } else if (has_js_title && js_title[0]) {
          strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
        }
      }
      kfree(js_out);
    }

    browser_fetch_images(state);
    serial_log("[POST_LOAD]\n");

    state->is_loading = false;
    mini_snprintf(state->status_msg, sizeof(state->status_msg),
                  "YouTube Italia caricato con successo (HTML5)");
    return;
  }

  // 3. Connessione remota HTTP/1.1 e HTTPS (TLS 1.3 via NETTLS.DLL)
  mini_snprintf(state->status_msg, sizeof(state->status_msg), "Connessione: %s",
                url);

  nettls_response_t resp;
  if (nettls_http_get(url, MAX_HTML_SIZE - 1, &resp) && resp.body &&
      resp.body_len > 0) {
    uint32_t copy_len = resp.body_len;
    if (copy_len >= MAX_HTML_SIZE) {
      copy_len = MAX_HTML_SIZE - 1;
    }
    memcpy(state->raw_html, resp.body, copy_len);
    state->raw_html[copy_len] = '\0';
    state->raw_html_len = copy_len;
    state->is_secure_local = resp.tls_active;
    if (resp.final_url[0]) {
      strncpy(state->current_url, resp.final_url,
              sizeof(state->current_url) - 1);
      strncpy(state->edit_url, resp.final_url, sizeof(state->edit_url) - 1);
    }
    int32_t st_code = resp.status_code;
    bool tls_act = resp.tls_active;
    nettls_free_response(&resp);

    // Scarica eventuali CSS esterni (<link rel="stylesheet">) per libcss
    browser_fetch_external_css(state);

    // Parsing HTML5 W3C tramite LibHubbub (NETSURF_CORE.DLL out-of-kernel)
    if (state->raw_html_len > 0 && state->raw_html_len < 262144) {
      char *ns_out = (char *)kmalloc(262144);
      if (ns_out) {
        uint32_t ns_len = 0, nc = 0;
        char ns_title[64] = "";
        bool has_ns_title = false;
        if (netsurf_parse_html5(state->raw_html, state->raw_html_len, ns_out,
                                262144, &ns_len, ns_title, &has_ns_title, &nc) &&
            ns_len > 0) {
          memcpy(state->raw_html, ns_out, ns_len);
          state->raw_html[ns_len] = '\0';
          state->raw_html_len = ns_len;
        }
        kfree(ns_out);
      }
    }

    browser_parse_html(state, viewport_w);

    // Esecuzione JS + SPA Hydration via JSENGINE.DLL (out-of-kernel)
    char *js_out = (char *)kmalloc(32768);
    if (js_out) {
      uint32_t js_len = 0, srun = 0, dops = 0;
      char js_title[64] = "";
      bool has_js_title = false;
      if (js_run_page_scripts(state->raw_html, state->current_url, js_out, 32768,
                              &js_len, js_title, &has_js_title, &srun, &dops)) {
        if (js_len > 0 && state->num_elems < 10) {
          // Pagina SPA o generata via JS: renderizza il DOM prodotto da JSENGINE.DLL
          uint32_t cpy = (js_len < MAX_HTML_SIZE - 1) ? js_len : (MAX_HTML_SIZE - 1);
          memcpy(state->raw_html, js_out, cpy);
          state->raw_html[cpy] = '\0';
          state->raw_html_len = cpy;
          char saved_title[64];
          strncpy(saved_title, state->page_title, sizeof(saved_title) - 1);
          saved_title[sizeof(saved_title) - 1] = '\0';
          browser_parse_html(state, viewport_w);
          if (has_js_title && js_title[0]) {
            strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
          } else if (strcmp(state->page_title, "NewSoft Web Browser") == 0 && saved_title[0]) {
            strncpy(state->page_title, saved_title, sizeof(state->page_title) - 1);
          }
        } else if (has_js_title && js_title[0]) {
          strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
        }
      }
      kfree(js_out);
    }

    browser_fetch_images(state);
    serial_log("[POST_LOAD]\n");

    state->is_loading = false;
    mini_snprintf(state->status_msg, sizeof(state->status_msg),
                  tls_act ? "HTTPS TLS 1.3 (%u B)" : "HTTP %d (%u B)",
                  tls_act ? (unsigned)copy_len : st_code, (unsigned)copy_len);
  } else {
    const char *err =
        resp.error_msg[0] ? resp.error_msg : "Nessun dato dal server";
    nettls_free_response(&resp);
    generate_error_page(state, "Rete / TLS", err, url);
    browser_parse_html(state, viewport_w);

    // Esecuzione JS + SPA Hydration via JSENGINE.DLL (out-of-kernel)
    char *js_out = (char *)kmalloc(32768);
    if (js_out) {
      uint32_t js_len = 0, srun = 0, dops = 0;
      char js_title[64] = "";
      bool has_js_title = false;
      if (js_run_page_scripts(state->raw_html, state->current_url, js_out, 32768,
                              &js_len, js_title, &has_js_title, &srun, &dops)) {
        if (js_len > 0 && state->num_elems < 10) {
          // Pagina SPA o generata via JS: renderizza il DOM prodotto da JSENGINE.DLL
          uint32_t cpy = (js_len < MAX_HTML_SIZE - 1) ? js_len : (MAX_HTML_SIZE - 1);
          memcpy(state->raw_html, js_out, cpy);
          state->raw_html[cpy] = '\0';
          state->raw_html_len = cpy;
          char saved_title[64];
          strncpy(saved_title, state->page_title, sizeof(saved_title) - 1);
          saved_title[sizeof(saved_title) - 1] = '\0';
          browser_parse_html(state, viewport_w);
          if (has_js_title && js_title[0]) {
            strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
          } else if (strcmp(state->page_title, "NewSoft Web Browser") == 0 && saved_title[0]) {
            strncpy(state->page_title, saved_title, sizeof(state->page_title) - 1);
          }
        } else if (has_js_title && js_title[0]) {
          strncpy(state->page_title, js_title, sizeof(state->page_title) - 1);
        }
      }
      kfree(js_out);
    }

    browser_fetch_images(state);
    serial_log("[POST_LOAD]\n");

    state->is_loading = false;
    mini_snprintf(state->status_msg, sizeof(state->status_msg), "Errore: %s",
                  err);
  }
}

// Navigazione a un URL con aggiornamento dello stack storico
static void browser_navigate(window_t *win, const char *target_url,
                             bool push_history) {
  browser_state_t *state = (browser_state_t *)win->app_data;
  if (!state || !target_url)
    return;

  browser_clear_images(state);

  // Normalizzazione URL: pulizia spazi e auto-prefisso http:// se mancante
  char norm_url[MAX_URL_LEN];
  const char *u = target_url;
  while (*u == ' ')
    u++;
  if (*u == '\0')
    u = BROWSER_DEFAULT_HOME;

  if (strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0 ||
      strncmp(u, "about:", 6) == 0 || strncmp(u, "file:///", 8) == 0 ||
      (u[1] == ':' && (u[2] == '\\' || u[2] == '/'))) {
    strncpy(norm_url, u, sizeof(norm_url) - 1);
  } else if (u[0] == '/' && u[1] == '/') {
    mini_snprintf(norm_url, sizeof(norm_url), "https:%s", u);
  } else if (u[0] == '/') {
    // Link relativo rispetto all'host corrente
    const char *cur = state->current_url;
    const char *sch =
        (strncmp(cur, "https://", 8) == 0) ? "https://" : "http://";
    const char *hst = strstr(cur, "://");
    hst = hst ? (hst + 3) : cur;
    char host_only[128];
    size_t hi = 0;
    while (hst[hi] && hst[hi] != '/' && hi + 1 < sizeof(host_only)) {
      host_only[hi] = hst[hi];
      hi++;
    }
    host_only[hi] = '\0';
    if (host_only[0]) {
      mini_snprintf(norm_url, sizeof(norm_url), "%s%s%s", sch, host_only, u);
    } else {
      mini_snprintf(norm_url, sizeof(norm_url), "https://lite.duckduckgo.com%s",
                    u);
    }
  } else if (browser_strncasecmp(u, "yt:", 3) == 0 ||
             browser_strncasecmp(u, "wiki:", 5) == 0 ||
             browser_strncasecmp(u, "g:", 2) == 0 ||
             (!strchr(u, '.') && !strchr(u, ':') &&
              strcmp(u, "localhost") != 0)) {
    // Omnibox intelligente: yt:, wiki:, g: o ricerca web DuckDuckGo Lite
    const char *qsrc = u;
    const char *base_url = "https://lite.duckduckgo.com/lite/?q=";
    if (browser_strncasecmp(u, "yt:", 3) == 0) {
      qsrc = u + 3;
      base_url = "https://www.youtube.com/results?hl=it&gl=IT&search_query=";
    } else if (browser_strncasecmp(u, "wiki:", 5) == 0) {
      qsrc = u + 5;
      base_url = "https://it.wikipedia.org/wiki/";
    } else if (strncmp(u, "g:", 2) == 0) {
      qsrc = u + 2;
      base_url = "https://www.google.com/search?hl=it&gl=IT&q=";
    }
    while (*qsrc == ' ') qsrc++;
    char qenc[160];
    size_t qi = 0;
    for (size_t i = 0; qsrc[i] && qi + 1 < sizeof(qenc); i++) {
      qenc[qi++] = (qsrc[i] == ' ') ? '+' : qsrc[i];
    }
    qenc[qi] = '\0';
    mini_snprintf(norm_url, sizeof(norm_url), "%s%s", base_url, qenc);
  } else {
    mini_snprintf(norm_url, sizeof(norm_url), "http://%s", u);
  }
  norm_url[sizeof(norm_url) - 1] = '\0';

  if (push_history && state->current_url[0] != '\0') {
    if (state->history_back_count < MAX_HISTORY) {
      strncpy(state->history_back[state->history_back_count++],
              state->current_url, MAX_URL_LEN - 1);
    } else {
      for (int i = 0; i < MAX_HISTORY - 1; i++) {
        strcpy(state->history_back[i], state->history_back[i + 1]);
      }
      strncpy(state->history_back[MAX_HISTORY - 1], state->current_url,
              MAX_URL_LEN - 1);
    }
    state->history_fwd_count = 0;
  }

  int32_t vp_w = win->width - 12;
  browser_load_url(state, norm_url, vp_w);
  browser_record_history(state, norm_url);

  if (state->num_tabs > 0 && state->active_tab >= 0 && state->active_tab < state->num_tabs) {
    strncpy(state->tabs[state->active_tab].url, norm_url, MAX_URL_LEN - 1);
    strncpy(state->tabs[state->active_tab].title, state->page_title, 31);
    state->tabs[state->active_tab].title[31] = '\0';
    state->tabs[state->active_tab].scroll_y = 0;
  }

  // Aggiorna titolo della finestra del Window Manager
  if (strstr(state->page_title, "Web Browser")) {
    strncpy(win->title, state->page_title, sizeof(win->title) - 1);
  } else {
    mini_snprintf(win->title, sizeof(win->title), "%s - NewSoft Web Browser",
                  state->page_title);
  }
}

// Rendering dell'interfaccia grafica del Web Browser
static void browser_draw(window_t *win) {
  browser_state_t *state = (browser_state_t *)win->app_data;
  if (!state)
    return;

  int32_t x = win->x;
  int32_t y = win->y;
  int32_t w = win->width;
  int32_t h = win->height;

  // 1. Sfondo generale della finestra in grigio pulsanti Classic 2000
  gfx_fill_rect(x + 2, y + 22, w - 4, h - 24, COLOR_BTNFACE);

  // 2. Barra dei Menu (File, Modifica, Visualizza, Preferiti, ?)
  int32_t menu_y = y + 22;
  gfx_fill_rect(x + 2, menu_y, w - 4, 18, COLOR_BTNFACE);
  gfx_draw_string(x + 10, menu_y + 3, "File", COLOR_WINDOWTEXT, 0, true);
  gfx_draw_string(x + 56, menu_y + 3, "Modifica", COLOR_WINDOWTEXT, 0, true);
  gfx_draw_string(x + 130, menu_y + 3, "Visualizza", COLOR_WINDOWTEXT, 0, true);
  gfx_draw_string(x + 226, menu_y + 3, "Preferiti", COLOR_WINDOWTEXT, 0, true);
  gfx_draw_string(x + 308, menu_y + 3, "?", COLOR_WINDOWTEXT, 0, true);
  gfx_draw_bevel_rect(x + 2, menu_y + 18, w - 4, 2, true);

  // 3. Barra degli Strumenti di Navigazione
  int32_t tb_y = menu_y + 20;
  gfx_fill_rect(x + 2, tb_y, w - 4, 30, COLOR_BTNFACE);

  // [ < Indietro ]
  bool can_back = (state->history_back_count > 0);
  gfx_fill_rect(x + 6, tb_y + 3, 26, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 6, tb_y + 3, 26, 22, false);
  gfx_draw_string(x + 14, tb_y + 6, "<", can_back ? 0x000000 : 0x808080, 0,
                  true);

  // [ > Avanti ]
  bool can_fwd = (state->history_fwd_count > 0);
  gfx_fill_rect(x + 36, tb_y + 3, 26, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 36, tb_y + 3, 26, 22, false);
  gfx_draw_string(x + 44, tb_y + 6, ">", can_fwd ? 0x000000 : 0x808080, 0,
                  true);

  // [ X Stop ]
  gfx_fill_rect(x + 66, tb_y + 3, 26, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 66, tb_y + 3, 26, 22, false);
  gfx_draw_string(x + 74, tb_y + 6, "x", 0xA00000, 0, true);

  // [ R Aggiorna ]
  gfx_fill_rect(x + 96, tb_y + 3, 26, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 96, tb_y + 3, 26, 22, false);
  gfx_draw_string(x + 104, tb_y + 6, "R", 0x006000, 0, true);

  // [ Home ]
  gfx_fill_rect(x + 126, tb_y + 3, 50, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 126, tb_y + 3, 50, 22, false);
  gfx_draw_string(x + 134, tb_y + 6, "Home", COLOR_WINDOWTEXT, 0, true);

  // [ Salva ] (Download pagina corrente in C:\User\Admin\Downloads\)
  gfx_fill_rect(x + 182, tb_y + 3, 58, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 182, tb_y + 3, 58, 22, false);
  gfx_draw_string(x + 191, tb_y + 6, "Salva", 0x0055AA, 0, true);

  // [ Downloads ]
  gfx_fill_rect(x + 244, tb_y + 3, 88, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 244, tb_y + 3, 88, 22, false);
  gfx_draw_string(x + 252, tb_y + 6, "Downloads", COLOR_WINDOWTEXT, 0, true);

  // [ Cronol. ]
  gfx_fill_rect(x + 336, tb_y + 3, 76, 22, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 336, tb_y + 3, 76, 22, false);
  gfx_draw_string(x + 344, tb_y + 6, "Cronol.", COLOR_WINDOWTEXT, 0, true);

  // Icona del globo / connessione animata sulla destra della toolbar
  gfx_draw_bevel_rect(x + w - 34, tb_y + 3, 26, 22, true);
  gfx_draw_icon_16(x + w - 29, tb_y + 6, ICON_NET);

  gfx_draw_bevel_rect(x + 2, tb_y + 28, w - 4, 2, true);

  // 4. Barra degli Indirizzi (URL Address Bar)
  int32_t ab_y = tb_y + 30;
  gfx_fill_rect(x + 2, ab_y, w - 4, 28, COLOR_BTNFACE);
  gfx_draw_string(x + 8, ab_y + 7, "Indirizzo:", COLOR_WINDOWTEXT, 0, true);

  int32_t url_box_x = x + 84;
  int32_t url_box_w = w - 156;
  gfx_fill_rect(url_box_x, ab_y + 4, url_box_w, 20, COLOR_WINDOW);
  gfx_draw_bevel_rect(url_box_x, ab_y + 4, url_box_w, 20, true);

  // Testo dell'URL
  const char *show_url =
      state->url_focused ? state->edit_url : state->current_url;
  gfx_draw_string(url_box_x + 6, ab_y + 6, show_url, COLOR_WINDOWTEXT, 0, true);
  if (state->url_focused) {
    int cur_x = url_box_x + 6 + (int)strlen(show_url) * 8;
    if (cur_x < url_box_x + url_box_w - 6) {
      gfx_fill_rect(cur_x, ab_y + 6, 2, 14, 0x000000);
    }
  }

  // Pulsante [ Vai ]
  int32_t go_btn_x = x + w - 66;
  gfx_fill_rect(go_btn_x, ab_y + 4, 58, 20, COLOR_BTNFACE);
  gfx_draw_bevel_rect(go_btn_x, ab_y + 4, 58, 20, false);
  gfx_draw_string(go_btn_x + 16, ab_y + 6, "Vai", 0x006000, 0, true);

  // 5. Barra dei Preferiti Rapidi
  int32_t fav_y = ab_y + 26;
  gfx_fill_rect(x + 2, fav_y, w - 4, 22, COLOR_BTNFACE);
  gfx_draw_string(x + 8, fav_y + 4, "Link:", 0x505050, 0, true);

  const char *fav_labels[] = {"Home", "YouTube", "Server 95.141.32.1",
                              "Gateway", "Regole"};
  int32_t fav_x = x + 52;
  for (int fi = 0; fi < 5; fi++) {
    int32_t fw = (int32_t)strlen(fav_labels[fi]) * 8 + 12;
    gfx_fill_rect(fav_x, fav_y + 2, fw, 18, COLOR_BTNFACE);
    gfx_draw_bevel_rect(fav_x, fav_y + 2, fw, 18, false);
    gfx_draw_string(fav_x + 6, fav_y + 3, fav_labels[fi],
                    (fi == 1) ? 0xCC181E : COLOR_WINDOWTEXT, 0, true);
    fav_x += fw + 6;
    if (fav_x > x + w - 40)
      break;
  }
  gfx_draw_bevel_rect(x + 2, fav_y + 21, w - 4, 2, true);

  // 5b. Barra delle Schede (Multi-Tab)
  int32_t tab_y = fav_y + 23;
  gfx_fill_rect(x + 2, tab_y, w - 4, 22, 0xCBD5E1);
  int32_t tx = x + 6;
  for (int ti = 0; ti < state->num_tabs; ti++) {
    bool is_act = (ti == state->active_tab);
    int32_t tw = 132;
    if (tx + tw + 30 > x + w - 4) break;
    gfx_fill_rect(tx, tab_y + 2, tw, 20, is_act ? COLOR_WINDOW : 0xE2E8F0);
    gfx_draw_bevel_rect(tx, tab_y + 2, tw, 20, !is_act);
    char lbl[15];
    const char *src_t = state->tabs[ti].title[0] ? state->tabs[ti].title : "Nuova Scheda";
    strncpy(lbl, src_t, 12);
    lbl[12] = '\0';
    gfx_draw_string(tx + 6, tab_y + 6, lbl, COLOR_WINDOWTEXT, 0, true);
    if (state->num_tabs > 1) {
      gfx_draw_string(tx + tw - 14, tab_y + 6, "x", 0x64748B, 0, true);
    }
    tx += tw + 3;
  }
  if (state->num_tabs < MAX_BROWSER_TABS && tx + 24 < x + w - 4) {
    gfx_fill_rect(tx, tab_y + 3, 22, 18, COLOR_BTNFACE);
    gfx_draw_bevel_rect(tx, tab_y + 3, 22, 18, false);
    gfx_draw_string(tx + 7, tab_y + 5, "+", COLOR_WINDOWTEXT, 0, true);
  }

  // 6. Area di Rendering Contenuto HTML (Sunken White Viewport)
  int32_t vp_x = x + 4;
  int32_t vp_y = tab_y + 23;
  int32_t vp_w = w - 8;
  int32_t vp_h = h - (vp_y - y) - 24;

  gfx_fill_rect(vp_x, vp_y, vp_w, vp_h,
                state->has_page_bg ? state->page_bg_color : COLOR_WINDOW);
  gfx_draw_bevel_rect(vp_x, vp_y, vp_w, vp_h, true);

  // Disegna la scrollbar verticale a destra
  int32_t sb_x = vp_x + vp_w - 16;
  int32_t sb_y = vp_y + 2;
  int32_t sb_w = 14;
  int32_t sb_h = vp_h - 4;
  gfx_fill_rect(sb_x, sb_y, sb_w, sb_h, COLOR_BTNFACE);
  gfx_draw_bevel_rect(sb_x, sb_y, sb_w, 14, false); // Pulsante Su
  gfx_draw_string(sb_x + 3, sb_y - 2, "^", 0x000000, 0, true);

  gfx_draw_bevel_rect(sb_x, sb_y + sb_h - 14, sb_w, 14, false); // Pulsante Giu
  gfx_draw_string(sb_x + 3, sb_y + sb_h - 16, "v", 0x000000, 0, true);

  // Calcolo posizione thumb scrollbar
  int32_t thumb_h = 24;
  int32_t thumb_track = sb_h - 28 - thumb_h;
  int32_t thumb_y = sb_y + 14;
  if (state->content_height > vp_h && thumb_track > 0) {
    int max_scroll = state->content_height - vp_h;
    thumb_y += (state->scroll_y * thumb_track) / max_scroll;
  }
  gfx_fill_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, COLOR_BTNFACE);
  gfx_draw_bevel_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, false);

  // Rendering degli elementi HTML visibili nella viewport
  for (int i = 0; i < state->num_elems; i++) {
    browser_elem_t *el = &state->elems[i];
    int32_t draw_y = vp_y + el->y - state->scroll_y;

    // Clipping verticale rapido
    if (draw_y + el->h < vp_y + 2 || draw_y > vp_y + vp_h - 4)
      continue;

    if (el->type == ELEM_H1) {
      uint32_t col = el->color ? el->color : 0x0A246A;
      browser_draw_ttf_string(vp_x + el->x, draw_y, el->text, 22, col, vp_x + vp_w - 20, false);
    } else if (el->type == ELEM_H2) {
      uint32_t col = el->color ? el->color : 0x003399;
      browser_draw_ttf_string(vp_x + el->x, draw_y, el->text, 18, col, vp_x + vp_w - 20, false);
    } else if (el->type == ELEM_H3) {
      uint32_t col = el->color ? el->color : 0x1A365D;
      browser_draw_ttf_string(vp_x + el->x, draw_y, el->text, 16, col, vp_x + vp_w - 20, false);
    } else if (el->type == ELEM_IMG) {
      int32_t ix = vp_x + el->x;
      int32_t iw = el->w;
      int32_t ih = el->h;
      int32_t r = 8;
      if (ix + iw > vp_x + vp_w - 18) iw = (vp_x + vp_w - 18) - ix;
      if (iw > 4 && ih > 4) {
        uint32_t *pix = NULL;
        int pw = 0, ph = 0;
        if (el->img_idx >= 0 && el->img_idx < state->num_imgs && state->imgs[el->img_idx].pixels) {
          pix = state->imgs[el->img_idx].pixels;
          pw = state->imgs[el->img_idx].w;
          ph = state->imgs[el->img_idx].h;
        }
        int32_t y0 = (draw_y < vp_y + 2) ? (vp_y + 2 - draw_y) : 0;
        int32_t y1 = (draw_y + ih > vp_y + vp_h - 4) ? (vp_y + vp_h - 4 - draw_y) : ih;
        for (int32_t py = y0; py < y1; py++) {
          int sy = (pix && ph > 0) ? ((py * ph) / ih) : 0;
          for (int32_t px = 0; px < iw; px++) {
            // Maschera angoli arrotondati r=8
            int32_t dx = (px < r) ? (r - 1 - px) : ((px >= iw - r) ? (px - (iw - r)) : 0);
            int32_t dy = (py < r) ? (r - 1 - py) : ((py >= ih - r) ? (py - (ih - r)) : 0);
            if (dx > 0 && dy > 0 && (dx * dx + dy * dy > r * r)) continue;
            uint32_t col = 0xE5E5E5;
            if (pix && pw > 0) {
              int sx = (px * pw) / iw;
              uint32_t c = pix[sy * pw + sx];
              uint8_t a = (uint8_t)(c >> 24);
              if (a == 0) continue;
              col = c & 0x00FFFFFF;
            }
            gfx_draw_pixel(ix + px, draw_y + py, col);
          }
        }
        // Badge durata in basso a destra (es. 10:27)
        if (el->badge[0]) {
          int32_t bw = (int32_t)strlen(el->badge) * 8 + 8;
          int32_t bx = ix + iw - bw - 6;
          int32_t by = draw_y + ih - 18;
          if (by >= vp_y + 2 && by + 15 <= vp_y + vp_h - 4) {
            gfx_fill_rect(bx, by, bw, 15, 0x181818);
            gfx_draw_string(bx + 4, by, el->badge, 0xFFFFFF, 0, true);
          }
        }
      }
    } else if (el->type == ELEM_VIDEO) {
      int32_t vw = el->w;
      int32_t vh = el->h;
      if (vw > vp_w - 36)
        vw = vp_w - 36;
      // Sfondo scuro del player video HTML5
      gfx_fill_rect(vp_x + el->x, draw_y, vw, vh, 0x111111);
      gfx_draw_bevel_rect(vp_x + el->x, draw_y, vw, vh, true);

      // Pulsante Play centrale stile YouTube
      int32_t btn_cx = vp_x + el->x + (vw / 2);
      int32_t btn_cy = draw_y + (vh / 2) - 10;
      gfx_fill_rect(btn_cx - 24, btn_cy - 16, 48, 32, 0xCC181E);
      gfx_draw_bevel_rect(btn_cx - 24, btn_cy - 16, 48, 32, false);
      gfx_draw_string(btn_cx - 4, btn_cy - 7, ">", 0xFFFFFF, 0, true);

      // Barra di controllo inferiore del video HTML5
      int32_t ctrl_y = draw_y + vh - 24;
      gfx_fill_rect(vp_x + el->x, ctrl_y, vw, 24, 0x222222);
      gfx_fill_rect(vp_x + el->x + 6, ctrl_y + 4, vw - 12, 4, 0x444444);
      gfx_fill_rect(vp_x + el->x + 6, ctrl_y + 4, (vw - 12) / 3, 4, 0xCC181E);
      gfx_draw_string(vp_x + el->x + 8, ctrl_y + 10,
                      ">  ||   0:42 / 4:18    [HD]  [Audio]  [HTML5]", 0xDDDDDD,
                      0, true);
    } else if (el->type == ELEM_BUTTON) {
      uint32_t bg = el->has_bg ? el->bg_color : COLOR_BTNFACE;
      uint32_t fg = el->color ? el->color : COLOR_WINDOWTEXT;
      gfx_fill_rect(vp_x + el->x, draw_y, el->w + 14, 20, bg);
      gfx_draw_bevel_rect(vp_x + el->x, draw_y, el->w + 14, 20, false);
      gfx_draw_string(vp_x + el->x + 6, draw_y + 3, el->text, fg,
                      0, true);
    } else if (el->type == ELEM_HR) {
      int32_t hw = (el->w > 0 && el->w < vp_w - 24) ? el->w : (vp_w - 32);
      gfx_fill_rect(vp_x + el->x, draw_y + 2, hw, 1, 0xE5E5E5);
    } else if (el->type == ELEM_LINK) {
      bool is_pill = (el->has_bg || el->has_border || el->border_radius > 0);
      int32_t r = (el->border_radius == 50) ? (el->h / 2) : el->border_radius;
      if (el->has_bg) {
        browser_fill_round_rect(vp_x + el->x, draw_y, el->w, el->h, r, el->bg_color);
      }
      if (el->has_border) {
        browser_stroke_round_rect(vp_x + el->x, draw_y, el->w, el->h, r, el->border_color);
      }
      bool is_hov = (i == state->hover_elem);
      uint32_t link_col = is_hov ? 0xCC0000 : (el->color ? el->color : 0x0000CC);
      int px_h = el->font_size ? el->font_size : 15;
      int32_t tx = vp_x + el->x + (is_pill ? 7 : 0);
      int32_t ty = draw_y + (is_pill ? 3 : 0);
      bool ul = !el->no_underline && !is_pill;
      browser_draw_ttf_string(tx, ty, el->text, px_h, link_col, vp_x + vp_w - 20, ul);
      if (el->is_bold) browser_draw_ttf_string(tx + 1, ty, el->text, px_h, link_col, vp_x + vp_w - 20, false);
    } else if (el->type == ELEM_LIST_ITEM) {
      gfx_fill_rect(vp_x + el->x - 10, draw_y + 6, 4, 4, 0x000000);
      browser_draw_ttf_string(vp_x + el->x, draw_y, el->text, 15, COLOR_WINDOWTEXT, vp_x + vp_w - 20, false);
    } else if (el->type == ELEM_PRE) {
      gfx_fill_rect(vp_x + el->x - 2, draw_y - 1, el->w + 4, el->h + 2,
                    0xF4F4F4);
      gfx_draw_string(vp_x + el->x, draw_y, el->text, 0x222222, 0, true);
    } else {
      bool is_pill = (el->has_bg || el->has_border || el->border_radius > 0);
      int32_t r = (el->border_radius == 50) ? (el->h / 2) : el->border_radius;
      if (el->has_shadow && (el->has_bg || el->has_border)) {
        browser_fill_round_rect(vp_x + el->x + 2, draw_y + 2, el->w, el->h, r, 0xE0E0E0);
      }
      if (el->has_bg) {
        browser_fill_round_rect(vp_x + el->x, draw_y, el->w, el->h, r, el->bg_color);
      }
      if (el->has_border) {
        browser_stroke_round_rect(vp_x + el->x, draw_y, el->w, el->h, r, el->border_color);
      }
      uint32_t col = el->color ? el->color : COLOR_WINDOWTEXT;
      int px_h = el->font_size ? el->font_size : 15;
      int32_t tx = vp_x + el->x + (is_pill ? 7 : 0);
      int32_t ty = draw_y + (is_pill ? 4 : 0);
      browser_draw_ttf_string(tx, ty, el->text, px_h, col, vp_x + vp_w - 20, false);
      if (el->is_bold) browser_draw_ttf_string(tx + 1, ty, el->text, px_h, col, vp_x + vp_w - 20, false);
      if (el->text_deco == CSS_TEXT_DECO_LINE_THRU) {
        gfx_fill_rect(tx, ty + px_h / 2, el->w, 1, col);
      } else if (el->text_deco == CSS_TEXT_DECO_OVERLINE) {
        gfx_fill_rect(tx, ty, el->w, 1, col);
      }
    }
  }
  if (state->num_imgs > 0 && state->imgs[0].pixels) {
    static int last_logged_imgs = 0;
    if (last_logged_imgs != state->num_imgs) {
      last_logged_imgs = state->num_imgs;
      serial_log("[DRAW_IMGS_OK]\n");
    }
  }

  // 7. Barra di Stato Inferiore
  int32_t stat_y = y + h - 22;
  gfx_fill_rect(x + 2, stat_y, w - 4, 20, COLOR_BTNFACE);

  // Pannello 1: Messaggio di stato
  int32_t sp1_w = w - 190;
  gfx_draw_bevel_rect(x + 4, stat_y + 2, sp1_w, 16, true);
  gfx_draw_string(x + 8, stat_y + 4, state->status_msg, COLOR_WINDOWTEXT, 0,
                  true);

  // Pannello 2: Zona di sicurezza
  int32_t sp2_x = x + 4 + sp1_w + 4;
  gfx_draw_bevel_rect(sp2_x, stat_y + 2, 140, 16, true);
  if (state->is_secure_local) {
    gfx_draw_string(sp2_x + 6, stat_y + 4, "Computer locale", 0x005500, 0,
                    true);
  } else {
    gfx_draw_string(sp2_x + 6, stat_y + 4, "Internet (Protetto)", 0x000088, 0,
                    true);
  }

  // Pannello 3: Icona status
  int32_t sp3_x = sp2_x + 144;
  gfx_draw_bevel_rect(sp3_x, stat_y + 2, 28, 16, true);
  gfx_draw_icon_16(sp3_x + 6, stat_y + 2, ICON_NET);
}

// Gestione eventi mouse
static void browser_mouse(window_t *win, int32_t local_x, int32_t local_y,
                          bool clicked) {
  browser_state_t *state = (browser_state_t *)win->app_data;
  if (!state)
    return;

  int32_t w = win->width;
  int32_t h = win->height;

  int32_t tb_y = 42;
  int32_t ab_y = tb_y + 30;
  int32_t fav_y = ab_y + 26;
  int32_t tab_y = fav_y + 23;
  int32_t vp_x = 4;
  int32_t vp_y = tab_y + 23;
  int32_t vp_w = w - 8;
  int32_t vp_h = h - vp_y - 24;

  // Rileva hover su collegamenti ipertestuali nella viewport
  state->hover_elem = -1;
  if (local_x >= vp_x && local_x <= vp_x + vp_w - 18 && local_y >= vp_y &&
      local_y <= vp_y + vp_h) {
    for (int i = 0; i < state->num_elems; i++) {
      browser_elem_t *el = &state->elems[i];
      if (el->type != ELEM_LINK)
        continue;
      int32_t ey = vp_y + el->y - state->scroll_y;
      if (local_x >= vp_x + el->x && local_x <= vp_x + el->x + el->w &&
          local_y >= ey && local_y <= ey + el->h) {
        state->hover_elem = i;
        mini_snprintf(state->status_msg, sizeof(state->status_msg),
                      "Collega a: %s", el->href);
        break;
      }
    }
  }

  if (!clicked)
    return;

  // Clic sulla barra degli strumenti
  if (local_y >= tb_y + 3 && local_y <= tb_y + 25) {
    // [ < Indietro ]
    if (local_x >= 6 && local_x <= 32 && state->history_back_count > 0) {
      char prev_url[MAX_URL_LEN];
      strcpy(prev_url, state->history_back[--state->history_back_count]);
      if (state->history_fwd_count < MAX_HISTORY) {
        strcpy(state->history_fwd[state->history_fwd_count++],
               state->current_url);
      }
      browser_load_url(state, prev_url, vp_w);
      return;
    }

    // [ > Avanti ]
    if (local_x >= 36 && local_x <= 62 && state->history_fwd_count > 0) {
      char next_url[MAX_URL_LEN];
      strcpy(next_url, state->history_fwd[--state->history_fwd_count]);
      if (state->history_back_count < MAX_HISTORY) {
        strcpy(state->history_back[state->history_back_count++],
               state->current_url);
      }
      browser_load_url(state, next_url, vp_w);
      return;
    }

    // [ X Stop ]
    if (local_x >= 66 && local_x <= 92) {
      state->is_loading = false;
      mini_snprintf(state->status_msg, sizeof(state->status_msg),
                    "Operazione interrotta dall'utente");
      return;
    }

    // [ R Aggiorna ]
    if (local_x >= 96 && local_x <= 122) {
      browser_load_url(state, state->current_url, vp_w);
      return;
    }

    // [ Home ]
    if (local_x >= 126 && local_x <= 176) {
      browser_navigate(win, BROWSER_DEFAULT_HOME, true);
      return;
    }

    // [ Salva ]
    if (local_x >= 182 && local_x <= 240) {
      browser_save_current_download(state);
      return;
    }

    // [ Downloads ]
    if (local_x >= 244 && local_x <= 332) {
      browser_navigate(win, "about:downloads", true);
      return;
    }

    // [ Cronol. ]
    if (local_x >= 336 && local_x <= 412) {
      browser_navigate(win, "about:history", true);
      return;
    }
  }

  // Clic sulla Barra delle Schede (Multi-Tab)
  if (local_y >= tab_y && local_y <= tab_y + 22) {
    int32_t tx = 6;
    for (int ti = 0; ti < state->num_tabs; ti++) {
      int32_t tw = 132;
      if (tx + tw + 30 > w - 4) break;
      if (local_x >= tx && local_x < tx + tw) {
        if (state->num_tabs > 1 && local_x >= tx + tw - 18) {
          for (int k = ti; k < state->num_tabs - 1; k++) state->tabs[k] = state->tabs[k + 1];
          state->num_tabs--;
          if (state->active_tab >= state->num_tabs) state->active_tab = state->num_tabs - 1;
          browser_navigate(win, state->tabs[state->active_tab].url, false);
        } else if (ti != state->active_tab) {
          state->tabs[state->active_tab].scroll_y = state->scroll_y;
          state->active_tab = ti;
          browser_navigate(win, state->tabs[ti].url, false);
          state->scroll_y = state->tabs[ti].scroll_y;
        }
        return;
      }
      tx += tw + 3;
    }
    if (state->num_tabs < MAX_BROWSER_TABS && local_x >= tx && local_x <= tx + 24) {
      state->tabs[state->active_tab].scroll_y = state->scroll_y;
      state->active_tab = state->num_tabs++;
      strcpy(state->tabs[state->active_tab].url, BROWSER_DEFAULT_HOME);
      strcpy(state->tabs[state->active_tab].title, "Nuova Scheda");
      state->tabs[state->active_tab].scroll_y = 0;
      browser_navigate(win, BROWSER_DEFAULT_HOME, false);
      return;
    }
  }

  // Clic sulla barra degli indirizzi o pulsante [ Vai ]
  if (local_y >= ab_y + 4 && local_y <= ab_y + 24) {
    int32_t url_box_x = 84;
    int32_t url_box_w = w - 156;
    int32_t go_btn_x = w - 66;

    if (local_x >= url_box_x && local_x <= url_box_x + url_box_w) {
      state->url_focused = true;
      return;
    }

    if (local_x >= go_btn_x && local_x <= go_btn_x + 58) {
      state->url_focused = false;
      browser_navigate(win, state->edit_url, true);
      return;
    }
  }

  // Clic sulla barra dei Preferiti
  if (local_y >= fav_y + 2 && local_y <= fav_y + 20) {
    const char *fav_urls[] = {"about:home", "https://www.youtube.com/results?hl=it&gl=IT&search_query=newos",
                              "http://95.141.32.1", "http://10.0.2.2",
                              "about:rules"};
    const char *fav_labels[] = {"Home", "YouTube", "Server 95.141.32.1",
                                "Gateway", "Regole"};
    int32_t fx = 52;
    for (int fi = 0; fi < 5; fi++) {
      int32_t fw = (int32_t)strlen(fav_labels[fi]) * 8 + 12;
      if (local_x >= fx && local_x <= fx + fw) {
        state->url_focused = false;
        browser_navigate(win, fav_urls[fi], true);
        return;
      }
      fx += fw + 6;
    }
  }

  // Clic nella Viewport HTML (collegamenti ipertestuali o scroll)
  if (local_x >= vp_x && local_x <= vp_x + vp_w && local_y >= vp_y &&
      local_y <= vp_y + vp_h) {
    state->url_focused = false;

    // Clic sulla scrollbar
    int32_t sb_x = vp_x + vp_w - 16;
    if (local_x >= sb_x) {
      if (local_y <= vp_y + 16) {
        if (state->scroll_y > 0)
          state->scroll_y -= 32;
        if (state->scroll_y < 0)
          state->scroll_y = 0;
      } else if (local_y >= vp_y + vp_h - 16) {
        if (state->scroll_y + vp_h < state->content_height)
          state->scroll_y += 32;
      } else {
        // Salto proporzionale
        int32_t rel_y = local_y - (vp_y + 16);
        int32_t track_h = vp_h - 32;
        if (track_h > 0 && state->content_height > vp_h) {
          state->scroll_y = (rel_y * (state->content_height - vp_h)) / track_h;
        }
      }
      return;
    }

    // Clic su un link o immagine cliccabile
    if (state->hover_elem >= 0 && state->hover_elem < state->num_elems) {
      browser_elem_t *el = &state->elems[state->hover_elem];
      if ((el->type == ELEM_LINK || el->type == ELEM_IMG) && el->href[0] != '\0') {
        browser_navigate(win, el->href, true);
        return;
      }
    }
  }
}

// Gestione tastiera
static void browser_key(window_t *win, char c) {
  browser_state_t *state = (browser_state_t *)win->app_data;
  if (!state)
    return;

  if (state->url_focused) {
    if (c == '\r' || c == '\n') {
      state->url_focused = false;
      browser_navigate(win, state->edit_url, true);
      return;
    }
    if (c == 27) { // Escape
      state->url_focused = false;
      strncpy(state->edit_url, state->current_url, sizeof(state->edit_url) - 1);
      return;
    }
    if (c == '\b') { // Backspace
      size_t len = strlen(state->edit_url);
      if (len > 0) {
        state->edit_url[len - 1] = '\0';
      }
      return;
    }
    if (c >= 32 && c <= 126) {
      size_t len = strlen(state->edit_url);
      if (len < sizeof(state->edit_url) - 2) {
        state->edit_url[len] = c;
        state->edit_url[len + 1] = '\0';
      }
      return;
    }
  } else {
    // Ctrl+T (20): nuova scheda; Ctrl+W (23): chiudi scheda corrente
    if (c == 20 && state->num_tabs < MAX_BROWSER_TABS) {
      state->tabs[state->active_tab].scroll_y = state->scroll_y;
      state->active_tab = state->num_tabs++;
      strcpy(state->tabs[state->active_tab].url, BROWSER_DEFAULT_HOME);
      strcpy(state->tabs[state->active_tab].title, "Nuova Scheda");
      state->tabs[state->active_tab].scroll_y = 0;
      browser_navigate(win, BROWSER_DEFAULT_HOME, false);
      return;
    }
    if (c == 23 && state->num_tabs > 1) {
      int ti = state->active_tab;
      for (int k = ti; k < state->num_tabs - 1; k++) state->tabs[k] = state->tabs[k + 1];
      state->num_tabs--;
      if (state->active_tab >= state->num_tabs) state->active_tab = state->num_tabs - 1;
      browser_navigate(win, state->tabs[state->active_tab].url, false);
      return;
    }
    // Tasti di scorrimento pagina
    int32_t vp_h = win->height - 151 - 24;
    if (c == 'w' || c == 'W' || c == 0x11) { // Up
      if (state->scroll_y > 0)
        state->scroll_y -= 24;
      if (state->scroll_y < 0)
        state->scroll_y = 0;
    } else if (c == 's' || c == 'S' || c == 0x12) { // Down
      if (state->scroll_y + vp_h < state->content_height)
        state->scroll_y += 24;
    } else if (c == ' ') { // Space = Page Down
      state->scroll_y += vp_h - 40;
      if (state->scroll_y + vp_h > state->content_height) {
        state->scroll_y = state->content_height - vp_h;
      }
      if (state->scroll_y < 0)
        state->scroll_y = 0;
    } else if (c == '\b') { // Backspace = Indietro nella cronologia
      if (state->history_back_count > 0) {
        char prev_url[MAX_URL_LEN];
        strcpy(prev_url, state->history_back[--state->history_back_count]);
        browser_load_url(state, prev_url, win->width - 12);
      }
    }
  }
}

// Funzione di creazione finestra Web Browser
static window_t *browser_create_window(const char *initial_url) {
  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();

  int32_t bw = 780;
  int32_t bh = 540;
  if (bw > (int32_t)sw - 20)
    bw = sw - 20;
  if (bh > (int32_t)sh - 40)
    bh = sh - 40;

  int32_t bx = (sw - bw) / 2;
  int32_t by = (sh - bh) / 2 - 10;
  if (by < 10)
    by = 10;

  window_t *win =
      wm_create_window("NewSoft Web Browser", bx, by, bw, bh, APP_BROWSER);
  if (!win)
    return NULL;

  browser_state_t *state = (browser_state_t *)kmalloc(sizeof(browser_state_t));
  if (!state)
    return NULL;

  memset(state, 0, sizeof(browser_state_t));
  state->raw_html = (char *)kmalloc(MAX_HTML_SIZE);
  state->elems =
      (browser_elem_t *)kmalloc(sizeof(browser_elem_t) * MAX_ELEMENTS);
  if (!state->raw_html || !state->elems) {
    if (state->raw_html)
      kfree(state->raw_html);
    if (state->elems)
      kfree(state->elems);
    kfree(state);
    return NULL;
  }
  state->raw_html[0] = '\0';
  state->hover_elem = -1;
  state->num_tabs = 1;
  state->active_tab = 0;

  win->app_data = state;
  win->on_draw = browser_draw;
  win->on_mouse = browser_mouse;
  win->on_key = browser_key;

  browser_navigate(
      win, (initial_url && initial_url[0]) ? initial_url : BROWSER_DEFAULT_HOME,
      false);
  return win;
}

void app_browser_launch(void) { browser_create_window(BROWSER_DEFAULT_HOME); }

void app_browser_open_url(const char *url) { browser_create_window(url); }
