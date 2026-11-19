#ifndef GUI_UISTYLE_LOADER_H
#define GUI_UISTYLE_LOADER_H

#include <include/uistyle.h>
#include <types.h>

// Inizializza leggendo dal Registro il profilo UI Style attivo (0=Standard, 1=Copenhagen)
void uistyle_init(void);

// Restituisce l'ID dello stile UI attivo
int uistyle_get_active(void);

// Imposta e applica un nuovo UI Style caricando il relativo modulo da C:\NewOS\Resources\UIStyle
bool uistyle_set_active(int style_id);

// Disegna gli elementi desktop del modulo UI Style attivo (barra superiore, orb, taskbar, menu)
void uistyle_draw_desktop(uint32_t sw, uint32_t sh);

// Inoltra gli eventi mouse desktop al modulo UI Style attivo (ritorna true se gestito)
bool uistyle_handle_desktop_mouse(int32_t mx, int32_t my, bool clicked);

// Apre la finestra Risorse del PC nello stile UI attivo (ritorna false se gestito dallo standard)
bool uistyle_open_explorer(const char* path);

// Apre i pannelli modulari My Scraps e Note nello stile UI attivo
void uistyle_open_workspace(void);

#endif // GUI_UISTYLE_LOADER_H
