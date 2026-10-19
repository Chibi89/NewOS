#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <types.h>

typedef struct {
    int32_t x;
    int32_t y;
    bool left_button;
    bool right_button;
    bool middle_button;
} mouse_state_t;

void mouse_init(uint32_t screen_width, uint32_t screen_height);
void mouse_set_bounds(uint32_t screen_width, uint32_t screen_height);
void mouse_get_state(mouse_state_t* state);
void mouse_inject_event(int32_t dx, int32_t dy, bool left, bool right, bool middle);

#endif // DRIVERS_MOUSE_H
