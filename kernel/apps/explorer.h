#ifndef APPS_EXPLORER_H
#define APPS_EXPLORER_H

#include <types.h>
#include <gui/wm.h>
#include <gui/icons.h>

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
    EXPLORER_MODE_CONTROL_PANEL   // Pannello di controllo
} explorer_mode_t;

typedef enum {
    EXPLORER_MENU_NONE = 0,
    EXPLORER_MENU_FILE,
    EXPLORER_MENU_EDIT,
    EXPLORER_MENU_VIEW,
    EXPLORER_MENU_TOOLS,
    EXPLORER_MENU_HELP
} explorer_menu_t;

typedef struct {
    char name[64];
    char desc[48];
    icon_id_t icon_id;
    uint32_t size_bytes;
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
    
    // Menu Contestuale Tasto Destro (Context Menu)
    bool ctx_open;
    bool ctx_is_item;
    int32_t ctx_x;
    int32_t ctx_y;
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
    
    // Cronologia navigazione
    char history[MAX_PATH_HISTORY][128];
    explorer_mode_t history_mode[MAX_PATH_HISTORY];
    int history_index;
    int history_count;
    
    // Stato click per doppio click
    uint64_t last_click_time;
    int last_clicked_idx;
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

#endif // APPS_EXPLORER_H
