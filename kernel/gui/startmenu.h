#ifndef GUI_STARTMENU_H
#define GUI_STARTMENU_H

#include <types.h>

// Modalita' Menu Start configurabile dal CPL Personalizzazione
#define STARTMENU_MODE_CLASSIC 0
#define STARTMENU_MODE_MODERN  1

void startmenu_draw(int32_t sw, int32_t sh);
// Ritorna: 0 = non gestito / fuori menu, 1 = gestito (rimani aperto o chiudi), 2 = chiudi + esegui logout
int  startmenu_handle_mouse(int32_t sw, int32_t sh, int32_t mx, int32_t my, bool clicked);
bool startmenu_handle_key(char c);

void wm_set_start_menu_mode(int mode);
int  wm_get_start_menu_mode(void);

#endif // GUI_STARTMENU_H
