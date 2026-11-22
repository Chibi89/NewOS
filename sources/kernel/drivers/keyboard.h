#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <types.h>

// Codici di controllo e scorciatoie globali di sistema
#define KEY_CTRL_C     ((char)0x03)
#define KEY_CTRL_F     ((char)0x06)
#define KEY_CTRL_N     ((char)0x0E)
#define KEY_CTRL_O     ((char)0x0F)
#define KEY_CTRL_S     ((char)0x13)
#define KEY_CTRL_T     ((char)0x14)
#define KEY_CTRL_V     ((char)0x16)
#define KEY_CTRL_W     ((char)0x17)
#define KEY_CTRL_X     ((char)0x18)
#define KEY_CTRL_Z     ((char)0x1A)
#define KEY_ALT_TAB    ((char)0x1C)
#define KEY_ALT_F4     ((char)0x1D)
#define KEY_WIN_START  ((char)0x1E)
#define KEY_DEL        ((char)0x7F)
#define KEY_WHEEL_UP   ((char)0xF1)
#define KEY_WHEEL_DOWN ((char)0xF2)
#define KEY_TASKMGR    ((char)0xF3)
#define KEY_WIN_E      ((char)0xF4)
#define KEY_WIN_R      ((char)0xF5)
#define KEY_WIN_D      ((char)0xF6)
#define KEY_WIN_L      ((char)0xF7)
#define KEY_ARROW_UP   ((char)0xF8)
#define KEY_ARROW_DOWN ((char)0xF9)

void keyboard_init(void);
bool keyboard_has_char(void);
char keyboard_get_char(void);
void keyboard_inject_char(char c);
#ifndef keyboard_is_ctrl_pressed
bool keyboard_is_ctrl_pressed(void);
#endif

#endif // DRIVERS_KEYBOARD_H
