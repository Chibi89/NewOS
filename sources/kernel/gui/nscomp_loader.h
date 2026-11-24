#ifndef NSCOMP_LOADER_H
#define NSCOMP_LOADER_H

#include <types.h>
#include "../../SystemLibrary/compositor/nscomp.h"

const nscomp_api_t *nscomp_get_api(void);
void nscomp_apply_window_refraction(int32_t x, int32_t y, int32_t w, int32_t h,
                                    int32_t rim_px, int32_t strength_px);
const void *nsmotion_get_api(void);

#endif
