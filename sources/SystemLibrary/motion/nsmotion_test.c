/**
 * SystemLibrary/motion/nsmotion_test.c
 * Unit test test-by-func per NSMOTION.DLL
 */
#include "nsmotion.h"
#include <assert.h>
#include <stdio.h>

static void test_easing_and_bezier(void) {
  assert(nsm_ease_q16(NSM_EASE_LINEAR, 32768) == 32768);
  assert(nsm_ease_q16(NSM_EASE_OUT_CUBIC, 0) == 0);
  assert(nsm_ease_q16(NSM_EASE_OUT_CUBIC, NSM_ONE) == NSM_ONE);
  assert(nsm_ease_q16(NSM_EASE_IN_CUBIC, 32768) < 32768);
  assert(nsm_ease_q16(NSM_EASE_IN_OUT_CUBIC, 32768) == 32768);
  assert(nsm_ease_q16(NSM_EASE_OUT_EXPO, NSM_ONE) == NSM_ONE);
  assert(nsm_ease_q16(NSM_EASE_OUT_BOUNCE, 0) == 0);
  assert(nsm_ease_q16(NSM_EASE_OUT_BOUNCE, NSM_ONE) >= 65500);
  assert(nsm_ease_q16(NSM_EASE_OUT_ELASTIC, NSM_ONE) == NSM_ONE);
  assert(nsm_bezier_q16(0, NSM_ONE, 0) == 0);
  assert(nsm_bezier_q16(0, NSM_ONE, NSM_ONE) == NSM_ONE);
}

static void test_vec2_rect_keyframes(void) {
  nsm_vec2_t v = nsm_lerp_vec2((nsm_vec2_t){0, 100}, (nsm_vec2_t){200, 300},
                               32768);
  assert(v.x == 100 && v.y == 200);
  nsm_rect_t r = nsm_lerp_rect((nsm_rect_t){0, 0, 100, 100},
                               (nsm_rect_t){100, 50, 300, 200}, 32768);
  assert(r.x == 50 && r.y == 25 && r.w == 200 && r.h == 150);

  nsm_keyframe_t kfs[3] = {{0, 0}, {32768, 200}, {65536, 100}};
  assert(nsm_eval_keyframes(kfs, 3, 0, NSM_EASE_LINEAR) == 0);
  assert(nsm_eval_keyframes(kfs, 3, 32768, NSM_EASE_LINEAR) == 200);
  assert(nsm_eval_keyframes(kfs, 3, 49152, NSM_EASE_LINEAR) == 150);
}

static void test_timeline_and_spring2d(void) {
  nsm_set_enabled(true);
  int tr = nsm_track_start(0, 100, 100, NSM_EASE_OUT_CUBIC);
  assert(tr >= 0);
  int32_t val = 0;
  assert(nsm_track_step(tr, 50, &val) == true);
  assert(val > 50 && val < 100);
  assert(nsm_track_step(tr, 50, &val) == false);
  assert(val == 100);

  nsm_spring2d_t sp2 = {{0, 0, 100}, {200, 0, 50}};
  for (int i = 0; i < 40; i++)
    nsm_spring2d_step(&sp2, 48, 200);
  assert(sp2.x.pos > 85 && sp2.x.pos <= 115);
  assert(sp2.y.pos >= 40 && sp2.y.pos <= 65);
}

int main(void) {
  test_easing_and_bezier();
  test_vec2_rect_keyframes();
  test_timeline_and_spring2d();
  printf("[PASS] NSMOTION test-by-func: 3/3 suite OK\n");
  return 0;
}
