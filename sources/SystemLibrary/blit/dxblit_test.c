/**
 * SystemLibrary/blit/dxblit_test.c
 * Unit test test-by-func per DXBLIT.DLL
 */
#include "dxblit.h"
#include <assert.h>
#include <stdio.h>

static void test_fill_and_copy(void) {
  uint32_t src[16] = {0};
  uint32_t dst[16] = {0};
  dxblit_fill_rect(src, 4, 1, 1, 2, 2, 0xFF112233u);
  assert(src[5] == 0xFF112233u && src[10] == 0xFF112233u);
  assert(src[0] == 0u);
  dxblit_copy_rect(dst, 4, src, 4, 4, 4);
  assert(dst[5] == 0xFF112233u && dst[10] == 0xFF112233u);
}

static void test_alpha_blend(void) {
  uint32_t dst[4] = {0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u};
  uint32_t src[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  dxblit_alpha_blend_rect(dst, 2, src, 2, 2, 2, 128);
  uint32_t r = (dst[0] >> 16) & 0xFFu;
  assert(r >= 126 && r <= 129);
}

static void test_scale_grad_and_565(void) {
  uint32_t sp[4] = {0xFFAA0000u, 0xFF00BB00u, 0xFF0000CCu, 0xFFFFFFFFu};
  uint32_t dp[16] = {0};
  dx_surface_desc_t sd = {sp, 2, 2, 2, DX_FMT_ARGB8888};
  dx_surface_desc_t dd = {dp, 4, 4, 4, DX_FMT_ARGB8888};
  dxblit_scale_nearest(&sd, (dx_rect_t){0, 0, 2, 2}, &dd,
                       (dx_rect_t){0, 0, 4, 4});
  assert(dp[0] == 0xFFAA0000u && dp[3] == 0xFF00BB00u && dp[15] == 0xFFFFFFFFu);

  uint32_t grad[6] = {0};
  dxblit_gradient_v(grad, 2, 0, 0, 2, 3, 0xFF000000u, 0xFF0000FEu);
  assert((grad[0] & 0xFFu) == 0 && (grad[2] & 0xFFu) == 127 &&
         (grad[4] & 0xFFu) == 254);

  uint16_t c565[2] = {0xF800u, 0x07E0u}; /* Rosso puro, Verde puro */
  uint32_t c8888[2] = {0};
  dxblit_convert_565_to_8888(c8888, c565, 2);
  assert(c8888[0] == 0xFFFF0000u && c8888[1] == 0xFF00FF00u);
}

static void test_dirty_rects(void) {
  dxblit_dirty_reset();
  assert(dxblit_dirty_count() == 0);
  assert(dxblit_dirty_add(-10, 5, 30, 20, 100, 100) == true);
  const dx_rect_t *r0 = dxblit_dirty_get(0);
  assert(r0 && r0->x == 0 && r0->y == 5 && r0->w == 20 && r0->h == 20);
  assert(dxblit_dirty_add(15, 10, 20, 20, 100, 100) == true);
  assert(dxblit_dirty_count() == 1);
  r0 = dxblit_dirty_get(0);
  assert(r0->x == 0 && r0->y == 5 && r0->w == 35 && r0->h == 25);
}

int main(void) {
  test_fill_and_copy();
  test_alpha_blend();
  test_scale_grad_and_565();
  test_dirty_rects();
  printf("[PASS] DXBLIT test-by-func: 4/4 suite OK\n");
  return 0;
}
