#ifndef GUI_THEME_MGR_H
#define GUI_THEME_MGR_H

#include <types.h>

#define MAX_SYSTEM_THEMES 16

typedef enum {
    THEME_SORT_ALPHA = 0,       // Alfabetico per Nome visualizzato (A-Z)
    THEME_SORT_STYLE,           // Per Engine stilistico (Mica -> Glass -> Classic)
    THEME_SORT_ACTIVE_FIRST     // Tema attualmente attivo come primo
} theme_sort_order_t;

typedef struct {
    char filename[32];          // Es. "MICA_DARK.THEMES"
    char name[48];              // Es. "Windows 11 Mica Dark"
    char description[64];       // Es. "Tema scuro moderno basato su materiale Mica"
    char author[32];            // Es. "Morgan"
    char style_engine[24];      // Es. "MicaDark", "FluentGlassLight", "Classic3D"
    uint32_t desktop_color;     // Fallback colore desktop
    uint32_t surface_base;      // Colore base finestra
    uint32_t active_title;      // Colore barra del titolo
    uint32_t accent_color;      // Colore accento primario
    uint32_t border_color;      // Colore perimetrale finestra
    uint8_t  surface_alpha;     // Opacità finestra (0-255)
    uint8_t  corner_radius;     // Raggio angoli (0 per 3D, 8 per Win11)
    uint8_t  border_width;      // Spessore bordo
    bool     drop_shadow;       // Ombra diffusa attiva
    int      theme_id;          // 0 = Classic 3D, 1 = Modern/Mica/LiquidGlass
    char     wallpaper_file[64];// File sfondo consigliato
} theme_info_t;

enum {
    THEME_CMD_SCAN = 1,
    THEME_CMD_SORT = 2
};

typedef struct {
    const char*   dir_path;
    theme_info_t* themes;
    int           theme_count;
    int           max_themes;
    int           sort_order;
} theme_mgr_cmd_params_t;

typedef int (*thememgr_entry_fn)(const void* api, int cmd, theme_mgr_cmd_params_t* params);

// Tabella esportazioni della libreria dinamica THEMEMGR.DLL
typedef struct {
    int  (*init)(const char* custom_dir);
    int  (*scan)(const char* dir_path);
    void (*sort)(theme_sort_order_t order);
    int  (*get_count)(void);
    const theme_info_t* (*get)(int index);
    int  (*find_by_filename)(const char* filename);
    int  (*find_by_id)(int theme_id);
    bool (*apply)(int index);
    const char* (*get_scanned_dir)(void);
} theme_mgr_exports_t;

// Inizializza il gestore temi e scansiona la cartella specificata (se NULL, usa "C:/NewOS/Resource/Themes")
int theme_mgr_init(const char* custom_dir);

// Scansiona la directory e popola dinamicamente tutti i file .THEMES e .THM
int theme_mgr_scan(const char* dir_path);

// Ordina automaticamente i temi caricati secondo il criterio richiesto
void theme_mgr_sort(theme_sort_order_t order);

// Restituisce il numero di temi validi trovati
int theme_mgr_get_count(void);

// Restituisce il tema all'indice specificato (NULL se fuori intervallo)
const theme_info_t* theme_mgr_get(int index);

// Trova l'indice del tema per nome file (es. "MICA_DARK.THEMES") o -1 se non trovato
int theme_mgr_find_by_filename(const char* filename);

// Trova l'indice del tema per theme_id (0: Classic, 1: Modern) o -1 se non trovato
int theme_mgr_find_by_id(int theme_id);

// Applica il tema al sistema (aggiorna Window Manager e stili attivi)
bool theme_mgr_apply(int index);

// Restituisce la cartella attualmente scansionata
const char* theme_mgr_get_scanned_dir(void);

#endif // GUI_THEME_MGR_H
