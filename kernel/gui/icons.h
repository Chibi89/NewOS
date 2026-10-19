// Autogenerato da tools/process_icons.js - NON MODIFICARE A MANO
#ifndef GUI_ICONS_H
#define GUI_ICONS_H

#include <types.h>

typedef enum {
    ICON_THIS_PC             = 0,
    ICON_CESTINO             = 1,
    ICON_CESTINO_PIENO       = 2,
    ICON_FOLDER              = 3,
    ICON_FOLDER_VOID         = 4,
    ICON_DOCUMENT            = 5,
    ICON_CONTROL_PANEL       = 6,
    ICON_SOUND               = 7,
    ICON_RUN                 = 8,
    ICON_SEARCH              = 9,
    ICON_DESKTOP             = 10,
    ICON_HD                  = 11,
    ICON_FLOPPY              = 12,
    ICON_CD                  = 13,
    ICON_NET                 = 14,
    ICON_INFO                = 15,
    ICON_KEYBOARD            = 16,
    ICON_MOUSE               = 17,
    ICON_REGISTRY            = 18,
    ICON_ACCESSIBILITY       = 19,
    ICON_DATABASE            = 20,
    ICON_FONT                = 21,
    ICON_IMAGE               = 22,
    ICON_IMAGE_PNG           = 23,
    ICON_PLAY                = 24,
    ICON_ENERGY              = 25,
    ICON_MAINTENANCE         = 26,
    ICON_NET_ON              = 27,
    ICON_NET_OFF             = 28,
    NUM_ICONS                = 29
} icon_id_t;

// Inizializza il repository icone caricando il pacchetto dal disco (LBA 1024)
void icons_init(void);

// Restituisce il puntatore al buffer pixel ARGB 32-bit (32x32 = 1024 dword, 16x16 = 256 dword)
const uint32_t* icon_get_32(icon_id_t id);
const uint32_t* icon_get_16(icon_id_t id);

// Disegna un'icona con trasparenza alpha nativa (per-pixel alpha blending)
void gfx_draw_icon_32(int32_t x, int32_t y, icon_id_t id);
void gfx_draw_icon_16(int32_t x, int32_t y, icon_id_t id);

// Funzioni di rendering generico di sprite ARGB con canale alpha
void gfx_draw_sprite_32_alpha(int32_t x, int32_t y, const uint32_t* pixels);
void gfx_draw_sprite_16_alpha(int32_t x, int32_t y, const uint32_t* pixels);

#endif // GUI_ICONS_H
