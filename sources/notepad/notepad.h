#ifndef NOTEPAD_H
#define NOTEPAD_H

/**
 * notepad/notepad.h
 * Editor di testo Blocco Note (NOTEPAD.EXE) per NewOS V1 (64-bit)
 * Componente autonomo scorporato dal kernel core.
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include <types.h>

void app_notepad_launch(void);
void app_notepad_open_file(const char* title, const char* content);

#endif // NOTEPAD_H
