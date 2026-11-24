/**
 * SystemLibrary/compositor/nscomp.h
 * Servizio Compositor Offscreen & Effetti Liquid Glass / Ombre (NSCOMP.DLL)
 */
#ifndef NEWOS_NSCOMP_H
#define NEWOS_NSCOMP_H

#if defined(NEWOS_KERNEL_TYPES) || defined(TYPES_H)
#include <types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define NSCOMP_MAGIC 0x4E534350u /* "NSCP" */
#define NSCOMP_MAX_SURFACES 16

typedef struct {
  bool in_use;
  bool dirty;
  int32_t w;
  int32_t h;
  uint32_t *pixels;
} nsc_surface_t;

typedef struct {
  uint8_t blur_passes;
  uint32_t tint_argb;
  uint8_t tint_alpha;
  int32_t corner_radius;
  int32_t shadow_radius;
} nsc_backdrop_cfg_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  int (*surface_init)(int id, int32_t w, int32_t h, uint32_t *backing_buf);
  void (*surface_free)(int id);
  void (*surface_mark_dirty)(int id);
  void (*surface_clear_dirty)(int id);
  bool (*surface_is_dirty)(int id);
  void (*blur_box_3x3)(uint32_t *dst, const uint32_t *src, int32_t w,
                       int32_t h);
  uint8_t (*shadow_alpha_edge)(int32_t dist_px, int32_t radius_px,
                               uint8_t max_alpha);
  void (*tint_rect)(uint32_t *buf, int32_t pitch, int32_t w, int32_t h,
                    uint32_t tint_argb, uint8_t alpha);
  void (*apply_round_mask)(uint32_t *buf, int32_t pitch, int32_t w, int32_t h,
                           int32_t radius);
  void (*liquid_refraction)(uint32_t *dst, const uint32_t *bg,
                            int32_t dst_pitch, int32_t bg_pitch, int32_t w,
                            int32_t h, int32_t rim_px, int32_t strength_px);
} nscomp_api_t;

int nsc_surface_init(int id, int32_t w, int32_t h, uint32_t *backing_buf);
void nsc_surface_free(int id);
void nsc_surface_mark_dirty(int id);
void nsc_surface_clear_dirty(int id);
bool nsc_surface_is_dirty(int id);
void nsc_blur_box_3x3(uint32_t *dst, const uint32_t *src, int32_t w, int32_t h);
uint8_t nsc_shadow_alpha_edge(int32_t dist_px, int32_t radius_px,
                              uint8_t max_alpha);
void nsc_tint_rect(uint32_t *buf, int32_t pitch, int32_t w, int32_t h,
                   uint32_t tint_argb, uint8_t alpha);
void nsc_apply_round_mask(uint32_t *buf, int32_t pitch, int32_t w, int32_t h,
                          int32_t radius);
void nsc_liquid_refraction(uint32_t *dst, const uint32_t *bg, int32_t dst_pitch,
                           int32_t bg_pitch, int32_t w, int32_t h,
                           int32_t rim_px, int32_t strength_px);

#endif
