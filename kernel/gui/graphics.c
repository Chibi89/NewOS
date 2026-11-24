#include <arch/x86_64/io.h>
#include <drivers/ata.h>
#include <drivers/mouse.h>
#include <fs/vfs.h>
#include <gui/font.h>
#include <gui/fontmgr.h>
#include <gui/graphics.h>
#include <gui/hwaccel_loader.h>
#include <gui/image.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <string.h>


static uint32_t *photo_wallpaper_buf = NULL;
static int loaded_photo_type = -1;
static uint32_t loaded_photo_w = 0;
static uint32_t loaded_photo_h = 0;
static bool active_bg_is_photo = false;
static uint32_t *blurred_buf = NULL; // Pre-blurred wallpaper per Liquid Glass
static size_t blurred_buf_capacity = 0;

static uint32_t *lfb_front = NULL;
static uint32_t *lfb_back = NULL;
static size_t lfb_back_capacity = 0;
static uint32_t scr_width = 1024;
static uint32_t scr_height = 768;
static uint32_t scr_pitch_pixels = 1024;
static uint32_t scr_pitch_bytes = 4096;
static uint32_t scr_bpp = 32;
static uint32_t g_ui_scale = 100;

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
      if (hwaccel_blit_32(lfb_front, lfb_back, scr_width * scr_height))
        return;
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
  if (hwaccel_fill_32(lfb_back, color, (uint32_t)total))
    return;
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

static void gfx_draw_glyph_rows(int32_t x, int32_t y, const uint8_t rows[16],
                                uint32_t fg, uint32_t bg, bool transparent) {
  if (g_ui_scale <= 100) {
    for (int cy = 0; cy < FONT_HEIGHT; cy++) {
      uint8_t line = rows[cy];
      for (int cx = 0; cx < FONT_WIDTH; cx++) {
        if (line & (0x80 >> cx)) {
          gfx_draw_pixel(x + cx, y + cy, fg);
        } else if (!transparent) {
          gfx_draw_pixel(x + cx, y + cy, bg);
        }
      }
    }
    return;
  }

  // Scaling DPI nitido (nearest-neighbor senza sfocatura)
  for (int cy = 0; cy < FONT_HEIGHT; cy++) {
    uint8_t line = rows[cy];
    int32_t py0 = (cy * (int32_t)g_ui_scale) / 100;
    int32_t py1 = ((cy + 1) * (int32_t)g_ui_scale) / 100;
    if (py1 <= py0) py1 = py0 + 1;

    for (int cx = 0; cx < FONT_WIDTH; cx++) {
      bool on = ((line & (0x80 >> cx)) != 0);
      if (!on && transparent)
        continue;
      int32_t px0 = (cx * (int32_t)g_ui_scale) / 100;
      int32_t px1 = ((cx + 1) * (int32_t)g_ui_scale) / 100;
      if (px1 <= px0) px1 = px0 + 1;
      uint32_t col = on ? fg : bg;
      for (int32_t sy = py0; sy < py1; sy++) {
        for (int32_t sx = px0; sx < px1; sx++) {
          gfx_draw_pixel(x + sx, y + sy, col);
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
  gfx_draw_glyph_rows(x, y, font8x16_basic[uc], fg, bg, transparent);
}

void gfx_draw_string(int32_t x, int32_t y, const char *str, uint32_t fg,
                     uint32_t bg, bool transparent) {
  if (!str)
    return;
  int32_t cw = (int32_t)gfx_get_font_w();
  int32_t ch = (int32_t)gfx_get_font_h();
  int32_t cx = x;
  const uint8_t *p = (const uint8_t *)str;
  while (*p) {
    if (*p == '\n') {
      y += ch;
      cx = x;
      p++;
      continue;
    }
    uint32_t cp = *p++;
    if ((cp & 0xE0) == 0xC0 && (p[0] & 0xC0) == 0x80) {
      cp = ((cp & 0x1F) << 6) | (p[0] & 0x3F);
      p += 1;
    } else if ((cp & 0xF0) == 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
      cp = ((cp & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((cp & 0xF8) == 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      cp = ((cp & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else if (cp >= 0x80) {
      cp = 0xFFFD;
    }

    if (cp < 128) {
      gfx_draw_glyph_rows(cx, y, font8x16_basic[cp], fg, bg, transparent);
    } else {
      uint8_t urows[16];
      if (fontmgr_get_glyph(cp, urows)) {
        gfx_draw_glyph_rows(cx, y, urows, fg, bg, transparent);
      } else {
        gfx_draw_glyph_rows(cx, y, font8x16_basic['?'], fg, bg, transparent);
      }
    }
    cx += cw;
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

void gfx_draw_resize_cursor(int32_t x, int32_t y, int resize_edge) {
  static const char *const cur_horiz[9] = {
      "..B.........B..",
      ".BWB.......BWB.",
      "BWWWB.....BWWWB",
      "BWWWWBBBBBWWWWB",
      "BWWWWWWWWWWWWWB",
      "BWWWWBBBBBWWWWB",
      "BWWWB.....BWWWB",
      ".BWB.......BWB.",
      "..B.........B.."};

  static const char *const cur_vert[15] = {
      "....B....",
      "...BWB...",
      "..BWWWB..",
      ".BWWWWWB.",
      "BWWWWWWWB",
      "..BBWBB..",
      "..BBWBB..",
      "..BBWBB..",
      "..BBWBB..",
      "..BBWBB..",
      "BWWWWWWWB",
      ".BWWWWWB.",
      "..BWWWB..",
      "...BWB...",
      "....B...."};

  static const char *const cur_diag_nwse[13] = {
      "BBBBBB.......",
      "BWWWWWB......",
      "BWWWWWB......",
      "BWWBB........",
      "BWB.BWB......",
      "BB...BWB.....",
      "......BWB....",
      ".....BWB...BB",
      "......BWB.BWB",
      "........BBWWB",
      "......BWWWWWB",
      "......BWWWWWB",
      ".......BBBBBB"};

  static const char *const cur_diag_nesw[13] = {
      ".......BBBBBB",
      "......BWWWWWB",
      "......BWWWWWB",
      "........BBWWB",
      "......BWB.BWB",
      ".....BWB...BB",
      "....BWB......",
      "BB...BWB.....",
      "BWB.BWB......",
      "BWWBB........",
      "BWWWWWB......",
      "BWWWWWB......",
      "BBBBBB......."};

  const char *const *bmp = NULL;
  int w = 0, h = 0, ox = 0, oy = 0;

  if (resize_edge == 1 || resize_edge == 4) { // RESIZE_RIGHT, RESIZE_LEFT
    bmp = cur_horiz;
    w = 15;
    h = 9;
    ox = 7;
    oy = 4;
  } else if (resize_edge == 2 || resize_edge == 5) { // RESIZE_BOTTOM, RESIZE_TOP
    bmp = cur_vert;
    w = 9;
    h = 15;
    ox = 4;
    oy = 7;
  } else if (resize_edge == 3 || resize_edge == 8) { // RESIZE_BOTTOM_RIGHT, RESIZE_TOP_LEFT
    bmp = cur_diag_nwse;
    w = 13;
    h = 13;
    ox = 6;
    oy = 6;
  } else if (resize_edge == 6 || resize_edge == 7) { // RESIZE_TOP_RIGHT, RESIZE_BOTTOM_LEFT
    bmp = cur_diag_nesw;
    w = 13;
    h = 13;
    ox = 6;
    oy = 6;
  } else {
    gfx_draw_cursor(x, y);
    return;
  }

  for (int cy = 0; cy < h; cy++) {
    for (int cx = 0; cx < w; cx++) {
      char c = bmp[cy][cx];
      if (c == 'B') {
        gfx_draw_pixel(x - ox + cx, y - oy + cy, 0x000000);
      } else if (c == 'W') {
        gfx_draw_pixel(x - ox + cx, y - oy + cy, 0xFFFFFF);
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

static void gfx_copy_wallpaper_to_back(void) {
  if (!photo_wallpaper_buf || !lfb_back)
    return;
  uint32_t copy_w = (loaded_photo_w < scr_width) ? loaded_photo_w : scr_width;
  uint32_t copy_h = (loaded_photo_h < scr_height) ? loaded_photo_h : scr_height;
  for (uint32_t y = 0; y < copy_h; y++) {
    memcpy(&lfb_back[y * scr_pitch_pixels],
           &photo_wallpaper_buf[y * loaded_photo_w], copy_w * sizeof(uint32_t));
  }
}

void gfx_draw_background(int bg_type) {
  int32_t h = scr_height - 28; // Esclude la taskbar
  int32_t w = scr_width;
  active_bg_is_photo = false;

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
    if (loaded_photo_type != bg_type || loaded_photo_w != scr_width ||
        loaded_photo_h != scr_height) {
      const char *fn = (bg_type == BG_PHOTO_HERO) ? "HERO" : "FLOW";
      char path_buf[128];
      image_t *img = NULL;

      mini_snprintf(path_buf, sizeof(path_buf),
                    "C:\\NewOS\\Resources\\Wallpapers\\%s.BMP", fn);
      img = image_load_from_file(path_buf);
      if (!img) {
        mini_snprintf(path_buf, sizeof(path_buf),
                      "C:\\NewOS\\Resources\\Wallpapers\\%s.JPG", fn);
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
        if (photo_wallpaper_buf &&
            (loaded_photo_w != scr_width || loaded_photo_h != scr_height)) {
          kfree(photo_wallpaper_buf);
          photo_wallpaper_buf = NULL;
        }
        if (!photo_wallpaper_buf) {
          photo_wallpaper_buf =
              (uint32_t *)kmalloc((size_t)scr_width * scr_height * 4);
        }
        if (photo_wallpaper_buf) {
          if ((uint32_t)img->width == scr_width &&
              (uint32_t)img->height == scr_height) {
            memcpy(photo_wallpaper_buf, img->pixels,
                   (size_t)scr_width * scr_height * 4);
          } else {
            image_t *res = image_resize(img, scr_width, scr_height);
            if (res) {
              memcpy(photo_wallpaper_buf, res->pixels,
                     (size_t)scr_width * scr_height * 4);
              image_free(res);
            }
          }
          loaded_photo_type = bg_type;
          loaded_photo_w = scr_width;
          loaded_photo_h = scr_height;
        }
        image_free(img);
      }
    }
    if (photo_wallpaper_buf && loaded_photo_type == bg_type &&
        loaded_photo_w == scr_width && loaded_photo_h == scr_height) {
      gfx_copy_wallpaper_to_back();
      active_bg_is_photo = true;
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
    if (photo_wallpaper_buf && loaded_photo_w > 0 && loaded_photo_h > 0) {
      gfx_copy_wallpaper_to_back();
      active_bg_is_photo = true;
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

uint64_t gfx_get_lfb_phys(void) { return (uint64_t)(uintptr_t)lfb_front; }

void gfx_set_ui_scale(uint32_t scale_percent) {
  if (scale_percent < 100) scale_percent = 100;
  if (scale_percent > 200) scale_percent = 200;
  g_ui_scale = scale_percent;
}

uint32_t gfx_get_ui_scale(void) { return g_ui_scale; }

int32_t gfx_scale(int32_t val) {
  if (g_ui_scale <= 100) return val;
  return (val * (int32_t)g_ui_scale + 50) / 100;
}

uint32_t gfx_get_font_w(void) {
  if (g_ui_scale <= 100) return FONT_WIDTH;
  return (FONT_WIDTH * g_ui_scale + 50) / 100;
}

uint32_t gfx_get_font_h(void) {
  if (g_ui_scale <= 100) return FONT_HEIGHT;
  return (FONT_HEIGHT * g_ui_scale + 50) / 100;
}

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

  // Reset buffer sfondi fotografici e Liquid Glass blur
  if (photo_wallpaper_buf) {
    kfree(photo_wallpaper_buf);
    photo_wallpaper_buf = NULL;
    loaded_photo_type = -1;
    loaded_photo_w = 0;
    loaded_photo_h = 0;
    active_bg_is_photo = false;
  }

  if (blurred_buf) {
    kfree(blurred_buf);
    blurred_buf = NULL;
    blurred_buf_capacity = 0;
  }

  // Pulizia completa backbuffer per prevenire artefatti residui
  gfx_clear(0);

  // Limiti coordinate mouse
  mouse_set_bounds(width, height);
  hwaccel_on_resolution_changed(width, height);

  // Notifica Window Manager per riallineare finestre e menu
  wm_on_resolution_changed(old_w, old_h, width, height);

  // Ridisegna sfondo, aggiorna blur Liquid Glass e renderizza subito l'intera scena
  gfx_draw_background(wm_get_background_type());
  const theme_info_t *th = wm_get_current_theme_props();
  if (wm_get_theme() != THEME_CLASSIC || (th && th->surface_alpha < 255)) {
    gfx_precompute_blur();
  }
  wm_render();
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

// Calcola la copertura anti-aliased (0..255) di (px, py) in un rounded-rect
// tramite supersampling 4x4 (16x) in aritmetica intera pura sui soli pixel di bordo
static inline uint8_t rounded_coverage(int32_t px, int32_t py, int32_t x,
                                       int32_t y, int32_t w, int32_t h,
                                       int32_t r) {
  if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h)
    return 0;
  if (r <= 0)
    return 255;
  if (r * 2 > w)
    r = w / 2;
  if (r * 2 > h)
    r = h / 2;
  if (r <= 0)
    return 255;

  int32_t cx, cy;
  if (px < x + r && py < y + r) {
    cx = x + r;
    cy = y + r;
  } else if (px >= x + w - r && py < y + r) {
    cx = x + w - 1 - r;
    cy = y + r;
  } else if (px < x + r && py >= y + h - r) {
    cx = x + r;
    cy = y + h - 1 - r;
  } else if (px >= x + w - r && py >= y + h - r) {
    cx = x + w - 1 - r;
    cy = y + h - 1 - r;
  } else {
    return 255;
  }

  int32_t dx = px - cx;
  int32_t dy = py - cy;
  int32_t adx = dx < 0 ? -dx : dx;
  int32_t ady = dy < 0 ? -dy : dy;
  int32_t r2 = r * r;

  if ((adx + 1) * (adx + 1) + (ady + 1) * (ady + 1) <= r2)
    return 255;
  if (adx > 0 && ady > 0 && (adx - 1) * (adx - 1) + (ady - 1) * (ady - 1) > r2)
    return 0;

  static const int32_t sub_off[4] = {-3, -1, 1, 3};
  int32_t r8_sq = (r * 8) * (r * 8);
  int32_t dx8 = dx * 8;
  int32_t dy8 = dy * 8;
  int hits = 0;
  for (int sy = 0; sy < 4; sy++) {
    int32_t ty = dy8 + sub_off[sy];
    int32_t ty2 = ty * ty;
    for (int sx = 0; sx < 4; sx++) {
      int32_t tx = dx8 + sub_off[sx];
      if (tx * tx + ty2 <= r8_sq)
        hits++;
    }
  }
  return (uint8_t)((hits * 255) >> 4);
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
      uint8_t cov = rounded_coverage(px, py, x, y, w, h, radius);
      if (cov == 255)
        row[px] = color;
      else if (cov > 0)
        row[px] = blend_color(row[px], color, cov);
    }
  }
}

// ---- gfx_draw_rounded_rect ---------------------------------
// Disegna il bordo perimetrale continuo e anti-aliased di 1px sugli angoli
void gfx_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t radius, uint32_t color) {
  if (w <= 0 || h <= 0)
    return;
  if (radius <= 0) {
    gfx_draw_rect(x, y, w, h, color);
    return;
  }
  if (radius * 2 > w)
    radius = w / 2;
  if (radius * 2 > h)
    radius = h / 2;

  int32_t r = radius;
  int32_t r_in = (r > 1) ? (r - 1) : 0;

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

  // 3. I 4 angoli arrotondati con anti-aliasing (copertura esterna - interna)
  const int32_t corners_x[4][2] = {
      {x, x + r}, {x + w - r, x + w}, {x, x + r}, {x + w - r, x + w}};
  const int32_t corners_y[4][2] = {
      {y, y + r}, {y, y + r}, {y + h - r, y + h}, {y + h - r, y + h}};
  for (int c = 0; c < 4; c++) {
    int32_t cy1 = corners_y[c][0] < 0 ? 0 : corners_y[c][0];
    int32_t cy2 = corners_y[c][1] > (int32_t)scr_height ? (int32_t)scr_height
                                                        : corners_y[c][1];
    int32_t cx1 = corners_x[c][0] < 0 ? 0 : corners_x[c][0];
    int32_t cx2 = corners_x[c][1] > (int32_t)scr_width ? (int32_t)scr_width
                                                       : corners_x[c][1];
    for (int32_t py = cy1; py < cy2; py++) {
      uint32_t *row = &lfb_back[py * scr_pitch_pixels];
      for (int32_t px = cx1; px < cx2; px++) {
        uint8_t cov_out = rounded_coverage(px, py, x, y, w, h, r);
        uint8_t cov_in = (w > 2 && h > 2)
                             ? rounded_coverage(px, py, x + 1, y + 1, w - 2,
                                                h - 2, r_in)
                             : 0;
        if (cov_out > cov_in) {
          uint8_t ring = (uint8_t)(cov_out - cov_in);
          row[px] = (ring == 255) ? color : blend_color(row[px], color, ring);
        }
      }
    }
  }
}

// ---- gfx_fill_rounded_alpha ---------------------------------
// Blenda il tint con AA sul contenuto gia' presente nel back-buffer (evita aloni neri/wallpaper nei controlli)
static void fill_rounded_alpha_impl(int32_t x, int32_t y, int32_t w, int32_t h,
                                    int32_t radius, uint32_t tint,
                                    uint8_t alpha, bool use_blurred_bg) {
  int32_t x1 = x < 0 ? 0 : x;
  int32_t y1 = y < 0 ? 0 : y;
  int32_t x2 = (x + w > (int32_t)scr_width) ? (int32_t)scr_width : x + w;
  int32_t y2 = (y + h > (int32_t)scr_height) ? (int32_t)scr_height : y + h;
  for (int32_t py = y1; py < y2; py++) {
    uint32_t *row = &lfb_back[py * scr_pitch_pixels];
    for (int32_t px = x1; px < x2; px++) {
      uint8_t cov = rounded_coverage(px, py, x, y, w, h, radius);
      if (cov == 0)
        continue;
      uint8_t eff_alpha = (cov == 255) ? alpha : (uint8_t)(((uint32_t)alpha * cov) / 255);
      if (eff_alpha == 0)
        continue;
      uint32_t base = (use_blurred_bg && blurred_buf)
                          ? blurred_buf[py * scr_pitch_pixels + px]
                          : row[px];
      row[px] = blend_color(base, tint, eff_alpha);
    }
  }
}

void gfx_fill_rounded_alpha(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, uint32_t tint, uint8_t alpha) {
  fill_rounded_alpha_impl(x, y, w, h, radius, tint, alpha, false);
}

void gfx_fill_rounded_glass(int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, uint32_t tint, uint8_t alpha) {
  fill_rounded_alpha_impl(x, y, w, h, radius, tint, alpha, true);
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
      uint8_t cov = rounded_coverage(px, py, x, y, w, h, radius);
      if (cov == 0)
        continue;
      uint8_t eff = (cov == 255) ? alphas[row] : (uint8_t)(((uint32_t)alphas[row] * cov) / 255);
      scanline[px] = blend_color(scanline[px], LG_SPECULAR_COLOR, eff);
    }
  }
  // Ultima riga: bordo inferiore sottilissimo semi-trasparente
  int32_t py_bot = y + h - 1;
  if (py_bot >= 0 && py_bot < (int32_t)scr_height) {
    uint32_t *scanline = &lfb_back[py_bot * scr_pitch_pixels];
    for (int32_t px = x; px < x + w; px++) {
      if (px < 0 || px >= (int32_t)scr_width)
        continue;
      uint8_t cov = rounded_coverage(px, py_bot, x, y, w, h, radius);
      if (cov == 0)
        continue;
      uint8_t eff = (cov == 255) ? LG_BORDER_ALPHA : (uint8_t)(((uint32_t)LG_BORDER_ALPHA * cov) / 255);
      scanline[px] = blend_color(scanline[px], LG_BORDER_COLOR, eff);
    }
  }
}

// ---- gfx_draw_drop_shadow -----------------------------------
void gfx_draw_drop_shadow(int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t spread) {
  (void)spread;
  const theme_info_t *th = wm_get_current_theme_props();
  int32_t r = (th && th->corner_radius > 0) ? th->corner_radius : 8;
  // Ombra leggera e centrata su tutti e 4 i lati della finestra con AA
  static const struct {
    int32_t pad;
    uint8_t a;
  } layers[] = {{2, 16}, {4, 9}, {6, 4}};
  for (int li = 0; li < 3; li++) {
    int32_t pad = layers[li].pad;
    int32_t sx = x - pad;
    int32_t sy = y - pad;
    int32_t sw_box = w + pad * 2;
    int32_t sh_box = h + pad * 2;
    int32_t x1 = sx < 0 ? 0 : sx;
    int32_t y1 = sy < 0 ? 0 : sy;
    int32_t x2 = (sx + sw_box > (int32_t)scr_width) ? (int32_t)scr_width
                                                    : sx + sw_box;
    int32_t y2 = (sy + sh_box > (int32_t)scr_height) ? (int32_t)scr_height
                                                     : sy + sh_box;
    for (int32_t py = y1; py < y2; py++) {
      uint32_t *row = &lfb_back[py * scr_pitch_pixels];
      for (int32_t px = x1; px < x2; px++) {
        uint8_t cov_out = rounded_coverage(px, py, sx, sy, sw_box, sh_box, r + pad);
        if (cov_out == 0)
          continue;
        uint8_t cov_in = rounded_coverage(px, py, x + 1, y + 1, w - 2, h - 2, r);
        if (cov_out <= cov_in)
          continue;
        uint8_t ring = (uint8_t)(cov_out - cov_in);
        uint8_t eff = (ring == 255) ? layers[li].a : (uint8_t)(((uint32_t)layers[li].a * ring) / 255);
        if (eff > 0)
          row[px] = blend_color(row[px], LG_SHADOW_COLOR, eff);
      }
    }
  }
}

// ---- gfx_precompute_blur ------------------------------------
// Separable box-blur 5x5 sul buffer di sfondo attivo -> blurred_buf.
// Se non c'e' foto attiva, sfoca il back-buffer attuale (sfondi procedurali).
void gfx_precompute_blur(void) {
  size_t need = (size_t)scr_width * scr_height * sizeof(uint32_t);
  if (blurred_buf && blurred_buf_capacity < need) {
    kfree(blurred_buf);
    blurred_buf = NULL;
    blurred_buf_capacity = 0;
  }
  if (!blurred_buf) {
    blurred_buf = (uint32_t *)kmalloc(need);
    if (!blurred_buf)
      return;
    blurred_buf_capacity = need;
  }

  uint32_t *src =
      (active_bg_is_photo && photo_wallpaper_buf && loaded_photo_w == scr_width &&
       loaded_photo_h == scr_height)
          ? photo_wallpaper_buf
          : lfb_back;
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
  if (photo_wallpaper_buf &&
      (loaded_photo_w != scr_width || loaded_photo_h != scr_height)) {
    kfree(photo_wallpaper_buf);
    photo_wallpaper_buf = NULL;
  }
  if (!photo_wallpaper_buf) {
    photo_wallpaper_buf =
        (uint32_t *)kmalloc((size_t)scr_width * scr_height * 4);
  }
  if (photo_wallpaper_buf) {
    if ((uint32_t)width == scr_width && (uint32_t)height == scr_height) {
      memcpy(photo_wallpaper_buf, pixels, (size_t)scr_width * scr_height * 4);
    } else {
      image_t src;
      src.width = width;
      src.height = height;
      src.pixels = (uint32_t *)pixels;
      image_t *res = image_resize(&src, scr_width, scr_height);
      if (res) {
        memcpy(photo_wallpaper_buf, res->pixels,
               (size_t)scr_width * scr_height * 4);
        image_free(res);
      }
    }
    loaded_photo_type = BG_CUSTOM;
    loaded_photo_w = scr_width;
    loaded_photo_h = scr_height;
    active_bg_is_photo = true;
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

uint32_t *gfx_get_backbuffer(void) { return lfb_back; }
