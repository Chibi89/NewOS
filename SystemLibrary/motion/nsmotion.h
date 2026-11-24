/**
 * SystemLibrary/motion/nsmotion.h
 * Servizio Animazioni, Easing Q16.16, Bezier, Keyframe & Fisica 2D (NSMOTION.DLL)
 */
#ifndef NEWOS_NSMOTION_H
#define NEWOS_NSMOTION_H

#if defined(NEWOS_KERNEL_TYPES) || defined(TYPES_H)
#include <types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define NSMOTION_MAGIC 0x4E534D54u /* "NSMT" */
#define NSM_ONE 65536
#define NSM_MAX_TRACKS 32

typedef enum {
  NSM_EASE_LINEAR = 0,
  NSM_EASE_OUT_CUBIC = 1,
  NSM_EASE_IN_OUT_QUAD = 2,
  NSM_EASE_OUT_BACK = 3,
  NSM_EASE_IN_CUBIC = 4,
  NSM_EASE_IN_OUT_CUBIC = 5,
  NSM_EASE_OUT_EXPO = 6,
  NSM_EASE_OUT_BOUNCE = 7,
  NSM_EASE_OUT_ELASTIC = 8
} nsm_ease_t;

typedef struct {
  int32_t x;
  int32_t y;
} nsm_vec2_t;

typedef struct {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} nsm_rect_t;

typedef struct {
  int32_t t_q16;
  int32_t value;
} nsm_keyframe_t;

typedef struct {
  bool active;
  int32_t from;
  int32_t to;
  int32_t current;
  uint32_t elapsed_ms;
  uint32_t duration_ms;
  nsm_ease_t ease;
} nsm_track_t;

typedef struct {
  int32_t pos;
  int32_t vel;
  int32_t target;
} nsm_spring_t;

typedef struct {
  nsm_spring_t x;
  nsm_spring_t y;
} nsm_spring2d_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  void (*set_enabled)(bool en);
  bool (*is_enabled)(void);
  int32_t (*ease_q16)(nsm_ease_t type, int32_t t_q16);
  int32_t (*bezier_q16)(int32_t p1y_q16, int32_t p2y_q16, int32_t t_q16);
  int32_t (*lerp_i32)(int32_t a, int32_t b, int32_t t_q16);
  uint32_t (*lerp_argb)(uint32_t c1, uint32_t c2, int32_t t_q16);
  nsm_vec2_t (*lerp_vec2)(nsm_vec2_t a, nsm_vec2_t b, int32_t t_q16);
  nsm_rect_t (*lerp_rect)(nsm_rect_t a, nsm_rect_t b, int32_t t_q16);
  int32_t (*eval_keyframes)(const nsm_keyframe_t *kf, int count, int32_t t_q16,
                            nsm_ease_t ease);
  int (*track_start)(int32_t from, int32_t to, uint32_t duration_ms,
                     nsm_ease_t ease);
  bool (*track_step)(int id, uint32_t dt_ms, int32_t *out_val);
  void (*spring_step)(nsm_spring_t *sp, int32_t stiffness_q8,
                      int32_t damping_q8);
  void (*spring2d_step)(nsm_spring2d_t *sp, int32_t stiffness_q8,
                        int32_t damping_q8);
} nsmotion_api_t;

/* Funzioni esportabili/testabili singolarmente (test-by-func) */
void nsm_set_enabled(bool en);
bool nsm_is_enabled(void);
int32_t nsm_ease_q16(nsm_ease_t type, int32_t t_q16);
int32_t nsm_bezier_q16(int32_t p1y_q16, int32_t p2y_q16, int32_t t_q16);
int32_t nsm_lerp_i32(int32_t a, int32_t b, int32_t t_q16);
uint32_t nsm_lerp_argb(uint32_t c1, uint32_t c2, int32_t t_q16);
nsm_vec2_t nsm_lerp_vec2(nsm_vec2_t a, nsm_vec2_t b, int32_t t_q16);
nsm_rect_t nsm_lerp_rect(nsm_rect_t a, nsm_rect_t b, int32_t t_q16);
int32_t nsm_eval_keyframes(const nsm_keyframe_t *kf, int count, int32_t t_q16,
                           nsm_ease_t ease);
int nsm_track_start(int32_t from, int32_t to, uint32_t duration_ms,
                    nsm_ease_t ease);
bool nsm_track_step(int id, uint32_t dt_ms, int32_t *out_val);
void nsm_spring_step(nsm_spring_t *sp, int32_t stiffness_q8,
                     int32_t damping_q8);
void nsm_spring2d_step(nsm_spring2d_t *sp, int32_t stiffness_q8,
                       int32_t damping_q8);

#endif
