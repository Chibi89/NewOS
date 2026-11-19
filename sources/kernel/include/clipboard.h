/**
 * kernel/include/clipboard.h
 * Appunti di Sistema Globali (System Clipboard) per testo e stack di file
 * Condivisi tra Blocco Note, File Explorer, Prompt dei Comandi e Librerie .DLL
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <types.h>

#define CLIPBOARD_MAX_TEXT   2048
#define CLIPBOARD_MAX_FILES  16
#define CLIPBOARD_MAX_PATH   160

void        clipboard_set_text(const char* text);
const char* clipboard_get_text(void);
bool        clipboard_has_text(void);

bool        clipboard_push_file(const char* filepath, bool is_cut);
int         clipboard_get_file_count(void);
const char* clipboard_get_file(int index, bool* out_is_cut);
void        clipboard_clear_files(void);

#endif // CLIPBOARD_H

