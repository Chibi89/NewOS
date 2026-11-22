#ifndef SYSTEMLIBRARY_COMCTL_H
#define SYSTEMLIBRARY_COMCTL_H

#include <types.h>

#define COMCTL_API_VERSION 1
#define COMCTL_MAX_CONTROLS 64

// Tipi di controllo standardizzati moderni (Fluent / Liquid Glass / Classic)
typedef enum {
    COMCTL_NONE = 0,
    COMCTL_LABEL,
    COMCTL_BUTTON,
    COMCTL_CHECKBOX,
    COMCTL_RADIO,
    COMCTL_TOGGLE,
    COMCTL_TEXTBOX,
    COMCTL_NUMBER_STEPPER,
    COMCTL_SLIDER,
    COMCTL_PROGRESS,
    COMCTL_COMBOBOX,
    COMCTL_TABBAR,
    COMCTL_GROUPBOX,
    COMCTL_LISTVIEW,
    COMCTL_TREEVIEW,
    COMCTL_SCROLLBAR,
    COMCTL_MENU,
    COMCTL_TOOLBAR,
    COMCTL_STATUSBAR,
    COMCTL_BANNER,
    COMCTL_TOOLTIP,
    COMCTL_DATE_TIME_PICKER,
    COMCTL_SEPARATOR,
    COMCTL_BREADCRUMB,
    COMCTL_SPLITTER,
    COMCTL_WIZARD_STEPS,
    COMCTL_PROPERTY_GRID,
    COMCTL_CHART,
    COMCTL_TILE,
    COMCTL_CHIP_GROUP,
    COMCTL_COLOR_PICKER,
    COMCTL_HOTKEY_BOX,
    COMCTL_AVATAR,
    COMCTL_SPINNER,
    COMCTL_TOAST
} comctl_type_t;

// Varianti visive e funzionali per ogni tipo di controllo
typedef enum {
    COMCTL_VAR_DEFAULT = 0,
    COMCTL_VAR_PRIMARY,         // Accent / Call-to-action
    COMCTL_VAR_SECONDARY,       // Tonal / Subdued
    COMCTL_VAR_OUTLINE,         // Solo bordo
    COMCTL_VAR_GHOST,           // Trasparente fino a hover
    COMCTL_VAR_DANGER,          // Azione distruttiva (Rosso)
    COMCTL_VAR_SUCCESS,         // Conferma / Attivo (Verde)
    COMCTL_VAR_WARNING,         // Avviso (Ambra)
    COMCTL_VAR_INFO,            // Informativo (Blu/Ciano)
    COMCTL_VAR_PILL,            // Bordo completamente arrotondato
    COMCTL_VAR_SEGMENTED,       // Controllo segmentato (macOS / Fluent)
    COMCTL_VAR_CARD,            // Controllo selezionabile a scheda
    COMCTL_VAR_PASSWORD,        // TextBox mascherato (***)
    COMCTL_VAR_SEARCH,          // TextBox ricerca con icona e clear
    COMCTL_VAR_MULTILINE,       // Area testo multi-riga
    COMCTL_VAR_NUMERIC,         // Solo cifre / valori numerici
    COMCTL_VAR_TRISTATE,        // Checkbox 3 stati (off / on / mixed)
    COMCTL_VAR_EXPANDER,        // GroupBox / Accordion richiudibile
    COMCTL_VAR_VERTICAL,        // Orientamento verticale (Slider/Scrollbar/Sep)
    COMCTL_VAR_INDETERMINATE,   // ProgressBar animata indeterminata
    COMCTL_VAR_REPORT,          // ListView multi-colonna con header
    COMCTL_VAR_CHECKLIST,       // ListView con checkbox per riga
    COMCTL_VAR_SIDEBAR_NAV,     // TabBar / Menu laterale verticale
    COMCTL_VAR_LINK,            // Label cliccabile stile hyperlink
    COMCTL_VAR_BADGE            // Pill compatta di stato
} comctl_variant_t;

// Flag di stato e comportamento
#define COMCTL_F_VISIBLE        (1U << 0)
#define COMCTL_F_ENABLED        (1U << 1)
#define COMCTL_F_FOCUSED        (1U << 2)
#define COMCTL_F_HOVERED        (1U << 3)
#define COMCTL_F_PRESSED        (1U << 4)
#define COMCTL_F_CHECKED        (1U << 5)
#define COMCTL_F_INDETERMINATE  (1U << 6)
#define COMCTL_F_EXPANDED       (1U << 7)
#define COMCTL_F_READONLY       (1U << 8)
#define COMCTL_F_BORDERLESS     (1U << 9)
#define COMCTL_F_TRANSLUCENT    (1U << 10)
#define COMCTL_F_DROPDOWN_OPEN  (1U << 11)

typedef enum {
    COMCTL_ALIGN_LEFT = 0,
    COMCTL_ALIGN_CENTER = 1,
    COMCTL_ALIGN_RIGHT = 2
} comctl_align_t;

// Tipi di dati ospitabili nelle celle di COMCTL_PROPERTY_GRID (multi-colonna)
#define COMCTL_GRID_MAX_COLS 8

typedef enum {
    COMCTL_CELL_AUTO = 0,       // Eredita il tipo di default della colonna
    COMCTL_CELL_TEXT,           // Stringa di testo (editabile inline)
    COMCTL_CELL_INT,            // Intero decimale con segno
    COMCTL_CELL_HEX,            // Esadecimale (es. 0x00001A2B)
    COMCTL_CELL_BOOL,           // Booleano cliccabile ([x] / [ ])
    COMCTL_CELL_ENUM,           // Opzione ciclica / selettore
    COMCTL_CELL_COLOR,          // Anteprima colore (swatch) + codice hex
    COMCTL_CELL_PROGRESS,       // Barra percentuale (0..100)
    COMCTL_CELL_BADGE,          // Pill colorata di stato
    COMCTL_CELL_SIZE_BYTES,     // Dimensione formattata B/KB/MB/GB/TB
    COMCTL_CELL_DATETIME,       // Data/Ora formattata
    COMCTL_CELL_ACTION          // Pulsante azione inline nella cella
} comctl_cell_type_t;

typedef struct {
    char               title[32];
    int32_t            width;        // 0 = auto-distribuito
    comctl_cell_type_t default_type;
    uint8_t            align;        // comctl_align_t
    bool               editable;
} comctl_grid_col_t;

typedef struct {
    comctl_cell_type_t type;         // COMCTL_CELL_AUTO = usa default colonna
    char               text[48];
    int64_t            num_val;
    uint32_t           color;
    bool               bool_val;
    bool               readonly;
} comctl_grid_cell_t;

typedef struct {
    comctl_grid_cell_t cells[COMCTL_GRID_MAX_COLS];
    uint32_t           row_tag;
    uint16_t           level;
    bool               expanded;
} comctl_grid_row_t;

// Voce generica per ListView, TreeView, ComboBox, TabBar, Menu, Toolbar, Chart, Breadcrumb
typedef struct {
    char     label[64];
    char     sublabel[48];
    char     badge[24];
    uint32_t tag;
    uint16_t level;       // Profondita' per TreeView (0 = root)
    bool     expanded;    // Nodo espanso in TreeView
    bool     has_children;
    bool     checked;     // Checklist / Menu check
    bool     separator;   // Voce separatore in Menu / Toolbar
    bool     disabled;
} comctl_item_t;

// Tema normalizzato passato da qualsiasi app/DLL
typedef struct {
    uint32_t bg_color;
    uint32_t card_bg;
    uint32_t card_border;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t accent;
    uint32_t accent_text;
    uint32_t input_bg;
    uint32_t hover_bg;
    uint32_t danger_color;
    uint32_t success_color;
    uint32_t warning_color;
    int32_t  radius;
    uint8_t  surface_alpha; // 175..255 (traslucidita' Glass/Mica, 255 = opaco)
    bool     is_dark;
    bool     is_classic;
} comctl_theme_t;

struct comctl_control;
typedef void (*comctl_event_fn)(struct comctl_control* ctrl, void* user_data);

typedef struct comctl_control {
    uint32_t        id;
    comctl_type_t   type;
    uint16_t        variant;      // comctl_variant_t
    uint16_t        group_id;     // Per RadioButton / Tab group
    int16_t         tab_page;     // -1 = sempre visibile, >= 0 = visibile solo su quel tab
    uint32_t        flags;        // COMCTL_F_*
    uint8_t         align;        // comctl_align_t
    int8_t          radius;       // -1 = usa theme.radius

    int32_t         x, y, w, h;   // Coordinate relative all'area client della finestra

    char            text[128];
    char            placeholder[64];
    char            tooltip[64];

    int64_t         value;        // Valore numerico (Slider, Stepper, Progress, SelectedIdx, ActiveTab)
    int64_t         min_val;
    int64_t         max_val;
    int32_t         step;
    int32_t         scroll_offset;

    comctl_item_t*      items;
    int32_t             item_count;

    // Supporto Multi-Colonna e Multi-Tipo per COMCTL_PROPERTY_GRID
    comctl_grid_col_t*  grid_cols;
    int32_t             grid_col_count;
    comctl_grid_row_t*  grid_rows;
    int32_t             grid_row_count;
    int32_t             selected_col;
    int32_t             editing_row;
    int32_t             editing_col;

    comctl_event_fn     on_click;
    comctl_event_fn     on_change;
    void*               user_data;
} comctl_control_t;

typedef struct {
    comctl_control_t controls[COMCTL_MAX_CONTROLS];
    int32_t          count;
    int32_t          focused_index;
    int32_t          active_tab;
    int32_t          open_popup_index; // ComboBox o Menu aperto (-1 = nessuno)
    comctl_theme_t   theme;
    uint32_t         anim_tick;
} comctl_form_t;

// Tabella API esportata da COMCTL.DLL (Out-of-Kernel)
typedef struct {
    uint32_t version;
    void (*init_theme_from_system)(comctl_theme_t* out_theme);
    void (*form_init)(comctl_form_t* form, const comctl_theme_t* theme);
    int32_t (*form_add)(comctl_form_t* form, const comctl_control_t* ctrl);
    comctl_control_t* (*form_get)(comctl_form_t* form, uint32_t id);
    void (*form_draw)(comctl_form_t* form, int32_t ox, int32_t oy);
    bool (*form_mouse)(comctl_form_t* form, int32_t ox, int32_t oy, int32_t mx, int32_t my,
                       bool clicked, bool pressed, int32_t scroll_delta);
    bool (*form_key)(comctl_form_t* form, int key);
    void (*draw_control)(const comctl_theme_t* th, const comctl_control_t* ctrl, int32_t ox, int32_t oy);
} comctl_api_t;

const comctl_api_t* comctl_get_api(void);

#endif // SYSTEMLIBRARY_COMCTL_H
