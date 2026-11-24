/**
 * SystemLibrary/blit/dxblit.h
 * Servizio Blitting Vettoriale, Scaling, Gradienti & Dirty-Rectangles
 * (DXBLIT.DLL)
 */
#ifndef NEWOS_DXBLIT_H
#define NEWOS_DXBLIT_H

#if defined(NEWOS_KERNEL_TYPES) || defined(TYPES_H)
#include <types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define DXBLIT_MAGIC 0x4458424Cu /* "DXBL" */
#define DXBLIT_MAX_DIRTY 32

typedef enum { DX_FMT_ARGB8888 = 0, DX_FMT_RGB565 = 1 } dx_pixel_format_t;

typedef struct {
  int32_t x, y, w, h;
} dx_rect_t;

typedef struct {
  uint32_t *pixels;
  int32_t w;
  int32_t h;
  int32_t pitch;
  dx_pixel_format_t fmt;
} dx_surface_desc_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  void (*copy_rect)(uint32_t *dst, int32_t dst_pitch, const uint32_t *src,
                    int32_t src_pitch, int32_t w, int32_t h);
  void (*fill_rect)(uint32_t *dst, int32_t dst_pitch, int32_t x, int32_t y,
                    int32_t w, int32_t h, uint32_t argb);
  void (*alpha_blend_rect)(uint32_t *dst, int32_t dst_pitch,
                           const uint32_t *src, int32_t src_pitch, int32_t w,
                           int32_t h, uint8_t global_alpha);
  void (*scale_nearest)(const dx_surface_desc_t *src, dx_rect_t sr,
                        const dx_surface_desc_t *dst, dx_rect_t dr);
  void (*gradient_v)(uint32_t *dst, int32_t dst_pitch, int32_t x, int32_t y,
                     int32_t w, int32_t h, uint32_t top_argb,
                     uint32_t bot_argb);
  void (*convert_565_to_8888)(uint32_t *dst, const uint16_t *src,
                              int32_t count);
  void (*dirty_reset)(void);
  bool (*dirty_add)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t max_w,
                    int32_t max_h);
  int (*dirty_count)(void);
  const dx_rect_t *(*dirty_get)(int idx);
} dxblit_api_t;

/* Funzioni interne esportabili/testabili singolarmente (test-by-func) */
void dxblit_copy_rect(uint32_t *dst, int32_t dst_pitch, const uint32_t *src,
                      int32_t src_pitch, int32_t w, int32_t h);
void dxblit_fill_rect(uint32_t *dst, int32_t dst_pitch, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t argb);
void dxblit_alpha_blend_rect(uint32_t *dst, int32_t dst_pitch,
                             const uint32_t *src, int32_t src_pitch, int32_t w,
                             int32_t h, uint8_t global_alpha);
void dxblit_scale_nearest(const dx_surface_desc_t *src, dx_rect_t sr,
                          const dx_surface_desc_t *dst, dx_rect_t dr);
void dxblit_gradient_v(uint32_t *dst, int32_t dst_pitch, int32_t x, int32_t y,
                       int32_t w, int32_t h, uint32_t top_argb,
                       uint32_t bot_argb);
void dxblit_convert_565_to_8888(uint32_t *dst, const uint16_t *src,
                                int32_t count);
void dxblit_dirty_reset(void);
bool dxblit_dirty_add(int32_t x, int32_t y, int32_t w, int32_t h, int32_t max_w,
                      int32_t max_h);
int dxblit_dirty_count(void);
const dx_rect_t *dxblit_dirty_get(int idx);

#endif
