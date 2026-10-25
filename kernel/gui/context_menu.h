#ifndef GUI_CONTEXT_MENU_H
#define GUI_CONTEXT_MENU_H

#include <types.h>

#define CTX_MENU_MAX_ITEMS 14
#define CTX_MENU_MAX_LABEL 48

typedef enum {
    CTX_ACTION_NONE = 0,
    CTX_ACTION_OPEN_PRIMARY,   // Azione determinata dal Registro (es. Riproduci audio, Modifica config, Imposta sfondo, ecc.)
    CTX_ACTION_CUT,
    CTX_ACTION_COPY,
    CTX_ACTION_PASTE,
    CTX_ACTION_DELETE,
    CTX_ACTION_RENAME,
    CTX_ACTION_PROPERTIES,
    // Azioni per sfondo / cartella vuota
    CTX_ACTION_NEW_FOLDER,
    CTX_ACTION_NEW_FILE,
    CTX_ACTION_REFRESH,
    CTX_ACTION_DRIVE_PROPS
} context_menu_action_t;

typedef struct {
    char label[CTX_MENU_MAX_LABEL];
    context_menu_action_t action;
    bool is_separator;
    bool is_disabled;
    bool is_default;
    char command[32]; // "PLAYER.EXE", "NOTEPAD.EXE", "WALLPAPER", ecc.
} context_menu_item_t;

typedef struct {
    bool is_open;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    context_menu_item_t items[CTX_MENU_MAX_ITEMS];
    int num_items;

    char target_name[64];
    char target_path[160];
    bool is_item;
    bool is_folder;
} context_menu_t;

// Inizializzazione e ricaricamento associazioni da REGISTRY.DAT
void context_menu_init(void);
void context_menu_reload_from_registry(void);

// Interrogazione estensione nel registro (restituisce true se trovata in HKEY_CLASSES_ROOT)
bool context_menu_query_extension(const char* ext, char* out_desc, size_t desc_sz,
                                   char* out_action, size_t action_sz,
                                   char* out_cmd, size_t cmd_sz);

// Costruzione dinamica menu contestuale
void context_menu_build_for_item(context_menu_t* menu, const char* name, const char* full_path,
                                 bool is_folder, bool can_paste);
void context_menu_build_for_empty(context_menu_t* menu, const char* current_dir, bool can_paste);

// Gestione visualizzazione, posizionamento e hit-test
void context_menu_show(context_menu_t* menu, int32_t screen_x, int32_t screen_y,
                       int32_t bound_w, int32_t bound_h);
void context_menu_hide(context_menu_t* menu);
void context_menu_draw(const context_menu_t* menu);
context_menu_action_t context_menu_handle_click(context_menu_t* menu, int32_t screen_x, int32_t screen_y);

// Esecuzione dell'azione primaria determinata dal registro
bool context_menu_execute_primary(const context_menu_t* menu);

#endif // GUI_CONTEXT_MENU_H
