/**
 * SystemLibrary/motion/nsmotion_types.c
 * Interpolazione di Vec2, Rect, Keyframes multi-punto e Spring 2D
 */
#include "nsmotion.h"

nsm_vec2_t nsm_lerp_vec2(nsm_vec2_t a, nsm_vec2_t b, int32_t t_q16) {
  nsm_vec2_t out;
  out.x = nsm_lerp_i32(a.x, b.x, t_q16);
  out.y = nsm_lerp_i32(a.y, b.y, t_q16);
  return out;
}

nsm_rect_t nsm_lerp_rect(nsm_rect_t a, nsm_rect_t b, int32_t t_q16) {
  nsm_rect_t out;
  out.x = nsm_lerp_i32(a.x, b.x, t_q16);
  out.y = nsm_lerp_i32(a.y, b.y, t_q16);
  out.w = nsm_lerp_i32(a.w, b.w, t_q16);
  out.h = nsm_lerp_i32(a.h, b.h, t_q16);
  return out;
}

int32_t nsm_eval_keyframes(const nsm_keyframe_t *kf, int count, int32_t t_q16,
                           nsm_ease_t ease) {
  if (!kf || count <= 0)
    return 0;
  if (count == 1 || t_q16 <= kf[0].t_q16)
    return kf[0].value;
  if (t_q16 >= kf[count - 1].t_q16)
    return kf[count - 1].value;

  for (int i = 0; i < count - 1; i++) {
    if (t_q16 >= kf[i].t_q16 && t_q16 <= kf[i + 1].t_q16) {
      int32_t span = kf[i + 1].t_q16 - kf[i].t_q16;
      if (span <= 0)
        return kf[i + 1].value;
      int32_t local_t =
          (int32_t)(((int64_t)(t_q16 - kf[i].t_q16) << 16) / span);
      int32_t e = nsm_ease_q16(ease, local_t);
      return nsm_lerp_i32(kf[i].value, kf[i + 1].value, e);
    }
  }
  return kf[count - 1].value;
}

void nsm_spring2d_step(nsm_spring2d_t *sp, int32_t stiffness_q8,
                       int32_t damping_q8) {
  if (!sp)
    return;
  nsm_spring_step(&sp->x, stiffness_q8, damping_q8);
  nsm_spring_step(&sp->y, stiffness_q8, damping_q8);
}
