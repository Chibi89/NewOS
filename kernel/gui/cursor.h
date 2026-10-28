/**
 * kernel/gui/cursor.h
 * Specifiche, Stati di Sistema e Interfacce per Cursori Personalizzati NewOS V1 (64-bit)
 * Ispirato all'architettura standard dei cursori di Windows (.CUR / .ANI)
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#ifndef GUI_CURSOR_H
#define GUI_CURSOR_H

#include <types.h>

#define CURSOR_BMP_SIZE        32
#define CURSOR_MAX_SCHEMES     16
#define CURSOR_MAX_SINGLES     256
#define CURSOR_DEFAULT_PATH    "C:/NewOS/Resource/cursor"

/**
 * Stati di sistema standard del cursore (Windows Cursor Roles)
 */
typedef enum {
    CURSOR_STATE_NORMAL = 0,    // Normal Select (Freccia standard)
    CURSOR_STATE_HELP,          // Help Select (? affiancato alla freccia)
    CURSOR_STATE_WORKING,       // Working in Background (Freccia con clessidra/rotella)
    CURSOR_STATE_BUSY,          // Busy / Wait (Rotella animata / Clessidra)
    CURSOR_STATE_CROSS,         // Precision Select (Mirino a croce)
    CURSOR_STATE_IBEAM,         // Text Select (Cursore I-Beam)
    CURSOR_STATE_PEN,           // Handwriting / Pen (Penna)
    CURSOR_STATE_UNAVAIL,       // Unavailable (Segnale di divieto)
    CURSOR_STATE_RESIZE_NS,     // Vertical Resize (Frecce su/giu)
    CURSOR_STATE_RESIZE_EW,     // Horizontal Resize (Frecce sinistra/destra)
    CURSOR_STATE_RESIZE_NWSE,   // Diagonal Resize 1 (Nord-Ovest / Sud-Est)
    CURSOR_STATE_RESIZE_NESW,   // Diagonal Resize 2 (Nord-Est / Sud-Ovest)
    CURSOR_STATE_MOVE,          // Move / Size All (4 frecce direzionali)
    CURSOR_STATE_UP,            // Alternate Select / Up Arrow (Freccia verticale)
    CURSOR_STATE_LINK,          // Link Select (Mano con indice / Hand)
    CURSOR_STATE_PIN,           // Location Select (Spillo / Pin)
    CURSOR_STATE_PERSON,        // Person Select (Contatto / Persona)
    CURSOR_STATE_COUNT          // Totale stati standard di sistema (17)
} cursor_system_state_t;

/**
 * Singola risorsa cursore decodificata con bitmap ARGB 32-bit e hotspot
 */
typedef struct {
    char                  filename[48];
    char                  fullpath[144];
    cursor_system_state_t role;
    uint16_t              hotspot_x;
    uint16_t              hotspot_y;
    uint16_t              width;
    uint16_t              height;
    bool                  is_valid;
    bool                  is_animated;
    uint16_t              frame_count;
    uint32_t              pixels[CURSOR_BMP_SIZE * CURSOR_BMP_SIZE];
} cursor_resource_t;

/**
 * Metadati compatti per un cursore singolo (file sciolto nella radice)
 */
typedef struct {
    char                  filename[48];
    char                  fullpath[144];
    cursor_system_state_t role;
    bool                  is_valid;
} cursor_single_info_t;

/**
 * Metadati per uno schema/gruppo di cursori (es. "Attune", "Glass Cursors", "Windows Aero", ecc.)
 */
typedef struct {
    char     name[48];
    char     path[144];
    uint32_t recognized_count;
    bool     is_active;
    bool     is_directory_scheme; // true = sottocartella, false = schema radice da file singoli
} cursor_scheme_info_t;

/**
 * Contesto di stato globale del gestore cursori (allocato nel kernel space sicuro)
 */
typedef struct {
    cursor_scheme_info_t schemes[CURSOR_MAX_SCHEMES];
    int                  scheme_count;
    int                  active_scheme_idx;
    cursor_resource_t    active_resources[CURSOR_STATE_COUNT];
    bool                 active_res_loaded[CURSOR_STATE_COUNT];
    cursor_single_info_t single_cursors[CURSOR_MAX_SINGLES];
    int                  single_count;
    char                 scan_root[128];
} cursor_context_t;

/**
 * Parametri di comunicazione con la libreria out-of-kernel CURSORMGR.DLL
 */
typedef struct {
    cursor_context_t*     ctx;
    const char*           root_dir;
    int                   scheme_index;
    cursor_system_state_t state_role;
    int32_t               draw_x;
    int32_t               draw_y;
    int*                  out_count;
} cursor_cmd_params_t;

/**
 * Comandi supportati dall'entry point di CURSORMGR.DLL
 */
enum {
    CURSOR_CMD_INIT             = 1,
    CURSOR_CMD_SCAN             = 2,
    CURSOR_CMD_SET_ACTIVE_SCHEME= 3,
    CURSOR_CMD_DRAW             = 4
};

/**
 * Firma dell'entry point della libreria CURSORMGR.DLL
 */
typedef int (*cursormgr_entry_fn)(const void* api, int cmd, cursor_cmd_params_t* params);

// --- Funzioni API del Kernel / Loader Client ---
bool cursor_loader_init(void);
bool cursor_mgr_draw(int32_t x, int32_t y, cursor_system_state_t state);
int  cursor_mgr_get_scheme_count(void);
const cursor_scheme_info_t* cursor_mgr_get_scheme_info(int index);
bool cursor_mgr_set_active_scheme(int index);
int  cursor_mgr_get_active_scheme_index(void);
int  cursor_mgr_get_single_count(void);
const cursor_single_info_t* cursor_mgr_get_single_info(int index);
const char* cursor_mgr_get_state_name(cursor_system_state_t state);

#endif // GUI_CURSOR_H
