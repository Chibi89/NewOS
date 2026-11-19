#ifndef KERNEL_UISTYLE_H
#define KERNEL_UISTYLE_H

#include <types.h>

typedef enum {
    UISTYLE_STANDARD   = 0,
    UISTYLE_COPENHAGEN = 1
} uistyle_id_t;

typedef enum {
    UISTYLE_CMD_INIT           = 0,
    UISTYLE_CMD_DRAW_DESKTOP   = 1,
    UISTYLE_CMD_MOUSE_DESKTOP  = 2,
    UISTYLE_CMD_OPEN_EXPLORER  = 3,
    UISTYLE_CMD_OPEN_WORKSPACE = 4
} uistyle_cmd_t;

typedef struct {
    uint32_t    screen_w;
    uint32_t    screen_h;
    int32_t     mouse_x;
    int32_t     mouse_y;
    bool        clicked;
    bool        handled;
    const char* path_arg;
} uistyle_params_t;

#endif // KERNEL_UISTYLE_H

