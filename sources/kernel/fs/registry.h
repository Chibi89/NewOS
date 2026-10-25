#ifndef FS_REGISTRY_H
#define FS_REGISTRY_H

#include <types.h>

#define REG_MAX_NAME_LEN    64
#define REG_MAX_VAL_LEN     128
#define REG_MAX_PATH_LEN    256
#define REG_MAX_VALUES      32
#define REG_MAX_SUBKEYS     12

typedef enum {
    REG_NONE = 0,
    REG_SZ,         // String (ASCII / UTF-8)
    REG_DWORD,      // 32-bit unsigned integer
    REG_BINARY      // Binary byte array
} reg_type_t;

typedef enum {
    REG_RISK_LOW = 0,       // Desktop, preferenze utente (verde)
    REG_RISK_MEDIUM,        // Rete, associazioni file (arancione)
    REG_RISK_CRITICAL       // Sistema, parametri kernel, driver (rosso)
} reg_risk_t;

typedef struct {
    char name[REG_MAX_NAME_LEN];
    reg_type_t type;
    char str_val[REG_MAX_VAL_LEN];
    uint32_t dword_val;
    uint8_t bin_val[32];
    uint32_t bin_len;
} reg_value_t;

typedef struct reg_key {
    char name[REG_MAX_NAME_LEN];
    char full_path[REG_MAX_PATH_LEN];
    bool is_root;
    bool expanded;
    reg_risk_t risk;

    int num_values;
    reg_value_t values[REG_MAX_VALUES];

    int num_subkeys;
    struct reg_key* subkeys[REG_MAX_SUBKEYS];
    struct reg_key* parent;
} reg_key_t;

// Inizializzazione registro di sistema
void registry_init(void);

// Conteggio e accesso alle chiavi radice (HKEY_*)
int registry_get_root_count(void);
reg_key_t* registry_get_root(int index);

// Ricerca chiave tramite percorso completo (es. "HKEY_LOCAL_MACHINE\\SYSTEM")
reg_key_t* registry_get_key(const char* path);

// Valutazione livello di rischio della modifica
reg_risk_t registry_get_risk_level(const char* path, const char* val_name);
const char* registry_get_risk_string(reg_risk_t risk);

// Lettura e modifica valori
bool registry_get_value_sz(const char* path, const char* val_name, char* out_buf, size_t max_len);
bool registry_get_value_dword(const char* path, const char* val_name, uint32_t* out_val);
bool registry_set_value_sz(const char* path, const char* val_name, const char* new_val);
bool registry_set_value_dword(const char* path, const char* val_name, uint32_t new_val);

// Audit Trail: registra le modifiche in memoria e su C:\SYSTEM\REGISTRY.LOG
void registry_audit_log(const char* action, const char* path, const char* val_name, 
                        const char* old_val, const char* new_val, reg_risk_t risk);

// Lettura log di audit
int registry_get_log_count(void);
const char* registry_get_log_line(int index);

// Permessi di accesso: sola lettura in fase di logon/pre-desktop, scrittura abilitata solo su Desktop
void registry_set_read_only(bool read_only);
bool registry_is_read_only(void);

// Interrogazione del registro post-inserimento credenziali (esclusivamente in Sola Lettura)
void registry_logon_load_readonly_preferences(void);

// Sincronizzazione con disco exFAT C:\NewOS\Registry\REGISTRY.DAT
bool registry_load_from_disk(void);
bool registry_save_to_disk(void);

#endif // FS_REGISTRY_H

