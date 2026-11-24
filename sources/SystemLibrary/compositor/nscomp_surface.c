/**
 * SystemLibrary/compositor/nscomp_surface.c
 * Gestione Surface Offscreen per caching finestre senza ridisegno completo
 */
#include "nscomp.h"

static nsc_surface_t g_surfaces[NSCOMP_MAX_SURFACES];

int nsc_surface_init(int id, int32_t w, int32_t h, uint32_t *backing_buf) {
  if (id < 0 || id >= NSCOMP_MAX_SURFACES || w <= 0 || h <= 0 || !backing_buf)
    return -1;
  g_surfaces[id].in_use = true;
  g_surfaces[id].dirty = true;
  g_surfaces[id].w = w;
  g_surfaces[id].h = h;
  g_surfaces[id].pixels = backing_buf;
  return id;
}

void nsc_surface_free(int id) {
  if (id < 0 || id >= NSCOMP_MAX_SURFACES)
    return;
  g_surfaces[id].in_use = false;
  g_surfaces[id].dirty = false;
  g_surfaces[id].pixels = 0;
}

void nsc_surface_mark_dirty(int id) {
  if (id >= 0 && id < NSCOMP_MAX_SURFACES && g_surfaces[id].in_use)
    g_surfaces[id].dirty = true;
}

void nsc_surface_clear_dirty(int id) {
  if (id >= 0 && id < NSCOMP_MAX_SURFACES && g_surfaces[id].in_use)
    g_surfaces[id].dirty = false;
}

bool nsc_surface_is_dirty(int id) {
  if (id < 0 || id >= NSCOMP_MAX_SURFACES || !g_surfaces[id].in_use)
    return false;
  return g_surfaces[id].dirty;
}
