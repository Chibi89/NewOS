/**
 * SystemLibrary/motion/nsmotion_timeline.c
 * Gestione tracce temporali (timeline) e fisica a molla per scroll inerziale
 */
#include "nsmotion.h"

static bool g_nsm_enabled = true;
static nsm_track_t g_tracks[NSM_MAX_TRACKS];

void nsm_set_enabled(bool en) { g_nsm_enabled = en; }
bool nsm_is_enabled(void) { return g_nsm_enabled; }

int nsm_track_start(int32_t from, int32_t to, uint32_t duration_ms,
                    nsm_ease_t ease) {
  for (int i = 0; i < NSM_MAX_TRACKS; i++) {
    if (!g_tracks[i].active) {
      g_tracks[i].active = true;
      g_tracks[i].from = from;
      g_tracks[i].to = to;
      g_tracks[i].current = (!g_nsm_enabled || duration_ms == 0) ? to : from;
      g_tracks[i].elapsed_ms = 0;
      g_tracks[i].duration_ms = g_nsm_enabled ? duration_ms : 0;
      g_tracks[i].ease = ease;
      return i;
    }
  }
  return -1;
}

bool nsm_track_step(int id, uint32_t dt_ms, int32_t *out_val) {
  if (id < 0 || id >= NSM_MAX_TRACKS || !g_tracks[id].active)
    return false;
  nsm_track_t *tr = &g_tracks[id];
  if (!g_nsm_enabled || tr->duration_ms == 0) {
    tr->current = tr->to;
    tr->active = false;
    if (out_val)
      *out_val = tr->to;
    return false;
  }
  tr->elapsed_ms += dt_ms;
  if (tr->elapsed_ms >= tr->duration_ms) {
    tr->current = tr->to;
    tr->active = false;
    if (out_val)
      *out_val = tr->to;
    return false;
  }
  int32_t t_q16 = (int32_t)(((int64_t)tr->elapsed_ms << 16) / tr->duration_ms);
  int32_t e_q16 = nsm_ease_q16(tr->ease, t_q16);
  tr->current = nsm_lerp_i32(tr->from, tr->to, e_q16);
  if (out_val)
    *out_val = tr->current;
  return true;
}

void nsm_spring_step(nsm_spring_t *sp, int32_t stiffness_q8,
                     int32_t damping_q8) {
  if (!sp)
    return;
  if (!g_nsm_enabled) {
    sp->pos = sp->target;
    sp->vel = 0;
    return;
  }
  int32_t force = ((sp->target - sp->pos) * stiffness_q8) >> 8;
  sp->vel = ((sp->vel + force) * damping_q8) >> 8;
  sp->pos += sp->vel;
  if (sp->pos == sp->target && sp->vel > -2 && sp->vel < 2)
    sp->vel = 0;
}
