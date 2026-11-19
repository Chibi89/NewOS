#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <types.h>

// Codici di controllo e scorciatoie globali di sistema
#define KEY_CTRL_C     ((char)0x03)
#define KEY_CTRL_N     ((char)0x0E)
#define KEY_CTRL_O     ((char)0x0F)
#define KEY_CTRL_S     ((char)0x13)
#define KEY_CTRL_V     ((char)0x16)
#define KEY_CTRL_X     ((char)0x18)
#define KEY_ALT_TAB    ((char)0x1C)
#define KEY_ALT_F4     ((char)0x1D)
#define KEY_WIN_START  ((char)0x1E)
#define KEY_DEL        ((char)0x7F)

void keyboard_init(void);
bool keyboard_has_char(void);
char keyboard_get_char(void);
void keyboard_inject_char(char c);

#endif // DRIVERS_KEYBOARD_H
