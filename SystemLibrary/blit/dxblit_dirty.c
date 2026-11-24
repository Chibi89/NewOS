/**
 * SystemLibrary/blit/dxblit_dirty.c
 * Gestione Dirty-Rectangles con clipping e merge automatico
 */
#include "dxblit.h"

static dx_rect_t g_dirty[DXBLIT_MAX_DIRTY];
static int g_dirty_cnt = 0;

void dxblit_dirty_reset(void) { g_dirty_cnt = 0; }

int dxblit_dirty_count(void) { return g_dirty_cnt; }

const dx_rect_t *dxblit_dirty_get(int idx) {
  if (idx < 0 || idx >= g_dirty_cnt)
    return NULL;
  return &g_dirty[idx];
}

static bool rects_overlap_or_touch(const dx_rect_t *a, const dx_rect_t *b) {
  if (a->x + a->w < b->x || b->x + b->w < a->x)
    return false;
  if (a->y + a->h < b->y || b->y + b->h < a->y)
    return false;
  return true;
}

bool dxblit_dirty_add(int32_t x, int32_t y, int32_t w, int32_t h, int32_t max_w,
                      int32_t max_h) {
  if (w <= 0 || h <= 0 || max_w <= 0 || max_h <= 0)
    return false;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > max_w)
    w = max_w - x;
  if (y + h > max_h)
    h = max_h - y;
  if (w <= 0 || h <= 0)
    return false;

  dx_rect_t r = {x, y, w, h};
  for (int i = 0; i < g_dirty_cnt; i++) {
    if (rects_overlap_or_touch(&g_dirty[i], &r)) {
      int32_t nx = g_dirty[i].x < r.x ? g_dirty[i].x : r.x;
      int32_t ny = g_dirty[i].y < r.y ? g_dirty[i].y : r.y;
      int32_t rx1 = g_dirty[i].x + g_dirty[i].w;
      int32_t rx2 = r.x + r.w;
      int32_t ry1 = g_dirty[i].y + g_dirty[i].h;
      int32_t ry2 = r.y + r.h;
      g_dirty[i].x = nx;
      g_dirty[i].y = ny;
      g_dirty[i].w = (rx1 > rx2 ? rx1 : rx2) - nx;
      g_dirty[i].h = (ry1 > ry2 ? ry1 : ry2) - ny;
      return true;
    }
  }
  if (g_dirty_cnt < DXBLIT_MAX_DIRTY) {
    g_dirty[g_dirty_cnt++] = r;
    return true;
  }
  /* Fallback: unifica tutto nell'ultimo slot se saturo */
  g_dirty[0] = (dx_rect_t){0, 0, max_w, max_h};
  g_dirty_cnt = 1;
  return true;
}
