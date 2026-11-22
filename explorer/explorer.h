#ifndef EXPLORER_H
#define EXPLORER_H

/**
 * explorer/explorer.h
 * Esplora Risorse (File Explorer) per NewOS V1 (64-bit)
 * Componente autonomo scorporato dal kernel core.
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include <types.h>
#include <gui/wm.h>
#include <gui/icons.h>
#include <gui/context_menu.h>
#include <settings.h>

#define MAX_EXPLORER_ITEMS 64
#define MAX_PATH_HISTORY   16

typedef enum {
    EXPLORER_MODE_THIS_PC = 0,    // Risorse del computer (vista unita)
    EXPLORER_MODE_DRIVE_C,        // Navigazione radice Disco C:
    EXPLORER_MODE_DRIVE_D,        // Navigazione radice CD-ROM D:
    EXPLORER_MODE_DRIVE_A,        // Navigazione Floppy A:
    EXPLORER_MODE_DRIVE_U,        // Navigazione Unità Rimovibile USB U:
    EXPLORER_MODE_FOLDER,         // Cartella specifica del filesystem
    EXPLORER_MODE_RECYCLE_BIN,    // Cestino (elementi pronti da eliminare)
    EXPLORER_MODE_CONTROL_PANEL,  // Pannello di controllo (Classico, primario)
    EXPLORER_MODE_SETTINGS        // Nuova modalità Impostazioni (Moderna, scorporata)
} explorer_mode_t;

typedef enum {
    EXPLORER_MENU_NONE = 0,
    EXPLORER_MENU_FILE,
    EXPLORER_MENU_EDIT,
    EXPLORER_MENU_VIEW,
    EXPLORER_MENU_TOOLS,
    EXPLORER_MENU_HELP
} explorer_menu_t;

typedef enum {
    EXPLORER_VIEW_EXTRA_LARGE = 0, // Icone molto grandi
    EXPLORER_VIEW_LARGE,           // Icone grandi
    EXPLORER_VIEW_MEDIUM,          // Icone medie (default)
    EXPLORER_VIEW_SMALL,           // Icone piccole
    EXPLORER_VIEW_LIST,            // Elenco
    EXPLORER_VIEW_DETAILS,         // Dettagli multi-colonna
    EXPLORER_VIEW_TILES,           // Riquadri
    EXPLORER_VIEW_CONTENT,         // Contenuto
    EXPLORER_VIEW_COUNT
} explorer_view_mode_t;

typedef struct {
    char name[64];
    char desc[48];
    icon_id_t icon_id;
    uint64_t size_bytes;
    uint32_t mtime;
    uint16_t permissions;
    bool is_folder;
    bool is_drive;
    char content[256]; // Contenuto testuale se file di testo
} explorer_item_t;

typedef struct {
    explorer_mode_t mode;
    char current_path[128];
    char folder_name[128];
    
    // Lista elementi contenuti
    explorer_item_t items[MAX_EXPLORER_ITEMS];
    int num_items;
    int selected_item;
    
    // Scorrimento verticale (Scrollbar)
    int scroll_row;
    
    // Barra dei Menu a tendina (Top Menus)
    explorer_menu_t active_menu;
    
    // Menu Contestuale Tasto Destro (Modulo indipendente autoaggiornante)
    context_menu_t ctx_menu;
    int ctx_target_idx;
    
    // Sidebar: Albero Cartelle (Folder Tree) vs Web View
    bool show_folder_tree;
    
    // Filtro di Ricerca (Cerca)
    bool search_active;
    char search_filter[32];
    
    // Barra Indirizzi interattiva
    bool address_editing;
    char address_buf[128];
    
    // Finestra di dialogo Proprietà
    bool show_properties;
    explorer_item_t props_item;
    char props_path[160];
    
    // Opzioni di visualizzazione e ordinamento
    bool show_toolbar;
    bool show_statusbar;
    int sort_mode; // 0=default, 1=Nome, 2=Dimensione, 3=Tipo
    
    // Cronologia navigazione
    char history[MAX_PATH_HISTORY][128];
    explorer_mode_t history_mode[MAX_PATH_HISTORY];
    int history_index;
    int history_count;
    
    // Stato click per doppio click
    uint64_t last_click_time;
    int last_clicked_idx;

    // Coordinate pulsante sidebar dinamico (Web View)
    int32_t sidebar_btn_y;
    int32_t sidebar_btn_h;

    // Coordinate link commutazione modalità (Web View)
    int32_t sidebar_mode_link_y;
    int32_t sidebar_mode_link_h;

    // Stato della modalità moderna Impostazioni (scorporata in SETTINGS.DLL)
    settings_state_t settings_state;

    // Banner stato / sicurezza ACL (COMCTL_BANNER)
    char status_banner[96];
    bool status_banner_err;

    // Modalità di visualizzazione Windows Explorer (CTRL + Rotella Mouse)
    explorer_view_mode_t view_mode;
    bool show_extensions;
    bool show_hidden;
    bool show_preview_pane;
    bool show_checkboxes;
    bool view_submenu_open;
    bool sort_desc;
    bool item_selected[MAX_EXPLORER_ITEMS];
    int selected_count;
    uint64_t selected_bytes;
} explorer_state_t;

// Funzioni pubbliche di apertura
void app_explorer_open_this_pc(void);
void app_explorer_open_drive_c(void);
void app_explorer_open_drive_d(void);
void app_explorer_open_drive_a(void);
void app_explorer_open_drive_u(void);
void app_explorer_open_folder(const char* folder_name);
void app_explorer_open_recycle_bin(void);
void app_control_panel_open(void);
void app_settings_open(void);

#endif // EXPLORER_H
