#include "bootscreen.h"

// Header di protezione del modulo (esattamente 64 byte all'inizio del file)
__attribute__((section(".header"),
               used)) module_header_t g_bootscreen_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_BOOTSCREEN,
    .code_size = 0,     // Popolato dal build system
    .entry_offset = 64, // Posizione dell'entry point
    .checksum = 0,      // Popolato dal build system
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "BOOTSCREEN",
    .signature = MODULE_AUTH_TAG};

static const char g_boot_steps[7][64] = {
    "Inizializzazione sottosistema memoria e heap a 64-bit...",
    "Rilevamento processore x86_64, bus PCI e timer PIT...",
    "Montaggio volume file system Microsoft exFAT (C:\\)...",
    "Caricamento driver modulari da C:\\NewOS\\Driver\\...",
    "Avvio stack di rete TCP/IP ed interfacce Ethernet...",
    "Verifica crittografica e autenticazione moduli protetti...",
    "Preparazione interfaccia utente e servizi di sistema..."};

typedef struct {
  int duration_ms;
  bool show_progress;
  bool allow_skip;
  char sound_path[128];
} bootscreen_cfg_t;

static int bs_strlen(const char *s) {
  int len = 0;
  while (s && s[len])
    len++;
  return len;
}

static void bs_strcpy(char *dst, const char *src, int max_len) {
  if (!dst || !src || max_len <= 0)
    return;
  int i = 0;
  while (i < max_len - 1 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static char bs_tolower(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c + ('a' - 'A'));
  return c;
}

static bool bs_starts_with_ci(const char *line, const char *key) {
  if (!line || !key)
    return false;
  int i = 0;
  while (key[i]) {
    if (bs_tolower(line[i]) != bs_tolower(key[i]))
      return false;
    i++;
  }
  return line[i] == '=';
}

static int bs_atoi(const char *s) {
  int v = 0;
  if (!s)
    return 0;
  while (*s == ' ' || *s == '\t')
    s++;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  return v;
}

static void load_bootscreen_config(const kernel_api_t *api,
                                   const bootscreen_params_t *params,
                                   bootscreen_cfg_t *cfg) {
  cfg->duration_ms =
      (params && params->duration_ms > 0) ? params->duration_ms : 1800;
  cfg->show_progress = true;
  cfg->allow_skip = params ? params->allow_skip : true;
  bs_strcpy(cfg->sound_path, "C:\\NewOS\\Resources\\Sounds\\STARTUP.WAV",
            sizeof(cfg->sound_path));

  if (!api || !api->vfs_read_entire_file)
    return;

  static const char ini_paths[4][48] = {
      "C:\\NewOS\\Boot\\BOOTSCR.INI", "C:\\NewOS\\SystemProgram\\BOOTSCR.INI",
      "/NewOS/Boot/BOOTSCR.INI", "C:\\NewOS\\Boot\\bootscreen.ini"};

  uint32_t sz = 0;
  char *buf = NULL;
  for (int i = 0; i < 4; i++) {
    buf = (char *)api->vfs_read_entire_file(ini_paths[i], &sz);
    if (buf && sz > 0)
      break;
    if (buf && api->kfree) {
      api->kfree(buf);
      buf = NULL;
    }
  }
  if (!buf || sz == 0)
    return;

  uint32_t pos = 0;
  while (pos < sz) {
    while (pos < sz && (buf[pos] == '\r' || buf[pos] == '\n' ||
                        buf[pos] == ' ' || buf[pos] == '\t'))
      pos++;
    if (pos >= sz)
      break;
    uint32_t start = pos;
    while (pos < sz && buf[pos] != '\r' && buf[pos] != '\n')
      pos++;
    uint32_t len = pos - start;
    if (len > 0 && len < 200) {
      char line[200];
      for (uint32_t k = 0; k < len; k++)
        line[k] = buf[start + k];
      line[len] = '\0';

      if (bs_starts_with_ci(line, "Duration")) {
        int d = bs_atoi(line + 9);
        if (d >= 400 && d <= 10000)
          cfg->duration_ms = d;
      } else if (bs_starts_with_ci(line, "ShowProgress")) {
        cfg->show_progress = (bs_atoi(line + 13) != 0);
      } else if (bs_starts_with_ci(line, "AllowSkip")) {
        cfg->allow_skip = (bs_atoi(line + 10) != 0);
      } else if (bs_starts_with_ci(line, "Sound")) {
        const char *val = line + 6;
        if (val[0])
          bs_strcpy(cfg->sound_path, val, sizeof(cfg->sound_path));
      }
    }
  }

  if (api->kfree)
    api->kfree(buf);
}

static void draw_boot_background(const kernel_api_t *api, image_t *bg_img,
                                 uint32_t sw, uint32_t sh) {
  if (bg_img && api->gfx_draw_image) {
    if (api->gfx_clear)
      api->gfx_clear(0x050C1A);
    int32_t ix = ((int32_t)sw - bg_img->width) / 2;
    int32_t iy = ((int32_t)sh - bg_img->height) / 2;
    api->gfx_draw_image(ix, iy, bg_img);
  } else {
    // Fallback procedurale con gradiente blu profondo
    if (api->gfx_draw_gradient_v) {
      api->gfx_draw_gradient_v(0, 0, sw, sh, 0x050C1A, 0x18345C);
    } else if (api->gfx_clear) {
      api->gfx_clear(0x0A246A);
    }

    int32_t logo_x = (int32_t)sw / 2 - 18;
    int32_t logo_y = (int32_t)sh / 2 - 95;
    if (api->gfx_draw_windows_logo) {
      api->gfx_draw_windows_logo(logo_x, logo_y);
    }

    const char *title = "NewOS V1 64-bit Edition";
    int32_t title_w = bs_strlen(title) * 8;
    int32_t title_x = ((int32_t)sw - title_w) / 2;
    int32_t title_y = logo_y + 42;
    if (api->gfx_draw_string) {
      api->gfx_draw_string(title_x + 1, title_y + 1, title, 0x000000, 0, true);
      api->gfx_draw_string(title_x, title_y, title, 0xFFFFFF, 0, true);
    }

    const char *sub = "Copyright (C) @ Newspoint Software - NewSoft - NewOS V1";
    int32_t sub_w = bs_strlen(sub) * 8;
    int32_t sub_x = ((int32_t)sw - sub_w) / 2;
    int32_t sub_y = title_y + 22;
    if (api->gfx_draw_string) {
      api->gfx_draw_string(sub_x, sub_y, sub, 0x8DA9C4, 0, true);
    }
  }

  const char *badge = "[ Modulo Protetto Esterno: C:\\NewOS\\Boot\\BOOTSCR.MOD "
                      "- Ring 0 Native ]";
  int32_t badge_x = ((int32_t)sw - (bs_strlen(badge) * 8)) / 2;
  if (api->gfx_draw_string) {
    api->gfx_draw_string(badge_x, (int32_t)sh - 28, badge, 0x486581, 0, true);
  }
}

__attribute__((section(".text.entry"), used)) int
bootscreen_main(const kernel_api_t *api, const bootscreen_params_t *params) {
  if (!api)
    return BOOTSCREEN_ERROR;

  if (api->serial_puts) {
    api->serial_puts(
        "[BOOTSCR.MOD] Esecuzione Modulo Bootscreen esterno protetto...\n");
  }

  bootscreen_cfg_t cfg;
  load_bootscreen_config(api, params, &cfg);

  const kernel_ext_api_t *ext = (const kernel_ext_api_t *)api->ext_api;
  const nsmotion_api_t *motion = NULL;
  if (ext && ext->get_system_service) {
    motion = (const nsmotion_api_t *)ext->get_system_service("NSMOTION");
  } else if (ext && ext->nsmotion_get_api) {
    motion = (const nsmotion_api_t *)ext->nsmotion_get_api();
  }

  uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
  uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

  image_t *bg_img = NULL;
  if (api->image_load_from_file) {
    bg_img =
        api->image_load_from_file("C:\\NewOS\\Resources\\Screens\\BOOTSCR.BMP");
    if (!bg_img)
      bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\BOOTSCR.BMP");
    if (!bg_img)
      bg_img = api->image_load_from_file(
          "C:\\NewOS\\Resource\\wallpapers\\BOOTSCR.BMP");
    if (!bg_img)
      bg_img =
          api->image_load_from_file("/NewOS/Resources/Screens/BOOTSCR.BMP");
  }

  if (bg_img && api->serial_puts) {
    api->serial_puts(
        "[BOOTSCR.MOD] Immagine bootScreen caricata con successo.\n");
  }

  int total_steps = (int)(sizeof(g_boot_steps) / sizeof(g_boot_steps[0]));
  int total_frames = cfg.duration_ms / 16;
  if (total_frames < 25)
    total_frames = 25;

  // Coordinate dinamiche rispetto alla risoluzione
  int32_t bar_w = (sw >= 800) ? 440 : (int32_t)sw - 120;
  int32_t bar_h = 14;
  int32_t bar_x = ((int32_t)sw - bar_w) / 2;
  int32_t bar_y = bg_img ? ((int32_t)sh / 2 + ((int32_t)sh * 205) / 768)
                         : ((int32_t)sh / 2 + 70);
  if (bar_y > (int32_t)sh - 86)
    bar_y = (int32_t)sh - 86;

  // Area dirty-rect per barra + riga stato (evita ridisegno full-screen ad ogni
  // frame)
  int32_t dirty_x = ((int32_t)sw - 640) / 2;
  if (dirty_x < 0)
    dirty_x = 0;
  int32_t dirty_w = (sw >= 640) ? 640 : (int32_t)sw;
  int32_t dirty_y = bar_y - 6;
  int32_t dirty_h = 54;
  if (dirty_y + dirty_h > (int32_t)sh)
    dirty_h = (int32_t)sh - dirty_y;

  // Suono di avvio
  if (api->sound_play_wav_file && cfg.sound_path[0]) {
    if (!api->sound_play_wav_file(cfg.sound_path)) {
      api->sound_play_wav_file("/NewOS/Resources/Sounds/STARTUP.WAV");
    }
  } else if (api->sound_play) {
    api->sound_play(1); // SOUND_STARTUP
  }

  // Disegna lo sfondo una sola volta e cattura lo snapshot della sola area
  // dirty da bg_img in RAM
  draw_boot_background(api, bg_img, sw, sh);

  uint32_t *dirty_bg = NULL;
  if (bg_img && bg_img->pixels && api->kmalloc && api->gfx_draw_pixel &&
      dirty_w > 0 && dirty_h > 0) {
    dirty_bg = (uint32_t *)api->kmalloc((size_t)(dirty_w * dirty_h) *
                                        sizeof(uint32_t));
    if (dirty_bg) {
      int32_t ix = ((int32_t)sw - bg_img->width) / 2;
      int32_t iy = ((int32_t)sh - bg_img->height) / 2;
      for (int32_t ry = 0; ry < dirty_h; ry++) {
        int32_t sy = dirty_y + ry - iy;
        for (int32_t rx = 0; rx < dirty_w; rx++) {
          int32_t sx = dirty_x + rx - ix;
          if (sx >= 0 && sx < bg_img->width && sy >= 0 && sy < bg_img->height) {
            dirty_bg[ry * dirty_w + rx] =
                bg_img->pixels[sy * bg_img->width + sx];
          } else {
            dirty_bg[ry * dirty_w + rx] = 0x050C1A;
          }
        }
      }
    }
  }

  bool verbose_mode = false;
  int frame = 0;

  while (frame < total_frames) {
    if (api->keyboard_has_char && api->keyboard_has_char()) {
      char ch = api->keyboard_get_char();
      if (ch == '\t' || ch == 'v' || ch == 'V') {
        verbose_mode = !verbose_mode;
        if (!verbose_mode) {
          draw_boot_background(api, bg_img, sw, sh);
        }
      } else if (cfg.allow_skip) {
        if (dirty_bg && api->kfree)
          api->kfree(dirty_bg);
        if (bg_img && api->image_free)
          api->image_free(bg_img);
        return BOOTSCREEN_SKIPPED;
      }
    }

    // Ripristino leggero del solo rettangolo dirty (o full redraw in modalita'
    // verbosa / fallback)
    if (!verbose_mode && dirty_bg) {
      for (int32_t ry = 0; ry < dirty_h; ry++) {
        for (int32_t rx = 0; rx < dirty_w; rx++) {
          api->gfx_draw_pixel(dirty_x + rx, dirty_y + ry,
                              dirty_bg[ry * dirty_w + rx]);
        }
      }
    } else {
      draw_boot_background(api, bg_img, sw, sh);
    }

    int32_t t_q16 = (frame * NSM_ONE) / total_frames;
    int32_t e_q16 = (motion && motion->ease_q16)
                        ? motion->ease_q16(NSM_EASE_OUT_CUBIC, t_q16)
                        : t_q16;

    int step_idx = (frame * total_steps) / total_frames;
    if (step_idx >= total_steps)
      step_idx = total_steps - 1;

    if (cfg.show_progress) {
      // Track barra Liquid Glass (con fallback sicuro a gfx_fill_rounded_alpha
      // / gfx_fill_rect)
      if (ext && ext->gfx_fill_rounded_glass) {
        ext->gfx_fill_rounded_glass(bar_x, bar_y, bar_w, bar_h, 7, 0x0B192E,
                                    175);
      } else if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(bar_x, bar_y, bar_w, bar_h, 7, 0x0B192E,
                                    185);
      } else if (api->gfx_fill_rect) {
        api->gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x0B192E);
      }

      if (api->gfx_draw_rounded_rect) {
        api->gfx_draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, 7, 0x4E79B8);
      }

      // Riempimento progressivo fluido + riflesso scorrevole
      int32_t fill_w = (((bar_w - 4) * e_q16) >> 16);
      if (fill_w < 12)
        fill_w = 12;
      if (fill_w > bar_w - 4)
        fill_w = bar_w - 4;

      if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 5,
                                    0x2F7CF6, 235);
        // Riflesso superiore della barra
        if (fill_w > 16) {
          api->gfx_fill_alpha(bar_x + 6, bar_y + 3, fill_w - 8, 2, 0xFFFFFF,
                              120);
        }
      } else if (api->gfx_fill_rect) {
        api->gfx_fill_rect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 0x2F7CF6);
      }
    }

    // Messaggio di stato corrente
    const char *step_msg = g_boot_steps[step_idx];
    int32_t msg_x = ((int32_t)sw - (bs_strlen(step_msg) * 8)) / 2;
    int32_t msg_y = bar_y + bar_h + 14;
    if (api->gfx_draw_string) {
      api->gfx_draw_string(msg_x, msg_y, step_msg, 0xD2E1F5, 0, true);
    }

    // Pannello opzionale Verbose / Diagnostica (tasto TAB o V)
    if (verbose_mode) {
      int32_t vx = ((int32_t)sw - 580) / 2;
      int32_t vy = 22;
      if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(vx, vy, 580, 162, 12, 0x06101F, 232);
      } else if (api->gfx_fill_rect) {
        api->gfx_fill_rect(vx, vy, 580, 162, 0x06101F);
      }
      if (api->gfx_draw_rounded_rect) {
        api->gfx_draw_rounded_rect(vx, vy, 580, 162, 12, 0x38BDF8);
      }
      if (api->gfx_draw_string) {
        api->gfx_draw_string(vx + 16, vy + 12,
                             "NewOS V1 Boot Diagnostics (TAB/V per chiudere)",
                             0x7DD3FC, 0, true);
        for (int s = 0; s <= step_idx && s < total_steps; s++) {
          uint32_t col = (s == step_idx) ? 0xFFFFFF : 0x6EE7B7;
          api->gfx_draw_string(vx + 16, vy + 34 + s * 17, g_boot_steps[s], col,
                               0, true);
        }
      }
    }

    if (api->gfx_flip)
      api->gfx_flip();
    if (api->timer_sleep)
      api->timer_sleep(16);

    frame++;
  }

  if (dirty_bg && api->kfree)
    api->kfree(dirty_bg);
  if (bg_img && api->image_free)
    api->image_free(bg_img);

  if (api->serial_puts) {
    api->serial_puts("[BOOTSCR.MOD] Splash screen completato con successo.\n");
  }

  return BOOTSCREEN_OK;
}
