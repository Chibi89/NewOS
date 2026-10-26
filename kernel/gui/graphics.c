#include <arch/x86_64/io.h>
#include <drivers/ata.h>
#include <drivers/mouse.h>
#include <fs/vfs.h>
#include <gui/font.h>
#include <gui/graphics.h>
#include <gui/image.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <string.h>


static uint32_t *photo_wallpaper_buf = NULL;
static int loaded_photo_type = -1;
static uint32_t *blurred_buf = NULL; // Pre-blurred wallpaper per Liquid Glass

static uint32_t *lfb_front = NULL;
static uint32_t *lfb_back = NULL;
static size_t lfb_back_capacity = 0;
static uint32_t scr_width = 1024;
static uint32_t scr_height = 768;
static uint32_t scr_pitch_pixels = 1024;
static uint32_t scr_pitch_bytes = 4096;
static uint32_t scr_bpp = 32;

// Bitmap Cursore Mouse Windows 2000 (12x19)
// 0: trasparente, 1: nero (0x000000), 2: bianco (0xFFFFFF)
static const uint8_t cursor_bitmap[19][12] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0}, {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0}, {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0}, {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0}, {1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0, 0}, {1, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0}, {0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

void gfx_init(BootInfo *binfo) {
  lfb_front = (uint32_t *)binfo->framebuffer_base;
  scr_width = binfo->width;
  scr_height = binfo->height;
  scr_bpp = binfo->bpp;
  scr_pitch_bytes = binfo->pitch;
  scr_pitch_pixels = binfo->width;

  // Buffer dinamico fino a 1920x1080 (Full HD 32bpp = ~8 MB)
  size_t max_buf = 1920ULL * 1080ULL * 4ULL;
  lfb_back = (uint32_t *)kmalloc(max_buf);
  if (lfb_back) {
    lfb_back_capacity = max_buf;
    memset(lfb_back, 0,
           (size_t)scr_pitch_pixels * scr_height * sizeof(uint32_t));
  } else {
    size_t buffer_size =
        (size_t)scr_pitch_pixels * scr_height * sizeof(uint32_t);
    lfb_back = (uint32_t *)kmalloc(buffer_size);
    if (!lfb_back) {
      lfb_back = lfb_front;
      lfb_back_capacity = buffer_size;
    } else {
      lfb_back_capacity = buffer_size;
      memset(lfb_back, 0, buffer_size);
    }
  }
}

void gfx_flip(void) {
  if (!lfb_front || !lfb_back || lfb_front == lfb_back)
    return;

  if (scr_bpp == 32) {
    if (scr_pitch_bytes == scr_width * 4) {
      size_t qwords = ((size_t)scr_width * scr_height * 4) / sizeof(uint64_t);
      uint64_t *dst = (uint64_t *)lfb_front;
      uint64_t *src = (uint64_t *)lfb_back;
      for (size_t i = 0; i < qwords; i++) {
        dst[i] = src[i];
      }
    } else {
      uint8_t *dst_bytes = (uint8_t *)lfb_front;
      for (uint32_t y = 0; y < scr_height; y++) {
        memcpy(dst_bytes + (y * scr_pitch_bytes),
               lfb_back + (y * scr_pitch_pixels), scr_width * 4);
      }
    }
  } else if (scr_bpp == 24) {
    uint8_t *dst_bytes = (uint8_t *)lfb_front;
    for (uint32_t y = 0; y < scr_height; y++) {
      uint32_t *src_row = lfb_back + (y * scr_pitch_pixels);
      uint8_t *dst_row = dst_bytes + (y * scr_pitch_bytes);
      for (uint32_t x = 0; x < scr_width; x++) {
        uint32_t c = src_row[x];
        dst_row[x * 3 + 0] = (uint8_t)(c & 0xFF);         // Blue
        dst_row[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);  // Green
        dst_row[x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF); // Red
      }
    }
  }
}

void gfx_clear(uint32_t color) {
  size_t total = (size_t)scr_pitch_pixels * scr_height;
  for (size_t i = 0; i < total; i++) {
    lfb_back[i] = color;
  }
}

void gfx_draw_pixel(int32_t x, int32_t y, uint32_t color) {
  if (x < 0 || x >= (int32_t)scr_width || y < 0 || y >= (int32_t)scr_height)
    return;
  lfb_back[y * scr_pitch_pixels + x] = color;
}

uint32_t gfx_get_pixel(int32_t x, int32_t y) {
  if (x < 0 || x >= (int32_t)scr_width || y < 0 || y >= (int32_t)scr_height)
    return 0;
  return lfb_back[y * scr_pitch_pixels + x];
}

void gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
  if (x >= (int32_t)scr_width || y >= (int32_t)scr_height)
    return;
  if (x + w <= 0 || y + h <= 0)
    return;

  int32_t x1 = x < 0 ? 0 : x;
  int32_t y1 = y < 0 ? 0 : y;
  int32_t x2 = (x + w > (int32_t)scr_width) ? (int32_t)scr_width : (x + w);
  int32_t y2 = (y + h > (int32_t)scr_height) ? (int32_t)scr_height : (y + h);

  for (int32_t cy = y1; cy < y2; cy++) {
    uint32_t *row = &lfb_back[cy * scr_pitch_pixels + x1];
    int32_t span = x2 - x1;
    for (int32_t cx = 0; cx < span; cx++) {
      row[cx] = color;
    }
  }
}

void gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
  for (int32_t cx = x; cx < x + w; cx++) {
    gfx_draw_pixel(cx, y, color);
    gfx_draw_pixel(cx, y + h - 1, color);
  }
  for (int32_t cy = y; cy < y + h; cy++) {
    gfx_draw_pixel(x, cy, color);
    gfx_draw_pixel(x + w - 1, cy, color);
  }
}

void gfx_draw_bevel_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                         bool sunken) {
  uint32_t c_topleft_outer = sunken ? COLOR_BTNDKSHADOW : COLOR_BTNHIGHLIGHT;
  uint32_t c_topleft_inner = sunken ? COLOR_BTNSHADOW : COLOR_BTNFACE;
  uint32_t c_bottomright_outer =
      sunken ? COLOR_BTNHIGHLIGHT : COLOR_BTNDKSHADOW;
  uint32_t c_bottomright_inner = sunken ? COLOR_BTNFACE : COLOR_BTNSHADOW;

  // Bordo esterno superiore e sinistro
  for (int32_t i = 0; i < w; i++)
    gfx_draw_pixel(x + i, y, c_topleft_outer);
  for (int32_t i = 0; i < h; i++)
    gfx_draw_pixel(x, y + i, c_topleft_outer);

  // Bordo interno superiore e sinistro
  for (int32_t i = 1; i < w - 1; i++)
    gfx_draw_pixel(x + i, y + 1, c_topleft_inner);
  for (int32_t i = 1; i < h - 1; i++)
    gfx_draw_pixel(x + 1, y + i, c_topleft_inner);

  // Bordo interno inferiore e destro
  for (int32_t i = 1; i < w - 1; i++)
    gfx_draw_pixel(x + i, y + h - 2, c_bottomright_inner);
  for (int32_t i = 1; i < h - 1; i++)
    gfx_draw_pixel(x + w - 2, y + i, c_bottomright_inner);

  // Bordo esterno inferiore e destro
  for (int32_t i = 0; i < w; i++)
    gfx_draw_pixel(x + i, y + h - 1, c_bottomright_outer);
  for (int32_t i = 0; i < h; i++)
    gfx_draw_pixel(x + w - 1, y + i, c_bottomright_outer);
}

void gfx_draw_gradient_h(int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t c_start, uint32_t c_end) {
  if (w <= 0 || h <= 0)
    return;

  int32_t r1 = (c_start >> 16) & 0xFF, g1 = (c_start >> 8) & 0xFF,
          b1 = c_start & 0xFF;
  int32_t r2 = (c_end >> 16) & 0xFF, g2 = (c_end >> 8) & 0xFF,
          b2 = c_end & 0xFF;

  for (int32_t cx = 0; cx < w; cx++) {
    int32_t r = r1 + ((r2 - r1) * cx) / w;
    int32_t g = g1 + ((g2 - g1) * cx) / w;
    int32_t b = b1 + ((b2 - b1) * cx) / w;
    uint32_t color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);

    int32_t px = x + cx;
    if (px >= 0 && px < (int32_t)scr_width) {
      for (int32_t cy = 0; cy < h; cy++) {
        int32_t py = y + cy;
        if (py >= 0 && py < (int32_t)scr_height) {
          lfb_back[py * scr_pitch_pixels + px] = color;
        }
      }
    }
  }
}

void gfx_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg,
                   bool transparent) {
  uint8_t uc = (uint8_t)c;
  if (uc >= 128)
    uc = '?';

  for (int cy = 0; cy < FONT_HEIGHT; cy++) {
    uint8_t line = font8x16_basic[uc][cy];
    for (int cx = 0; cx < FONT_WIDTH; cx++) {
      if (line & (0x80 >> cx)) {
        gfx_draw_pixel(x + cx, y + cy, fg);
      } else if (!transparent) {
        gfx_draw_pixel(x + cx, y + cy, bg);
      }
    }
  }
}

void gfx_draw_string(int32_t x, int32_t y, const char *str, uint32_t fg,
                     uint32_t bg, bool transparent) {
  if (!str)
    return;
  int32_t cx = x;
  while (*str) {
    if (*str == '\n') {
      y += FONT_HEIGHT;
      cx = x;
    } else {
      gfx_draw_char(cx, y, *str, fg, bg, transparent);
      cx += FONT_WIDTH;
    }
    str++;
  }
}

void gfx_draw_cursor(int32_t x, int32_t y) {
  for (int cy = 0; cy < 19; cy++) {
    for (int cx = 0; cx < 12; cx++) {
      uint8_t pixel = cursor_bitmap[cy][cx];
      if (pixel == 1) {
        gfx_draw_pixel(x + cx, y + cy, 0x000000); // Bordo nero
      } else if (pixel == 2) {
        gfx_draw_pixel(x + cx, y + cy, 0xFFFFFF); // Interno bianco
      }
    }
  }
}

void gfx_draw_windows_logo(int32_t x, int32_t y) {
  // 4 riquadri iconici di Windows 2000 (Rosso, Verde, Blu, Giallo)
  gfx_fill_rect(x, y, 7, 7, 0xEE3224);         // Rosso (Top Left)
  gfx_fill_rect(x + 8, y, 7, 7, 0x7FB900);     // Verde (Top Right)
  gfx_fill_rect(x, y + 8, 7, 7, 0x00A4EF);     // Blu (Bottom Left)
  gfx_fill_rect(x + 8, y + 8, 7, 7, 0xFFB900); // Giallo (Bottom Right)
}

void gfx_draw_icon_computer(int32_t x, int32_t y) {
  // Monitor retro beveled con schermo blu
  gfx_fill_rect(x + 2, y + 2, 28, 20, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 2, y + 2, 28, 20, false);
  gfx_fill_rect(x + 6, y + 5, 20, 14, 0x008080);
  gfx_draw_bevel_rect(x + 6, y + 5, 20, 14, true);

  // Supporto e case
  gfx_fill_rect(x + 12, y + 22, 8, 4, COLOR_BTNFACE);
  gfx_fill_rect(x + 8, y + 26, 16, 4, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 8, y + 26, 16, 4, false);
}

void gfx_draw_icon_notepad(int32_t x, int32_t y) {
  // Blocco note bianco con righe e matita
  gfx_fill_rect(x + 4, y + 2, 24, 28, 0xFFFFFF);
  gfx_draw_bevel_rect(x + 4, y + 2, 24, 28, true);
  // Righe
  for (int i = 7; i < 26; i += 4) {
    gfx_fill_rect(x + 7, y + i, 18, 1, 0x808080);
  }
}

void gfx_draw_icon_calc(int32_t x, int32_t y) {
  // Calcolatrice classica
  gfx_fill_rect(x + 4, y + 2, 24, 28, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x + 4, y + 2, 24, 28, false);
  // Display LCD verde/chiaro
  gfx_fill_rect(x + 7, y + 5, 18, 6, 0xD4E6C4);
  gfx_draw_bevel_rect(x + 7, y + 5, 18, 6, true);
  // Pulsanti calcolatrice
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      gfx_fill_rect(x + 7 + c * 6, y + 14 + r * 5, 5, 4, COLOR_BTNFACE);
      gfx_draw_bevel_rect(x + 7 + c * 6, y + 14 + r * 5, 5, 4, false);
    }
  }
}

void gfx_draw_icon_cmd(int32_t x, int32_t y) {
  // Icona finestra Prompt dei Comandi nera con bordi beveled
  gfx_fill_rect(x + 2, y + 4, 28, 22, 0x000000);
  gfx_draw_bevel_rect(x + 2, y + 4, 28, 22, true);
  gfx_fill_rect(x + 3, y + 5, 26, 4, 0xFFFFFF);
  // Prompt ">_"
  gfx_draw_string(x + 5, y + 10, ">_", 0xFFFFFF, 0, true);
}

void gfx_draw_gradient_v(int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t c_start, uint32_t c_end) {
  if (w <= 0 || h <= 0)
    return;

  int32_t r1 = (c_start >> 16) & 0xFF, g1 = (c_start >> 8) & 0xFF,
          b1 = c_start & 0xFF;
  int32_t r2 = (c_end >> 16) & 0xFF, g2 = (c_end >> 8) & 0xFF,
          b2 = c_end & 0xFF;

  for (int32_t cy = 0; cy < h; cy++) {
    int32_t r = r1 + ((r2 - r1) * cy) / h;
    int32_t g = g1 + ((g2 - g1) * cy) / h;
    int32_t b = b1 + ((b2 - b1) * cy) / h;
    uint32_t color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);

    int32_t py = y + cy;
    if (py >= 0 && py < (int32_t)scr_height) {
      uint32_t *row = &lfb_back[py * scr_pitch_pixels];
      int32_t start_x = (x < 0) ? 0 : x;
      int32_t end_x =
          (x + w > (int32_t)scr_width) ? (int32_t)scr_width : (x + w);
      for (int32_t px = start_x; px < end_x; px++) {
        row[px] = color;
      }
    }
  }
}

void gfx_draw_background(int bg_type) {
  int32_t h = scr_height - 28; // Esclude la taskbar
  int32_t w = scr_width;

  switch (bg_type) {
  case BG_BLUE_NAVY:
    gfx_fill_rect(0, 0, w, h, 0x000080);
    break;

  case BG_SLATE_GRAY:
    gfx_fill_rect(0, 0, w, h, 0x3A4B53);
    break;

  case BG_BLISS_XP: {
    int32_t sky_h = (h * 65) / 100;
    int32_t hill_h = h - sky_h;
    gfx_draw_gradient_v(0, 0, w, sky_h, 0x1B58B8, 0x82B2F4);
    gfx_draw_gradient_v(0, sky_h, w, hill_h, 0x489C32, 0x245C1A);
    break;
  }

  case BG_BRICKS: {
    uint32_t c_brick = 0x8A2A18;
    uint32_t c_mortar = 0xC4C0B8;
    gfx_fill_rect(0, 0, w, h, c_brick);
    for (int32_t y = 0; y < h; y += 12) {
      gfx_fill_rect(0, y, w, 2, c_mortar);
      int offset = ((y / 12) % 2) * 16;
      for (int32_t x = offset; x < w; x += 32) {
        gfx_fill_rect(x, y, 2, 12, c_mortar);
      }
    }
    break;
  }

  case BG_MATRIX: {
    gfx_fill_rect(0, 0, w, h, 0x060E08);
    for (int32_t y = 0; y < h; y += 32) {
      gfx_fill_rect(0, y, w, 1, 0x0D3814);
    }
    for (int32_t x = 0; x < w; x += 32) {
      gfx_fill_rect(x, 0, 1, h, 0x0D3814);
    }
    for (int32_t y = 16; y < h; y += 32) {
      for (int32_t x = 16; x < w; x += 32) {
        gfx_fill_rect(x - 2, y - 2, 5, 5, 0x1FC044);
      }
    }
    break;
  }

  case BG_PHOTO_HERO:
  case BG_PHOTO_FLOW: {
    if (loaded_photo_type != bg_type) {
      const char *fn = (bg_type == BG_PHOTO_HERO) ? "HERO" : "FLOW";
      char path_buf[128];
      image_t *img = NULL;

      mini_snprintf(path_buf, sizeof(path_buf),
                    "C:\\NewOS\\Resources\\Wallpapers\\%s.BMP", fn);
      img = image_load_from_file(path_buf);
      if (!img) {
        mini_snprintf(path_buf, sizeof(path_buf),
                      "C:\\NewOS\\Resource\\wallpapers\\%s.BMP", fn);
        img = image_load_from_file(path_buf);
      }
      if (!img) {
        mini_snprintf(path_buf, sizeof(path_buf), "/SYSTEM/WALLPAPERS/%s.BMP",
                      fn);
        img = image_load_from_file(path_buf);
      }
      if (!img) {
        mini_snprintf(path_buf, sizeof(path_buf), "/SYSTEM/WALLPAPERS/%s.JPG",
                      fn);
        img = image_load_from_file(path_buf);
      }
      if (img) {
        if (!photo_wallpaper_buf) {
          photo_wallpaper_buf = (uint32_t *)kmalloc(1024 * 768 * 4);
        }
        if (photo_wallpaper_buf) {
          if (img->width == 1024 && img->height == 768) {
            memcpy(photo_wallpaper_buf, img->pixels, 1024 * 768 * 4);
          } else {
            image_t *res = image_resize(img, 1024, 768);
            if (res) {
              memcpy(photo_wallpaper_buf, res->pixels, 1024 * 768 * 4);
              image_free(res);
            }
          }
          loaded_photo_type = bg_type;
        }
        image_free(img);
      }
    }
    if (photo_wallpaper_buf && loaded_photo_type == bg_type) {
      size_t qwords = ((size_t)1024 * 768 * 4) / sizeof(uint64_t);
      uint64_t *dst = (uint64_t *)lfb_back;
      uint64_t *src = (uint64_t *)photo_wallpaper_buf;
      for (size_t i = 0; i < qwords; i++) {
        dst[i] = src[i];
      }
    } else {
      if (bg_type == BG_PHOTO_FLOW) {
        // Sfondo procedurale Windows 11 Flow / Bloom (gradiente ad alta
        // fedeltà)
        int32_t h1 = (h * 58) / 100;
        gfx_draw_gradient_v(0, 0, w, h1, 0x180A30, 0x822468);
        gfx_draw_gradient_v(0, h1, w, h - h1, 0xD45B4B, 0x1A0824);
        gfx_fill_alpha(w / 4, h / 3, w / 2, h / 3, 0xFF6584, 35);
        gfx_fill_alpha(w / 3, (h * 2) / 5, w / 3, h / 4, 0x8A4BF3, 45);
      } else {
        // BG_PHOTO_HERO
        gfx_draw_gradient_v(0, 0, w, h, 0x001B3A, 0x003366);
      }
    }
    break;
  }

  case BG_CUSTOM: {
    if (photo_wallpaper_buf) {
      size_t qwords = ((size_t)1024 * 768 * 4) / sizeof(uint64_t);
      uint64_t *dst = (uint64_t *)lfb_back;
      uint64_t *src = (uint64_t *)photo_wallpaper_buf;
      for (size_t i = 0; i < qwords; i++) {
        dst[i] = src[i];
      }
    } else {
      gfx_fill_rect(0, 0, w, h, COLOR_DESKTOP);
    }
    break;
  }

  case BG_TEAL_2000:
  default:
    gfx_fill_rect(0, 0, w, h, COLOR_DESKTOP);
    break;
  }
}

void gfx_draw_icon_folder(int32_t x, int32_t y) {
  // Linguetta superiore sinistra
  gfx_fill_rect(x + 4, y + 4, 11, 4, 0xCCA626);
  gfx_draw_bevel_rect(x + 4, y + 4, 11, 4, false);
  // Corpo cartella posteriore
  gfx_fill_rect(x + 4, y + 7, 24, 18, 0xE5BD3B);
  gfx_draw_bevel_rect(x + 4, y + 7, 24, 18, true);
  // Lembo anteriore in rilievo
  gfx_fill_rect(x + 2, y + 11, 28, 16, 0xFFD84A);
  gfx_draw_bevel_rect(x + 2, y + 11, 28, 16, false);
}

void gfx_draw_icon_doc(int32_t x, int32_t y) {
  // Foglio bianco con bordo
  gfx_fill_rect(x + 5, y + 3, 22, 26, 0xFFFFFF);
  gfx_draw_bevel_rect(x + 5, y + 3, 22, 26, false);
  // Angolo superiore piegato
  gfx_fill_rect(x + 21, y + 3, 6, 6, COLOR_BTNFACE);
  gfx_draw_pixel(x + 20, y + 4, 0x808080);
  gfx_draw_pixel(x + 21, y + 5, 0x808080);
  gfx_draw_pixel(x + 22, y + 6, 0x808080);
  // Righe testo
  gfx_fill_rect(x + 9, y + 10, 14, 2, 0x3060A0);
  gfx_fill_rect(x + 9, y + 14, 14, 2, 0x3060A0);
  gfx_fill_rect(x + 9, y + 18, 14, 2, 0x3060A0);
  gfx_fill_rect(x + 9, y + 22, 8, 2, 0x808080);
}

void gfx_draw_mini_monitor(int32_t x, int32_t y, int bg_type) {
  // Telaio monitor CRT beige (120x78)
  gfx_fill_rect(x, y, 120, 78, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x, y, 120, 78, false);

  // Bezel scuro dello schermo
  int32_t sx = x + 8;
  int32_t sy = y + 7;
  int32_t sw = 104;
  int32_t sh = 62;
  gfx_fill_rect(sx, sy, sw, sh, 0x101010);
  gfx_draw_bevel_rect(sx, sy, sw, sh, true);

  // Contenuto schermo anteprima
  int32_t px = sx + 2;
  int32_t py = sy + 2;
  int32_t pw = sw - 4;
  int32_t ph = sh - 4;

  switch (bg_type) {
  case BG_BLUE_NAVY:
    gfx_fill_rect(px, py, pw, ph, 0x000080);
    break;
  case BG_SLATE_GRAY:
    gfx_fill_rect(px, py, pw, ph, 0x3A4B53);
    break;
  case BG_BLISS_XP: {
    int32_t sky_h = (ph * 65) / 100;
    gfx_draw_gradient_v(px, py, pw, sky_h, 0x1B58B8, 0x82B2F4);
    gfx_draw_gradient_v(px, py + sky_h, pw, ph - sky_h, 0x489C32, 0x245C1A);
    break;
  }
  case BG_BRICKS: {
    gfx_fill_rect(px, py, pw, ph, 0x8A2A18);
    for (int32_t by = py; by < py + ph; by += 4) {
      gfx_fill_rect(px, by, pw, 1, 0xC4C0B8);
    }
    break;
  }
  case BG_MATRIX: {
    gfx_fill_rect(px, py, pw, ph, 0x060E08);
    for (int32_t by = py; by < py + ph; by += 8) {
      gfx_fill_rect(px, by, pw, 1, 0x0D3814);
    }
    for (int32_t bx = px; bx < px + pw; bx += 10) {
      gfx_fill_rect(bx, py, 1, ph, 0x0D3814);
    }
    break;
  }
  case BG_PHOTO_HERO:
    gfx_draw_gradient_v(px, py, pw, ph, 0x001B3A, 0x003366);
    gfx_fill_rect(px + pw / 4, py + ph / 4, pw / 2, ph / 2, 0x00A4EF);
    gfx_draw_windows_logo(px + pw / 2 - 8, py + ph / 2 - 8);
    break;
  case BG_PHOTO_FLOW:
    gfx_draw_gradient_v(px, py, pw, ph / 2, 0x2A1B4E, 0x7E3A6E);
    gfx_draw_gradient_v(px, py + ph / 2, pw, ph / 2, 0xC45B4A, 0x301B2E);
    break;
  case BG_CUSTOM: {
    if (photo_wallpaper_buf) {
      for (int32_t my = 0; my < ph; my++) {
        int32_t sy = (my * 768) / ph;
        for (int32_t mx = 0; mx < pw; mx++) {
          int32_t sx = (mx * 1024) / pw;
          gfx_draw_pixel(px + mx, py + my, photo_wallpaper_buf[sy * 1024 + sx]);
        }
      }
    } else {
      gfx_fill_rect(px, py, pw, ph, 0x001B3A);
    }
    break;
  }
  case BG_TEAL_2000:
  default:
    gfx_fill_rect(px, py, pw, ph, COLOR_DESKTOP);
    break;
  }

  // Mini finestra dimostrativa al centro dello schermo
  int32_t mwx = px + 18;
  int32_t mwy = py + 10;
  int32_t mww = 64;
  int32_t mwh = 36;
  gfx_fill_rect(mwx, mwy, mww, mwh, COLOR_BTNFACE);
  gfx_draw_bevel_rect(mwx, mwy, mww, mwh, false);
  gfx_draw_gradient_h(mwx + 2, mwy + 2, mww - 4, 6, COLOR_ACTIVECAPTION_LEFT,
                      COLOR_ACTIVECAPTION_RIGHT);
  gfx_fill_rect(mwx + 3, mwy + 9, mww - 6, mwh - 12, 0xFFFFFF);
  gfx_draw_bevel_rect(mwx + 3, mwy + 9, mww - 6, mwh - 12, true);

  // Supporto e base monitor
  int32_t neck_x = x + 48;
  int32_t neck_y = y + 78;
  gfx_fill_rect(neck_x, neck_y, 24, 8, COLOR_BTNFACE);
  gfx_draw_bevel_rect(neck_x, neck_y, 24, 8, false);

  int32_t base_x = x + 38;
  int32_t base_y = y + 86;
  gfx_fill_rect(base_x, base_y, 44, 8, COLOR_BTNFACE);
  gfx_draw_bevel_rect(base_x, base_y, 44, 8, false);

  // Spia led verde
  gfx_fill_rect(x + 108, y + 72, 4, 3, 0x00FF00);
}

uint32_t gfx_get_width(void) { return scr_width; }
uint32_t gfx_get_height(void) { return scr_height; }
uint32_t gfx_get_pitch_bytes(void) { return scr_pitch_bytes; }

bool gfx_set_resolution(uint32_t width, uint32_t height) {
  if (!lfb_front || width == 0 || height == 0)
    return false;

  uint32_t old_w = scr_width;
  uint32_t old_h = scr_height;
  size_t required_bytes = (size_t)width * height * 4;

  if (required_bytes > lfb_back_capacity) {
    uint32_t *new_buf = (uint32_t *)kmalloc(required_bytes);
    if (!new_buf)
      return false;
    if (lfb_back && lfb_back != lfb_front) {
      kfree(lfb_back);
    }
    lfb_back = new_buf;
    lfb_back_capacity = required_bytes;
  }

  scr_width = width;
  scr_height = height;
  scr_pitch_pixels = width;
  scr_pitch_bytes = width * 4;

  // Reset buffer sfondi fotografici
  if (photo_wallpaper_buf) {
    kfree(photo_wallpaper_buf);
    photo_wallpaper_buf = NULL;
    loaded_photo_type = -1;
  }

  // Reset Liquid Glass blur
  if (blurred_buf) {
    kfree(blurred_buf);
    blurred_buf = NULL;
  }

  // Limiti coordinate mouse
  mouse_set_bounds(width, height);

  // Notifica Window Manager per riallineare finestre e menu
  wm_on_resolution_changed(old_w, old_h, width, height);

  // Ridisegna immediatamente il desktop alla nuova risoluzione
  gfx_draw_background(wm_get_background_type());
  gfx_flip();

  return true;
}

// ============================================================
//  Liquid Glass primitives
// ============================================================

// Macro helper: blend un canale a e b con alpha 0-255
#define BLEND_CH(dst, src, a)                                                  \
  (((uint32_t)(dst) * (255u - (a)) + (uint32_t)(src) * (a)) / 255u)

static inline uint32_t blend_color(uint32_t dst, uint32_t src, uint8_t a) {
  uint8_t r = (uint8_t)BLEND_CH((dst >> 16) & 0xFF, (src >> 16) & 0xFF, a);
  uint8_t g = (uint8_t)BLEND_CH((dst >> 8) & 0xFF, (src >> 8) & 0xFF, a);
  uint8_t b = (uint8_t)BLEND_CH((dst) & 0xFF, (src) & 0xFF, a);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Verifica se (px, py) e' dentro il rounded-rect con angoli di raggio r
static inline bool inside_rounded(int32_t px, int32_t py, int32_t x, int32_t y,
                                  int32_t w, int32_t h, int32_t r) {
  if (r <= 0)
    return true;
  // Angolo top-left
  if (px < x + r && py < y + r) {
    int32_t dx = px - (x + r), dy = py - (y + r);
    return (dx * dx + dy * dy) <= r * r;
  }
  // Angolo top-right
  if (px >= x + w - r && py < y + r) {
    int32_t dx = px - (x + w - r), dy = py - (y + r);
    return (dx * dx + dy * dy) <= r * r;
  }
  // Angolo bottom-left
  if (px < x + r && py >= y + h - r) {
    int32_t dx = px - (x + r), dy = py - (y + h - r);
    return (dx * dx + dy * dy) <= r * r;
  }
  // Angolo bottom-right
  if (px >= x + w - r && py >= y + h - r) {
    int32_t dx = px - (x + w - r), dy = py - (y + h - r);
    return (dx * dx + dy * dy) <= r * r;
  }
  return true;
}

// ---- gfx_fill_alpha ----------------------------------------
void gfx_fill_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color,
                    uint8_t alpha) {
  int32_t x1 = x < 0 ? 0 : x;
  int32_t y1 = y < 0 ? 0 : y;
  int32_t x2 = (x + w > (int32_t)scr_width) ? (int32_t)scr_width : x + w;
  int32_t y2 = (y + h > (int32_t)scr_height) ? (int32_t)scr_height : y + h;
  for (int32_t py = y1; py < y2; py++) {
    uint32_t *row = &lfb_back[py * scr_pitch_pixels];
    for (int32_t px = x1; px < x2; px++) {
      row[px] = blend_color(row[px], color, alpha);
    }
  }
}

// ---- gfx_fill_rounded_rect ----------------------------------
void gfx_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t radius, uint32_t color) {
  int32_t x1 = x < 0 ? 0 : x;
  int32_t y1 = y < 0 ? 0 : y;
  int32_t x2 = (x + w > (int32_t)scr_width) ? (int32_t)scr_width : x + w;
  int32_t y2 = (y + h > (int32_t)scr_height) ? (int32_t)scr_height : y + h;
  for (int32_t py = y1; py < y2; py++) {
    uint32_t *row = &lfb_back[py * scr_pitch_pixels];
    for (int32_t px = x1; px < x2; px++) {
      if (inside_rounded(px, py, x, y, w, h, radius))
        row[px] = color;
    }
  }
}

// ---- gfx_draw_rounded_rect ---------------------------------
// Disegna il bordo perimetrale continuo di 1px che segue fedelmente la curvatura degli angoli
void gfx_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t radius, uint32_t color) {
  if (w <= 0 || h <= 0) return;
  if (radius <= 0) {
    gfx_draw_rect(x, y, w, h, color);
    return;
  }
  if (radius * 2 > w) radius = w / 2;
  if (radius * 2 > h) radius = h / 2;

  int32_t r = radius;

  // 1. Tratti rettilinei orizzontali (superiore e inferiore)
  for (int32_t px = x + r; px < x + w - r; px++) {
    gfx_draw_pixel(px, y, color);
    gfx_draw_pixel(px, y + h - 1, color);
  }

  // 2. Tratti rettilinei verticali (sinistro e destro)
  for (int32_t py = y + r; py < y + h - r; py++) {
    gfx_draw_pixel(x, py, color);
    gfx_draw_pixel(x + w - 1, py, color);
  }

  // 3. I 4 angoli arrotondati che seguono la sagoma esatta di inside_rounded
  // Angolo Top-Left: [x, x + r - 1] x [y, y + r - 1]
  for (int32_t py = y; py < y + r; py++) {
    for (int32_t px = x; px < x + r; px++) {
      if (inside_rounded(px, py, x, y, w, h, r)) {
        if (px == x || py == y ||
            !inside_rounded(px - 1, py, x, y, w, h, r) ||
            !inside_rounded(px, py - 1, x, y, w, h, r)) {
          gfx_draw_pixel(px, py, color);
        }
      }
    }
  }

  // Angolo Top-Right: [x + w - r, x + w - 1] x [y, y + r - 1]
  for (int32_t py = y; py < y + r; py++) {
    for (int32_t px = x + w - r; px < x + w; px++) {
      if (inside_rounded(px, py, x, y, w, h, r)) {
        if (px == x + w - 1 || py == y ||
            !inside_rounded(px + 1, py, x, y, w, h, r) ||
            !inside_rounded(px, py - 1, x, y, w, h, r)) {
          gfx_draw_pixel(px, py, color);
        }
      }
    }
  }

  // Angolo Bottom-Left: [x, x + r - 1] x [y + h - r, y + h - 1]
  for (int32_t py = y + h - r; py < y + h; py++) {
    for (int32_t px = x; px < x + r; px++) {
      if (inside_rounded(px, py, x, y, w, h, r)) {
        if (px == x || py == y + h - 1 ||
            !inside_rounded(px - 1, py, x, y, w, h, r) ||
            !inside_rounded(px, py + 1, x, y, w, h, r)) {
          gfx_draw_pixel(px, py, color);
        }
      }
    }
  }

  // Angolo Bottom-Right: [x + w - r, x + w - 1] x [y + h - r, y + h - 1]
  for (int32_t py = y + h - r; py < y + h; py++) {
    for (int32_t px = x + w - r; px < x + w; px++) {
      if (inside_rounded(px, py, x, y, w, h, r)) {
        if (px == x + w - 1 || py == y + h - 1 ||
            !inside_rounded(px + 1, py, x, y, w, h, r) ||
            !inside_rounded(px, py + 1, x, y, w, h, r)) {
          gfx_draw_pixel(px, py, color);
        }
      }
    }
  }
}

// ---- gfx_fill_rounded_alpha ---------------------------------
// Campiona blurred_buf (o back-buffer se non disponibile) e blenda il tint
void gfx_fill_rounded_alpha(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, uint32_t tint, uint8_t alpha) {
  int32_t x1 = x < 0 ? 0 : x;
  int32_t y1 = y < 0 ? 0 : y;
  int32_t x2 = (x + w > (int32_t)scr_width) ? (int32_t)scr_width : x + w;
  int32_t y2 = (y + h > (int32_t)scr_height) ? (int32_t)scr_height : y + h;
  for (int32_t py = y1; py < y2; py++) {
    uint32_t *row = &lfb_back[py * scr_pitch_pixels];
    for (int32_t px = x1; px < x2; px++) {
      if (!inside_rounded(px, py, x, y, w, h, radius))
        continue;
      // Base: pixel sfocato (o quello del back-buffer come fallback)
      uint32_t base =
          blurred_buf ? blurred_buf[py * scr_pitch_pixels + px] : row[px];
      row[px] = blend_color(base, tint, alpha);
    }
  }
}

// ---- gfx_draw_specular_rim ----------------------------------
// Striscia speculare luminosa nei 3 pixel superiori del pannello glass
void gfx_draw_specular_rim(int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t radius) {
  // 3 righe con opacita' decrescente: 180, 90, 35
  static const uint8_t alphas[3] = {180, 90, 35};
  for (int row = 0; row < 3 && row < h; row++) {
    int32_t py = y + row;
    if (py < 0 || py >= (int32_t)scr_height)
      continue;
    uint32_t *scanline = &lfb_back[py * scr_pitch_pixels];
    for (int32_t px = x; px < x + w; px++) {
      if (px < 0 || px >= (int32_t)scr_width)
        continue;
      if (!inside_rounded(px, py, x, y, w, h, radius))
        continue;
      scanline[px] = blend_color(scanline[px], LG_SPECULAR_COLOR, alphas[row]);
    }
  }
  // Ultima riga: bordo inferiore sottilissimo semi-trasparente
  int32_t py_bot = y + h - 1;
  if (py_bot >= 0 && py_bot < (int32_t)scr_height) {
    uint32_t *scanline = &lfb_back[py_bot * scr_pitch_pixels];
    for (int32_t px = x; px < x + w; px++) {
      if (px < 0 || px >= (int32_t)scr_width)
        continue;
      if (!inside_rounded(px, py_bot, x, y, w, h, radius))
        continue;
      scanline[px] =
          blend_color(scanline[px], LG_BORDER_COLOR, LG_BORDER_ALPHA);
    }
  }
}

// ---- gfx_draw_drop_shadow -----------------------------------
void gfx_draw_drop_shadow(int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t spread) {
  // 3 layer progressivamente piu' leggeri
  static const struct {
    int32_t off;
    uint8_t a;
  } layers[] = {{3, 75}, {5, 45}, {8, 20}};
  for (int li = 0; li < 3; li++) {
    int32_t sx = x + layers[li].off;
    int32_t sy = y + layers[li].off;
    int32_t x1 = sx < 0 ? 0 : sx;
    int32_t y1 = sy < 0 ? 0 : sy;
    int32_t x2 = (sx + w + spread > (int32_t)scr_width) ? (int32_t)scr_width
                                                        : sx + w + spread;
    int32_t y2 = (sy + h + spread > (int32_t)scr_height) ? (int32_t)scr_height
                                                         : sy + h + spread;
    for (int32_t py = y1; py < y2; py++) {
      uint32_t *row = &lfb_back[py * scr_pitch_pixels];
      for (int32_t px = x1; px < x2; px++) {
        row[px] = blend_color(row[px], LG_SHADOW_COLOR, layers[li].a);
      }
    }
  }
}

// ---- gfx_precompute_blur ------------------------------------
// Separable box-blur 5x5 sul photo_wallpaper_buf -> blurred_buf.
// Se non c'e' foto, copia semplicemente il back-buffer attuale.
void gfx_precompute_blur(void) {
  if (!blurred_buf) {
    blurred_buf =
        (uint32_t *)kmalloc((size_t)scr_width * scr_height * sizeof(uint32_t));
    if (!blurred_buf)
      return;
  }

  uint32_t *src = photo_wallpaper_buf ? photo_wallpaper_buf : lfb_back;
  uint32_t W = scr_width, H = scr_height;
  // tmp buffer (usa blurred_buf come scratch nella prima passata)
  uint32_t *tmp = blurred_buf;

  // --- Pass 1: blur orizzontale (radius=5) ---
  for (uint32_t row = 0; row < H; row++) {
    for (uint32_t col = 0; col < W; col++) {
      uint32_t r = 0, g = 0, b = 0, cnt = 0;
      for (int32_t k = -5; k <= 5; k++) {
        int32_t cc = (int32_t)col + k;
        if (cc < 0)
          cc = 0;
        if (cc >= (int32_t)W)
          cc = W - 1;
        uint32_t px = src[row * W + cc];
        r += (px >> 16) & 0xFF;
        g += (px >> 8) & 0xFF;
        b += (px) & 0xFF;
        cnt++;
      }
      tmp[row * W + col] = ((r / cnt) << 16) | ((g / cnt) << 8) | (b / cnt);
    }
  }

  // --- Pass 2: blur verticale sulla stessa blurred_buf in-place (safe perche'
  // andiamo riga per riga) --- Usiamo kmalloc di una riga temporanea
  uint32_t *row_tmp = (uint32_t *)kmalloc(W * sizeof(uint32_t));
  if (!row_tmp)
    return;

  for (uint32_t col = 0; col < W; col++) {
    for (uint32_t row = 0; row < H; row++) {
      uint32_t r = 0, g = 0, b = 0, cnt = 0;
      for (int32_t k = -5; k <= 5; k++) {
        int32_t rr = (int32_t)row + k;
        if (rr < 0)
          rr = 0;
        if (rr >= (int32_t)H)
          rr = H - 1;
        uint32_t px = tmp[rr * W + col];
        r += (px >> 16) & 0xFF;
        g += (px >> 8) & 0xFF;
        b += (px) & 0xFF;
        cnt++;
      }
      row_tmp[row] = ((r / cnt) << 16) | ((g / cnt) << 8) | (b / cnt);
    }
    for (uint32_t row = 0; row < H; row++) {
      blurred_buf[row * W + col] = row_tmp[row];
    }
  }
  kfree(row_tmp);
}

// ---- gfx_get_blurred_pixel ----------------------------------
uint32_t gfx_get_blurred_pixel(int32_t x, int32_t y) {
  if (!blurred_buf)
    return 0;
  if (x < 0 || x >= (int32_t)scr_width || y < 0 || y >= (int32_t)scr_height)
    return 0;
  return blurred_buf[y * scr_pitch_pixels + x];
}

void gfx_set_custom_wallpaper(const uint32_t *pixels, int32_t width,
                              int32_t height) {
  if (!pixels || width <= 0 || height <= 0)
    return;
  if (!photo_wallpaper_buf) {
    photo_wallpaper_buf = (uint32_t *)kmalloc(1024 * 768 * 4);
  }
  if (photo_wallpaper_buf) {
    if (width == 1024 && height == 768) {
      memcpy(photo_wallpaper_buf, pixels, 1024 * 768 * 4);
    } else {
      image_t src;
      src.width = width;
      src.height = height;
      src.pixels = (uint32_t *)pixels;
      image_t *res = image_resize(&src, 1024, 768);
      if (res) {
        memcpy(photo_wallpaper_buf, res->pixels, 1024 * 768 * 4);
        image_free(res);
      }
    }
    loaded_photo_type = BG_CUSTOM;
    wm_set_background_type(BG_CUSTOM);
    gfx_precompute_blur();
  }
}

void gfx_draw_image(int32_t x, int32_t y, const image_t *img) {
  if (!img || !img->pixels || !lfb_back)
    return;
  int32_t sw = (int32_t)scr_width;
  int32_t sh = (int32_t)scr_height;

  // Fast-path: immagine a schermo intero
  if (x == 0 && y == 0 && img->width == sw && img->height == sh &&
      scr_pitch_pixels == (uint32_t)sw) {
    memcpy(lfb_back, img->pixels, sw * sh * 4);
    return;
  }

  for (int32_t iy = 0; iy < img->height; iy++) {
    int32_t dy = y + iy;
    if (dy < 0 || dy >= sh)
      continue;
    uint32_t *dst_line = lfb_back + dy * scr_pitch_pixels;
    const uint32_t *src_line = img->pixels + iy * img->width;
    for (int32_t ix = 0; ix < img->width; ix++) {
      int32_t dx = x + ix;
      if (dx < 0 || dx >= sw)
        continue;
      uint32_t p = src_line[ix];
      uint32_t a = (p >> 24) & 0xFF;
      if (a == 255) {
        dst_line[dx] = p;
      } else if (a > 0) {
        uint32_t d = dst_line[dx];
        uint32_t inv_a = 255 - a;
        uint32_t r =
            (((p >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * inv_a) / 255;
        uint32_t g = (((p >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * inv_a) / 255;
        uint32_t b = ((p & 0xFF) * a + (d & 0xFF) * inv_a) / 255;
        dst_line[dx] = (0xFF << 24) | (r << 16) | (g << 8) | b;
      }
    }
  }
}
