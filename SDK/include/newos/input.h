/**
 * newos/input.h — Namespace Input (Tastiera, Mouse, Keycode)
 */
#ifndef NEWOS_SDK_INPUT_H
#define NEWOS_SDK_INPUT_H

#include "types.h"
#include <nsrt.h>

#define NS_KEY_ENTER     '\n'
#define NS_KEY_TAB       '\t'
#define NS_KEY_BACKSPACE '\b'
#define NS_KEY_ESC       0x1B

typedef enum {
    NS_MOUSE_NONE   = 0,
    NS_MOUSE_LEFT   = NS_BIT(0),
    NS_MOUSE_RIGHT  = NS_BIT(1),
    NS_MOUSE_MIDDLE = NS_BIT(2)
} ns_mouse_btn_t;

static inline uint32_t ns_mouse_buttons_mask(const ns_mouse_state_t* st) {
    if (!st) return NS_MOUSE_NONE;
    uint32_t m = 0;
    if (st->left_button)   m |= NS_MOUSE_LEFT;
    if (st->right_button)  m |= NS_MOUSE_RIGHT;
    if (st->middle_button) m |= NS_MOUSE_MIDDLE;
    return m;
}

#endif /* NEWOS_SDK_INPUT_H */
