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
    char     filename[32];          // Es. "GLASS_LIGHT.THEMES"
    char     name[48];              // Es. "Windows 11 Fluent Glass Light"
    char     description[64];       // Es. "Vetro satinato frosted glass..."
    char     author[32];            // Es. "Morgan"
    char     version[16];           // Es. "1.0"
    char     style_engine[24];      // Es. "FluentGlassLight", "MicaLight", "Classic3D"

    // [Metrics] Geometria e Raggi
    uint8_t  corner_radius;         // Raggio angoli finestre (WindowRadius / CornerRadius, es. 0..16)
    uint8_t  title_height;          // Altezza barra del titolo (es. 22 o 26)
    uint8_t  border_width;          // Spessore bordo finestra (es. 1 o 2)
    bool     drop_shadow;           // Ombra morbida diffusa attiva
    uint8_t  shadow_spread;         // Ampiezza raggio ombra in pixel (es. 6..12)
    bool     specular_rim;          // Bordo superiore speculare illuminato
    bool     modern_controls;       // Controlli interni moderni flat/pillola (1) vs classic 3D bevel (0)
    uint8_t  control_radius;        // Raggio angoli generico controlli (fallback predefinito)
    uint8_t  control_border;        // Spessore bordo controlli (es. 1)
    uint8_t  button_radius;         // Raggio specifico angoli bottoni/pulsanti (ButtonRadius, es. 4..8)
    uint8_t  tab_radius;            // Raggio specifico schede e tab (TabRadius, es. 4..6)
    uint8_t  input_radius;          // Raggio caselle input, ricerca e indirizzi (InputRadius, es. 4..6)
    uint8_t  panel_radius;          // Raggio pannelli interni, sidebar e riquadri (PanelRadius, es. 6..10)
    uint8_t  taskbar_btn_radius;    // Raggio pillole finestre aperte sulla taskbar (TaskbarBtnRadius, es. 4..8)
    uint8_t  start_btn_radius;      // Raggio pulsante Start sulla taskbar (StartBtnRadius, es. 6..12)
    uint8_t  tray_radius;           // Raggio pillola orologio/vassoio tray (TrayRadius, es. 4..8)

    // [Borders] Curvatura e Raggi Bordo Indipendenti
    bool     border_follows_corner;         // 1 = il bordo finestra segue la curvatura, 0 = rettangolare 90 gradi
    uint8_t  border_radius;                 // Raggio perimetrale finestra (BorderRadius, default = corner_radius se border_follows_corner=1)
    bool     control_border_follows_radius; // 1 = i bordi dei controlli seguono i rispettivi raggi, 0 = rettangolari
    uint8_t  button_border_radius;          // Raggio bordo bottoni (ButtonBorderRadius)
    uint8_t  tab_border_radius;             // Raggio bordo schede (TabBorderRadius)
    uint8_t  input_border_radius;           // Raggio bordo campi input (InputBorderRadius)
    uint8_t  panel_border_radius;           // Raggio bordo pannelli interni (PanelBorderRadius)
    uint8_t  taskbar_border_radius;         // Raggio bordo pillole taskbar (TaskbarBorderRadius)
    uint8_t  start_btn_border_radius;       // Raggio bordo pulsante Start (StartBtnBorderRadius)
    uint8_t  tray_border_radius;            // Raggio bordo pillola orologio (TrayBorderRadius)

    // [Colors] Finestre e Canvas
    uint32_t desktop_color;         // Colore solido di sfondo/desktop in fallback
    uint32_t surface_base;          // Tinta base del corpo finestra (es. 0xFFFFFF)
    uint8_t  surface_alpha;         // Opacità superficie (0-255: <240 = vetro trasparente)
    uint32_t active_title;          // Sfondo barra del titolo attiva
    uint32_t active_title_text;     // Colore testo titolo attivo (es. 0x1A1A1A o 0xFFFFFF)
    uint32_t inactive_title;        // Sfondo barra del titolo inattiva
    uint32_t inactive_title_text;   // Colore testo titolo inattivo (es. 0x757575)
    uint32_t window_bg;             // Sfondo client area interna (es. 0xFFFFFF)
    uint32_t window_text;           // Testo client area interna (es. 0x1E1E1E)
    uint32_t btn_face;              // Sfondo pulsanti e schede tab (es. 0xF5F7FA)
    uint32_t btn_text;              // Testo pulsanti e schede (es. 0x1A1A1A)
    uint32_t btn_hover;             // Sfondo pulsante in stato hover (es. 0xE4E8EC)
    uint32_t border_color;          // Colore perimetrale finestra attiva (es. 0xE8E8E8)
    uint32_t border_inactive;       // Colore perimetrale finestra inattiva (es. 0xD0D0D0)
    uint32_t control_border_color;  // Colore bordi di pulsanti e input (es. 0xD4D4D8)
    uint32_t accent_color;          // Colore accento primario (es. 0x0078D4)
    uint32_t accent_text;           // Colore testo su sfondo accento (es. 0xFFFFFF)
    uint32_t selection_color;       // Colore barra di selezione attiva (es. 0xCCE8FF)
    uint32_t selection_text;        // Testo su barra di selezione (es. 0x004578)
    uint32_t close_hover_bg;        // Sfondo pulsante chiusura [x] in hover (es. 0xE81123)

    // [Taskbar] Barra delle Applicazioni
    uint32_t taskbar_color;         // Colore base barra delle applicazioni (es. 0xF3F3F7)
    uint8_t  taskbar_alpha;         // Opacità taskbar (0-255: 160 = traslucida)
    uint8_t  taskbar_height;        // Altezza taskbar in pixel (es. 28)
    uint32_t taskbar_border;        // Linea divisoria superiore (es. 0xE5E5E5)
    uint32_t start_btn_color;       // Colore sfondo pulsante Start
    uint32_t start_btn_text;        // Colore testo Start
    uint32_t active_tab_indicator;  // Indicatore finestra attiva (es. 0x0078D4)
    uint32_t tray_pill_color;       // Colore pillola orologio/vassoio

    // [Wallpaper & Suoni]
    int      theme_id;              // Identificativo motore stile (0=Classic, 1=Glass, 2=MicaLight, 3=MicaDark)
    char     wallpaper_file[64];    // Percorso file sfondo
    char     sound_startup[64];     // Percorso file suono di avvio
    char     sound_click[64];       // Percorso file suono click

    // [Advanced Render Scripts - Out-of-Kernel Engine]
    char     render_script[128];    // Script di rendering globale finestra
    char     titlebar_script[128];  // Script barra del titolo (specular arc, highlight)
    char     button_script[96];     // Script pulsanti e tab (pill glass, gloss, aura)
    char     panel_script[96];      // Script pannelli laterali (frosted noise, etched divider)
    char     progress_script[96];   // Script indicatori progresso / utilizzo disco
    char     selection_script[96];  // Script riquadri di selezione icone / liste
    char     taskbar_script[96];    // Script barra delle applicazioni
} theme_info_t;

enum {
    THEME_CMD_SCAN          = 1,
    THEME_CMD_SORT          = 2,
    THEME_CMD_EXEC_SCRIPT   = 3
};

typedef struct {
    const char*   dir_path;
    theme_info_t* themes;
    int           theme_count;
    int           max_themes;
    int           sort_order;

    // Parametri esecuzione script fuori dal kernel
    const char*   render_script;
    int32_t       render_x;
    int32_t       render_y;
    int32_t       render_w;
    int32_t       render_h;
    int32_t       render_radius;
    uint32_t      render_flags;
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
    bool (*render_script)(const char* script, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t flags);
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

// Recupera le proprietà visive del tema attualmente attivo nel Window Manager
const theme_info_t* theme_mgr_get_active(void);

// Applica direttamente le proprietà visive estese del tema
void theme_mgr_apply_props(const theme_info_t* theme);

// Esegue uno script di rendering avanzato tramite la libreria dinamica out-of-kernel THEMEMGR.DLL
bool theme_mgr_render_script(const char* script, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t flags);

#endif // GUI_THEME_MGR_H
