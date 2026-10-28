#ifndef SETTINGS_H
#define SETTINGS_H

/**
 * kernel/include/settings.h
 * Interfaccia e strutture per la modalità moderna Impostazioni del Pannello di Controllo
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include <types.h>
#include <screen_modules.h>
#include <gui/theme_mgr.h>

#define SETTINGS_SECTION_HOME        0
#define SETTINGS_SECTION_SYSTEM      1
#define SETTINGS_SECTION_NETWORK     2
#define SETTINGS_SECTION_PERSONAL    3
#define SETTINGS_SECTION_DEVICES     4
#define SETTINGS_SECTION_POWER       5
#define SETTINGS_SECTION_SECURITY    6
#define SETTINGS_NUM_SECTIONS        7

typedef enum {
    SETTINGS_ACTION_NONE = 0,
    SETTINGS_ACTION_CLASSIC_MODE,       // Torna alla modalità classica del Pannello di Controllo
    SETTINGS_ACTION_LAUNCH_DISPLAY,     // Lancia applet Schermo (DISPLAY.CPL)
    SETTINGS_ACTION_LAUNCH_SOUND,       // Lancia applet Suoni (MMSYS.CPL)
    SETTINGS_ACTION_LAUNCH_MOUSE,       // Lancia applet Mouse (MAIN.CPL)
    SETTINGS_ACTION_LAUNCH_DEVMGMT,     // Lancia Gestione Periferiche (DEVMGMT.MSC)
    SETTINGS_ACTION_LAUNCH_POWER,       // Lancia Risparmio Energia (POWERCFG.CPL)
    SETTINGS_ACTION_LAUNCH_ABOUT,       // Lancia Info Sistema (WinVer / SYSDM.CPL)
    SETTINGS_ACTION_LAUNCH_REGEDIT,     // Lancia Editor Registro (REGEDIT.EXE)
    SETTINGS_ACTION_APPLY_THEME_0,      // Applica tema 0 (Classic)
    SETTINGS_ACTION_APPLY_THEME_1,      // Applica tema 1 (Glass Light)
    SETTINGS_ACTION_APPLY_THEME_2,      // Applica tema 2 (Mica Light)
    SETTINGS_ACTION_APPLY_THEME_3       // Applica tema 3 (Mica Dark)
} settings_action_t;

typedef struct {
    int32_t active_section;             // Sezione attiva corrente (0..6)
    char    search_text[32];            // Filtro di ricerca digitato
    bool    search_focus;               // Barra di ricerca attiva con focus
    int32_t hovered_item;               // Elemento evidenziato al passaggio del mouse
    int32_t scroll_y;                   // Offset scorrimento verticale
} settings_state_t;

#define SETTINGS_CMD_INIT    1
#define SETTINGS_CMD_RENDER  2
#define SETTINGS_CMD_MOUSE   3
#define SETTINGS_CMD_KEY     4

typedef struct {
    int32_t cx, cy, cw, ch;
    int32_t mouse_x, mouse_y;
    bool is_glass;
    uint32_t bg_color;
    settings_state_t* state;
    const theme_info_t* cur_theme;
} settings_render_params_t;

typedef struct {
    int32_t cx, cy, cw, ch;
    int32_t mx, my;
    bool clicked;
    settings_state_t* state;
} settings_mouse_params_t;

typedef struct {
    char key;
    settings_state_t* state;
} settings_key_params_t;

typedef int (*settings_entry_fn)(const kernel_api_t* api, int cmd, void* params);

// API Client per il kernel / explorer
void settings_loader_init(void);
bool settings_is_available(void);
void settings_render(const kernel_api_t* api, int32_t cx, int32_t cy, int32_t cw, int32_t ch,
                     int32_t mx, int32_t my, bool is_glass, uint32_t bg_color,
                     settings_state_t* state, const theme_info_t* cur_theme);
settings_action_t settings_handle_mouse(const kernel_api_t* api, int32_t cx, int32_t cy, int32_t cw, int32_t ch,
                                        int32_t mx, int32_t my, bool clicked, settings_state_t* state);
bool settings_handle_key(const kernel_api_t* api, char key, settings_state_t* state);

#endif // SETTINGS_H
