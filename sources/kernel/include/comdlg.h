#ifndef KERNEL_COMDLG_H
#define KERNEL_COMDLG_H

#include <types.h>
#include <gui/wm.h>
#include <include/screen_modules.h>

typedef enum {
    COMDLG_MODE_OPEN_FILE     = 0,
    COMDLG_MODE_SAVE_FILE     = 1,
    COMDLG_MODE_EXEC_OP       = 2,
    COMDLG_MODE_FTP_STACK_MGR = 3,
    COMDLG_MODE_COPY_MOVE_DLG = 4
} comdlg_mode_t;

typedef enum {
    COMDLG_OP_NONE          = 0,
    COMDLG_OP_COPY          = 1,
    COMDLG_OP_MOVE          = 2,
    COMDLG_OP_RECYCLE       = 3,
    COMDLG_OP_RESTORE       = 4,
    COMDLG_OP_DELETE_PERM   = 5,
    COMDLG_OP_EMPTY_RECYCLE = 6,
    COMDLG_OP_STACK_PC2FTP  = 7,
    COMDLG_OP_STACK_FTP2PC  = 8,
    COMDLG_OP_PASTE         = 9
} comdlg_op_type_t;

typedef void (*comdlg_file_cb_t)(const char* selected_path, void* user_data);

typedef struct {
    int              mode;           // comdlg_mode_t
    int              op_type;        // comdlg_op_type_t (per COMDLG_MODE_EXEC_OP)
    char             initial_dir[128];
    char             default_name[64];
    char             src_path[128];
    char             dst_path[128];
    comdlg_file_cb_t on_complete;
    void*            user_data;
} comdlg_request_t;

// Apre il dialogo universale "Apri File..." fornito da COMDLG.DLL
bool comdlg_open_file_dialog(const char* initial_dir, comdlg_file_cb_t cb, void* user_data);

// Apre il dialogo universale "Salva con nome..." fornito da COMDLG.DLL
bool comdlg_save_file_dialog(const char* initial_dir, const char* default_name, comdlg_file_cb_t cb, void* user_data);

// Esegue un'operazione su file o Cestino (Copia, Sposta, Cestina, Ripristina, Cancella Definitivamente, Svuota Cestino)
bool comdlg_exec_file_op(comdlg_op_type_t op, const char* src_path, const char* dst_path);

// Avvia la finestra Dual-Pane & Stack Manager (PC <-> Spazio FTP Remoto + Operazioni File)
void app_comdlg_ftp_stack_launch(void);

// Avvia la finestra interattiva di Copia / Spostamento / Incolla (Ctrl+C, Ctrl+X, Ctrl+V, Sposta)
void app_comdlg_copy_move_launch(int op_type, const char* src_path, const char* dst_dir);

// ==============================================================================
// SOTTOSISTEMA RECYCLE.DLL (C:\NewOS\SystemLibrary\RECYCLE.DLL)
// Gestione Cestino, Quota Spazio e Suggerimento Eliminazione Diretta
// ==============================================================================
typedef enum {
    RECYCLE_MODE_SETTINGS_DLG = 0,  // Finestra Impostazioni e Gestione Cestino (Quota, Policy, Azioni)
    RECYCLE_MODE_DELETE_CHECK = 1,  // Controlla quota ed elimina; se supera il limite suggerisce eliminazione diretta
    RECYCLE_MODE_QUOTA_PROMPT = 2,  // Finestra Avviso Superamento Limite Spazio -> Suggerimento Eliminazione Diretta
    RECYCLE_MODE_RESTORE_FILE = 3,  // Ripristina un file da C:\Recycle.Bin alla posizione originale
    RECYCLE_MODE_EMPTY_BIN    = 4,  // Svuota interamente C:\Recycle.Bin
    RECYCLE_MODE_GET_STATS    = 5   // Legge spazio occupato, limite configurato e conteggio file
} recycle_mode_t;

typedef struct {
    int      mode;              // recycle_mode_t
    char     target_path[128];  // Percorso file da eliminare o nome file da ripristinare
    uint32_t out_used_bytes;    // Spazio attualmente occupato nel Cestino (byte)
    uint32_t out_limit_bytes;   // Limite spazio massimo configurato (byte)
    uint32_t out_file_count;    // Numero file presenti nel Cestino
    bool     out_bypass_active; // true se l'eliminazione diretta immediata e' attiva
} recycle_request_t;

// Apre la finestra di configurazione e gestione Cestino (RECYCLE.DLL)
void app_recycle_settings_launch(void);

// Apre la finestra di avviso superamento limite spazio che suggerisce l'eliminazione diretta (RECYCLE.DLL)
void app_recycle_quota_prompt_launch(const char* target_path);

// Elimina un file tramite RECYCLE.DLL verificando il limite spazio (apre il suggerimento di eliminazione diretta se oltre soglia)
bool recycle_delete_file(const char* target_path);

// Ripristina un file dal Cestino tramite RECYCLE.DLL
bool recycle_restore_file(const char* basename);

// Svuota il Cestino tramite RECYCLE.DLL
bool recycle_empty_bin(void);

// Ottiene le statistiche e la quota attuale del Cestino da RECYCLE.DLL
bool recycle_get_stats(uint32_t* out_used_bytes, uint32_t* out_limit_bytes, uint32_t* out_count);

#endif // KERNEL_COMDLG_H

