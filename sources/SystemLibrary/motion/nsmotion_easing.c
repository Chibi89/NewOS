/**
 * SystemLibrary/motion/nsmotion_easing.c
 * Curve di Easing e Cubic Bezier in fixed-point Q16.16 (zero FPU)
 */
#include "nsmotion.h"

static int32_t clamp_q16(int32_t t) {
  if (t < 0)
    return 0;
  if (t > NSM_ONE)
    return NSM_ONE;
  return t;
}

static int32_t ease_out_bounce_q16(int64_t t) {
  /* 7.5625 in Q16 = 495616 */
  const int64_t n1 = 495616;
  if (t < (NSM_ONE * 4) / 11) {
    int64_t t2 = (t * t) >> 16;
    return (int32_t)((n1 * t2) >> 16);
  } else if (t < (NSM_ONE * 8) / 11) {
    t -= (NSM_ONE * 6) / 11;
    int64_t t2 = (t * t) >> 16;
    return (int32_t)(((n1 * t2) >> 16) + ((NSM_ONE * 3) / 4));
  } else if (t < (NSM_ONE * 10) / 11) {
    t -= (NSM_ONE * 9) / 11;
    int64_t t2 = (t * t) >> 16;
    return (int32_t)(((n1 * t2) >> 16) + ((NSM_ONE * 15) / 16));
  } else {
    t -= (NSM_ONE * 21) / 22;
    int64_t t2 = (t * t) >> 16;
    return (int32_t)(((n1 * t2) >> 16) + ((NSM_ONE * 63) / 64));
  }
}

int32_t nsm_ease_q16(nsm_ease_t type, int32_t t_q16) {
  int64_t t = clamp_q16(t_q16);
  if (type == NSM_EASE_LINEAR)
    return (int32_t)t;

  if (type == NSM_EASE_OUT_CUBIC) {
    int64_t inv = NSM_ONE - t;
    int64_t inv3 = (((inv * inv) >> 16) * inv) >> 16;
    return (int32_t)(NSM_ONE - inv3);
  }

  if (type == NSM_EASE_IN_CUBIC) {
    return (int32_t)((((t * t) >> 16) * t) >> 16);
  }

  if (type == NSM_EASE_IN_OUT_CUBIC) {
    if (t < (NSM_ONE / 2))
      return (int32_t)((4 * (((t * t) >> 16) * t)) >> 16);
    int64_t k = (-2 * t) + 2 * NSM_ONE;
    int64_t k3 = (((k * k) >> 16) * k) >> 16;
    return (int32_t)(NSM_ONE - (k3 / 2));
  }

  if (type == NSM_EASE_IN_OUT_QUAD) {
    if (t < (NSM_ONE / 2))
      return (int32_t)((2 * t * t) >> 16);
    int64_t k = (-2 * t) + 2 * NSM_ONE;
    return (int32_t)(NSM_ONE - (((k * k) >> 16) / 2));
  }

  if (type == NSM_EASE_OUT_BACK) {
    int64_t c1 = 111515;
    int64_t c3 = c1 + NSM_ONE;
    int64_t tm1 = t - NSM_ONE;
    int64_t tm1_2 = (tm1 * tm1) >> 16;
    int64_t tm1_3 = (tm1_2 * tm1) >> 16;
    return (int32_t)(NSM_ONE + ((c3 * tm1_3) >> 16) + ((c1 * tm1_2) >> 16));
  }

  if (type == NSM_EASE_OUT_EXPO) {
    if (t >= NSM_ONE)
      return NSM_ONE;
    /* Approssimazione rapida 1 - (1 - t)^5 in Q16.16 */
    int64_t inv = NSM_ONE - t;
    int64_t inv2 = (inv * inv) >> 16;
    int64_t inv5 = ((((inv2 * inv2) >> 16) * inv) >> 16);
    return (int32_t)(NSM_ONE - inv5);
  }

  if (type == NSM_EASE_OUT_BOUNCE) {
    return ease_out_bounce_q16(t);
  }

  if (type == NSM_EASE_OUT_ELASTIC) {
    if (t == 0)
      return 0;
    if (t >= NSM_ONE)
      return NSM_ONE;
    /* Onda elastica smorzata triangolare in Q16.16 (zero FPU) */
    int64_t inv = NSM_ONE - t;
    int64_t decay = (inv * inv) >> 16;
    int32_t phase = (int32_t)((t * 6) % NSM_ONE);
    int32_t wave = (phase < NSM_ONE / 2)
                       ? (phase * 4 - NSM_ONE)
                       : (NSM_ONE - (phase - NSM_ONE / 2) * 4);
    return (int32_t)(NSM_ONE + ((decay * wave) >> 17));
  }

  return (int32_t)t;
}

int32_t nsm_bezier_q16(int32_t p1y_q16, int32_t p2y_q16, int32_t t_q16) {
  int64_t t = clamp_q16(t_q16);
  int64_t u = NSM_ONE - t;
  int64_t u2 = (u * u) >> 16;
  int64_t t2 = (t * t) >> 16;
  int64_t t3 = (t2 * t) >> 16;
  /* B(t) = 3*u^2*t*p1y + 3*u*t^2*p2y + t^3 */
  int64_t term1 = (((3 * u2 * t) >> 16) * p1y_q16) >> 16;
  int64_t term2 = (((3 * u * t2) >> 16) * p2y_q16) >> 16;
  return (int32_t)(term1 + term2 + t3);
}

int32_t nsm_lerp_i32(int32_t a, int32_t b, int32_t t_q16) {
  int64_t diff = (int64_t)b - (int64_t)a;
  return a + (int32_t)((diff * t_q16) >> 16);
}

uint32_t nsm_lerp_argb(uint32_t c1, uint32_t c2, int32_t t_q16) {
  int32_t t = clamp_q16(t_q16);
  uint32_t a = (uint32_t)nsm_lerp_i32((int32_t)((c1 >> 24) & 0xFFu),
                                      (int32_t)((c2 >> 24) & 0xFFu), t);
  uint32_t r = (uint32_t)nsm_lerp_i32((int32_t)((c1 >> 16) & 0xFFu),
                                      (int32_t)((c2 >> 16) & 0xFFu), t);
  uint32_t g = (uint32_t)nsm_lerp_i32((int32_t)((c1 >> 8) & 0xFFu),
                                      (int32_t)((c2 >> 8) & 0xFFu), t);
  uint32_t b = (uint32_t)nsm_lerp_i32((int32_t)(c1 & 0xFFu),
                                      (int32_t)(c2 & 0xFFu), t);
  return ((a & 0xFFu) << 24) | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) |
         (b & 0xFFu);
}
