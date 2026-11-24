/**
 * SystemLibrary/compositor/nscomp_test.c
 * Unit test test-by-func per NSCOMP.DLL
 */
#include "nscomp.h"
#include <assert.h>
#include <stdio.h>

static void test_surface_lifecycle(void) {
  uint32_t buf[16];
  assert(nsc_surface_init(0, 4, 4, buf) == 0);
  assert(nsc_surface_is_dirty(0) == true);
  nsc_surface_clear_dirty(0);
  assert(nsc_surface_is_dirty(0) == false);
  nsc_surface_mark_dirty(0);
  assert(nsc_surface_is_dirty(0) == true);
  nsc_surface_free(0);
  assert(nsc_surface_is_dirty(0) == false);
}

static void test_blur_shadow_tint_and_mask(void) {
  uint32_t src[9] = {0xFF000000u, 0xFF000000u, 0xFF000000u,
                     0xFF000000u, 0xFFFFFFFFu, 0xFF000000u,
                     0xFF000000u, 0xFF000000u, 0xFF000000u};
  uint32_t dst[9] = {0};
  nsc_blur_box_3x3(dst, src, 3, 3);
  uint32_t r = (dst[4] >> 16) & 0xFFu;
  assert(r == 28);
  assert(nsc_shadow_alpha_edge(0, 8, 120) == 120);
  assert(nsc_shadow_alpha_edge(8, 8, 120) == 0);
  assert(nsc_shadow_alpha_edge(4, 8, 120) == 30);

  uint32_t panel[36];
  for (int i = 0; i < 36; i++)
    panel[i] = 0xFF000000u;
  nsc_tint_rect(panel, 6, 6, 6, 0xFFFFFFFFu, 128);
  assert(((panel[0] >> 16) & 0xFFu) >= 126);
  nsc_apply_round_mask(panel, 6, 6, 6, 2);
  assert(panel[0] == 0x00000000u); /* Angolo esterno tagliato */
  assert(panel[14] != 0x00000000u); /* Centro intatto */
}

static void test_liquid_refraction(void) {
  /* Sfondo a gradiente orizzontale: colonna x ha valore x * 10 */
  uint32_t bg[20 * 20];
  uint32_t dst[20 * 20];
  for (int y = 0; y < 20; y++) {
    for (int x = 0; x < 20; x++) {
      bg[y * 20 + x] = 0xFF000000u | ((uint32_t)x * 10u);
      dst[y * 20 + x] = 0;
    }
  }
  nsc_liquid_refraction(dst, bg, 20, 20, 20, 20, 10, 24);
  /* Bordo sinistro (x=0) e zona intermedia interna (x=4) devono mostrare rifrazione */
  assert((dst[10 * 20 + 0] & 0xFFu) != (bg[10 * 20 + 0] & 0xFFu));
  assert((dst[10 * 20 + 4] & 0xFFu) != (bg[10 * 20 + 4] & 0xFFu));
}

int main(void) {
  test_surface_lifecycle();
  test_blur_shadow_tint_and_mask();
  test_liquid_refraction();
  printf("[PASS] NSCOMP test-by-func: 3/3 suite OK\n");
  return 0;
}
