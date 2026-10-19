#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <types.h>

void keyboard_init(void);
bool keyboard_has_char(void);
char keyboard_get_char(void);
void keyboard_inject_char(char c);

#endif // DRIVERS_KEYBOARD_H
