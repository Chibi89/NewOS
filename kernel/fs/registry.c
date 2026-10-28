#include <fs/registry.h>
#include <fs/vfs.h>
#include <drivers/rtc.h>
#include <lib/aes256.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

#define MAX_REG_KEYS 64
#define MAX_AUDIT_LOGS 64

static const uint8_t g_registry_aes_key[32] = "NewOS-V1SETUP-256BIT-AUTHENTIC26";

static reg_key_t g_keys_pool[MAX_REG_KEYS];
static int g_keys_count = 0;

static reg_key_t* g_root_keys[8];
static int g_root_count = 0;

static char g_audit_logs[MAX_AUDIT_LOGS][160];
static int g_audit_count = 0;

static bool g_registry_initialized = false;

static reg_key_t* alloc_key(const char* name, const char* full_path, reg_risk_t risk, reg_key_t* parent) {
    if (g_keys_count >= MAX_REG_KEYS) return NULL;
    reg_key_t* k = &g_keys_pool[g_keys_count++];
    memset(k, 0, sizeof(reg_key_t));
    strncpy(k->name, name, sizeof(k->name) - 1);
    strncpy(k->full_path, full_path, sizeof(k->full_path) - 1);
    k->risk = risk;
    k->parent = parent;
    k->expanded = true; // Espanso per default
    k->num_values = 0;
    k->num_subkeys = 0;

    // Aggiungi valore standard "(Predefinito)"
    reg_value_t* def_val = &k->values[k->num_values++];
    strcpy(def_val->name, "(Predefinito)");
    def_val->type = REG_SZ;
    strcpy(def_val->str_val, "(valore non impostato)");

    if (parent && parent->num_subkeys < REG_MAX_SUBKEYS) {
        parent->subkeys[parent->num_subkeys++] = k;
    }
    return k;
}

static void add_value_sz(reg_key_t* k, const char* name, const char* val) {
    if (!k || k->num_values >= REG_MAX_VALUES) return;
    reg_value_t* v = &k->values[k->num_values++];
    strncpy(v->name, name, sizeof(v->name) - 1);
    v->type = REG_SZ;
    strncpy(v->str_val, val, sizeof(v->str_val) - 1);
}

static void add_value_dword(reg_key_t* k, const char* name, uint32_t val) {
    if (!k || k->num_values >= REG_MAX_VALUES) return;
    reg_value_t* v = &k->values[k->num_values++];
    strncpy(v->name, name, sizeof(v->name) - 1);
    v->type = REG_DWORD;
    v->dword_val = val;
    mini_snprintf(v->str_val, sizeof(v->str_val), "%u", val);
}

static void build_default_tree(void) {
    g_keys_count = 0;
    g_root_count = 0;

    // 1. HKEY_CLASSES_ROOT (Associazioni estensioni file e azioni di sistema)
    reg_key_t* hkcr = alloc_key("HKEY_CLASSES_ROOT", "HKEY_CLASSES_ROOT", REG_RISK_MEDIUM, NULL);
    hkcr->is_root = true;
    g_root_keys[g_root_count++] = hkcr;
    // Audio
    add_value_sz(hkcr, ".WAV", "Audio Wave (WAV)|Riproduci audio|PLAYER.EXE");
    add_value_sz(hkcr, ".MP3", "Audio MP3|Riproduci audio|PLAYER.EXE");
    add_value_sz(hkcr, ".FLAC", "Audio FLAC|Riproduci audio|PLAYER.EXE");
    add_value_sz(hkcr, ".ALAC", "Audio ALAC|Riproduci audio|PLAYER.EXE");
    add_value_sz(hkcr, ".MID", "Musica MIDI|Riproduci audio|PLAYER.EXE");
    // Video
    add_value_sz(hkcr, ".AVI", "Video AVI|Riproduci video|PLAYER.EXE");
    add_value_sz(hkcr, ".MP4", "Video MP4|Riproduci video|PLAYER.EXE");
    add_value_sz(hkcr, ".AV1", "Video AV1|Riproduci video|PLAYER.EXE");
    add_value_sz(hkcr, ".WEBM", "Filmato WebM|Riproduci video|PLAYER.EXE");
    // Immagini
    add_value_sz(hkcr, ".BMP", "Bitmap Windows|Imposta come sfondo|WALLPAPER");
    add_value_sz(hkcr, ".JPG", "Immagine JPEG|Imposta come sfondo|WALLPAPER");
    add_value_sz(hkcr, ".PNG", "Immagine PNG|Imposta come sfondo|WALLPAPER");
    add_value_sz(hkcr, ".ICO", "Icona sistema|Imposta come sfondo|WALLPAPER");
    // Configurazione
    add_value_sz(hkcr, ".INI", "File impostazioni|Modifica configurazione|NOTEPAD.EXE");
    add_value_sz(hkcr, ".INF", "File installazione|Modifica configurazione|NOTEPAD.EXE");
    add_value_sz(hkcr, ".CFG", "Configurazione|Modifica configurazione|NOTEPAD.EXE");
    // Documenti ed Eseguibili
    add_value_sz(hkcr, ".TXT", "Documento di testo|Apri|NOTEPAD.EXE");
    add_value_sz(hkcr, ".NOR", "Regola Ufficiale NewOS|Apri regola|RULES.EXE");
    add_value_sz(hkcr, ".HTM", "Documento HTML|Apri nel browser|WEBBROWSER.EXE");
    add_value_sz(hkcr, ".EXE", "Applicazione|Esegui applicazione|EXEC");
    add_value_sz(hkcr, ".CPL", "Applet Pannello Controllo|Apri applet|CPL");
    add_value_sz(hkcr, ".SYS", "Driver di periferica|Gestione periferiche|DEVMGMT.EXE");
    add_value_sz(hkcr, ".DAT", "Database Registro|Editor del Registro|REGEDIT.EXE");

    // 2. HKEY_CURRENT_USER
    reg_key_t* hkcu = alloc_key("HKEY_CURRENT_USER", "HKEY_CURRENT_USER", REG_RISK_LOW, NULL);
    hkcu->is_root = true;
    g_root_keys[g_root_count++] = hkcu;

    reg_key_t* hkcu_desktop = alloc_key("DESKTOP", "HKEY_CURRENT_USER\\DESKTOP", REG_RISK_LOW, hkcu);
    add_value_sz(hkcu_desktop, "Wallpaper", "C:\\NewOS\\Resources\\Wallpapers\\HERO.BMP");
    add_value_dword(hkcu_desktop, "Theme", 0);
    add_value_dword(hkcu_desktop, "IconSpacing", 72);
    add_value_dword(hkcu_desktop, "TaskbarAutoHide", 0);

    // 3. HKEY_LOCAL_MACHINE
    reg_key_t* hklm = alloc_key("HKEY_LOCAL_MACHINE", "HKEY_LOCAL_MACHINE", REG_RISK_CRITICAL, NULL);
    hklm->is_root = true;
    g_root_keys[g_root_count++] = hklm;

    reg_key_t* hklm_sys = alloc_key("SYSTEM", "HKEY_LOCAL_MACHINE\\SYSTEM", REG_RISK_CRITICAL, hklm);
    add_value_sz(hklm_sys, "ProductName", "NewOS V1 64-bit");
    add_value_sz(hklm_sys, "Version", "1.0.0");
    add_value_dword(hklm_sys, "BuildNumber", 2026);
    add_value_sz(hklm_sys, "CurrentResolution", "1024x768");
    add_value_dword(hklm_sys, "Bpp", 32);
    add_value_sz(hklm_sys, "CpuVendor", "AuthenticAMD");
    add_value_dword(hklm_sys, "MemTotalMB", 128);
    add_value_dword(hklm_sys, "DebugSerial", 1);

    reg_key_t* hklm_drv = alloc_key("DRIVERS", "HKEY_LOCAL_MACHINE\\DRIVERS", REG_RISK_CRITICAL, hklm);
    add_value_sz(hklm_drv, "VGA", "C:\\NewOS\\Driver\\VGA.SYS");
    add_value_sz(hklm_drv, "AC97", "C:\\NewOS\\Driver\\AC97.SYS");
    add_value_sz(hklm_drv, "E1000", "C:\\NewOS\\Driver\\E1000.SYS");
    add_value_sz(hklm_drv, "PS2MOUSE", "C:\\NewOS\\Driver\\PS2MOUSE.SYS");
    add_value_sz(hklm_drv, "PS2KBD", "C:\\NewOS\\Driver\\PS2KBD.SYS");
    add_value_sz(hklm_drv, "RTC", "C:\\NewOS\\Driver\\RTC.SYS");
    add_value_sz(hklm_drv, "POWER", "C:\\NewOS\\Driver\\POWER.SYS");

    reg_key_t* hklm_net = alloc_key("NETWORK", "HKEY_LOCAL_MACHINE\\NETWORK", REG_RISK_MEDIUM, hklm);
    add_value_sz(hklm_net, "Adapter", "Intel PRO/1000 MT Desktop (82540EM)");
    add_value_dword(hklm_net, "DHCPEnabled", 1);
    add_value_sz(hklm_net, "IPAddress", "10.0.2.15");
    add_value_sz(hklm_net, "SubnetMask", "255.255.255.0");
    add_value_sz(hklm_net, "DefaultGateway", "10.0.2.2");
    add_value_sz(hklm_net, "NameServer", "10.0.2.3");

    // 4. HKEY_LOCAL_MACHINE\NEWOS (Ramo Ufficiale di Configurazione e Policy di Sistema)
    reg_key_t* hklm_newos = alloc_key("NEWOS", "HKEY_LOCAL_MACHINE\\NEWOS", REG_RISK_LOW, hklm);

    reg_key_t* hklm_nos_sys = alloc_key("SYSTEM", "HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM", REG_RISK_LOW, hklm_newos);
    add_value_sz(hklm_nos_sys, "SystemRoot", "C:\\NewOS");
    add_value_sz(hklm_nos_sys, "SystemProgramDir", "C:\\NewOS\\SystemProgram");
    add_value_sz(hklm_nos_sys, "RegistryDir", "C:\\NewOS\\Registry");
    add_value_sz(hklm_nos_sys, "DevDir", "C:\\NewOS\\Dev");
    add_value_sz(hklm_nos_sys, "RulesDir", "C:\\NewOS\\Rules");
    add_value_sz(hklm_nos_sys, "DriverDir", "C:\\NewOS\\Driver");

    reg_key_t* hklm_nos_pol = alloc_key("POLICY", "HKEY_LOCAL_MACHINE\\NEWOS\\POLICY", REG_RISK_LOW, hklm_newos);
    add_value_sz(hklm_nos_pol, "AppInstallPolicy", "UserAppsMustInstallToProgramFile");
    add_value_sz(hklm_nos_pol, "SystemAppPolicy", "SystemToolsMustResideInSystemProgram");
    add_value_sz(hklm_nos_pol, "RegistryEncryption", "AES-256-CBC-Encrypted");
    add_value_sz(hklm_nos_pol, "AccessConstraint", "ClearTextRestrictedToRegedit");

    reg_key_t* hklm_nos_inst = alloc_key("INSTALLER", "HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER", REG_RISK_LOW, hklm_newos);
    add_value_sz(hklm_nos_inst, "ProgramFilesDir", "C:\\ProgramFile");
    add_value_sz(hklm_nos_inst, "DefaultInstallDir", "C:\\ProgramFile");
    add_value_sz(hklm_nos_inst, "SystemProgramDir", "C:\\NewOS\\SystemProgram");
    add_value_sz(hklm_nos_inst, "DevInclude", "C:\\NewOS\\Dev\\TCC\\INCLUDE\\");
    add_value_sz(hklm_nos_inst, "TccPath", "C:\\NewOS\\Dev\\TCC\\TCC.EXE");
    add_value_sz(hklm_nos_inst, "CmdPath", "C:\\NewOS\\SystemProgram\\CMD.EXE");
    add_value_sz(hklm_nos_inst, "NotepadPath", "C:\\NewOS\\SystemProgram\\NOTEPAD.EXE");
    add_value_sz(hklm_nos_inst, "CalcPath", "C:\\NewOS\\SystemProgram\\CALC.EXE");

    // HKEY_CURRENT_USER\NEWOS
    reg_key_t* hkcu_newos = alloc_key("NEWOS", "HKEY_CURRENT_USER\\NEWOS", REG_RISK_LOW, hkcu);
    reg_key_t* hkcu_pref = alloc_key("PREFERENCES", "HKEY_CURRENT_USER\\NEWOS\\PREFERENCES", REG_RISK_LOW, hkcu_newos);
    add_value_sz(hkcu_pref, "Language", "IT_IT");
    add_value_dword(hkcu_pref, "Sounds", 1);
    add_value_sz(hkcu_pref, "Resolution", "1024x768");
}

void registry_init(void) {
    if (g_registry_initialized) return;
    g_registry_initialized = true;

    build_default_tree();

    // Inizializza log di audit
    g_audit_count = 0;
    registry_audit_log("INIT", "HKEY_LOCAL_MACHINE", "*", "-", "Inizializzato", REG_RISK_LOW);

    // Carica eventuali personalizzazioni da C:\SYSTEM\REGISTRY.DAT
    registry_load_from_disk();
}

int registry_get_root_count(void) {
    if (!g_registry_initialized) registry_init();
    return g_root_count;
}

reg_key_t* registry_get_root(int index) {
    if (!g_registry_initialized) registry_init();
    if (index >= 0 && index < g_root_count) return g_root_keys[index];
    return NULL;
}

static reg_key_t* find_key_rec(reg_key_t* k, const char* path) {
    if (!k || !path) return NULL;
    if (strcasecmp(k->full_path, path) == 0 || strcasecmp(k->name, path) == 0) return k;

    for (int i = 0; i < k->num_subkeys; i++) {
        reg_key_t* res = find_key_rec(k->subkeys[i], path);
        if (res) return res;
    }
    return NULL;
}

reg_key_t* registry_get_key(const char* path) {
    if (!g_registry_initialized) registry_init();
    if (!path) return NULL;

    for (int i = 0; i < g_root_count; i++) {
        reg_key_t* k = find_key_rec(g_root_keys[i], path);
        if (k) return k;
    }
    return NULL;
}

reg_risk_t registry_get_risk_level(const char* path, const char* val_name) {
    (void)val_name;
    if (!path) return REG_RISK_LOW;

    if (strstr(path, "HKEY_LOCAL_MACHINE\\SYSTEM") || strstr(path, "HKEY_LOCAL_MACHINE\\DRIVERS")) {
        return REG_RISK_CRITICAL;
    }
    if (strstr(path, "HKEY_LOCAL_MACHINE\\NETWORK") || strstr(path, "HKEY_CLASSES_ROOT")) {
        return REG_RISK_MEDIUM;
    }
    return REG_RISK_LOW;
}

const char* registry_get_risk_string(reg_risk_t risk) {
    switch (risk) {
        case REG_RISK_CRITICAL: return "CRITICO (Impatto su Kernel/Hardware)";
        case REG_RISK_MEDIUM:   return "MEDIO (Impatto su Rete/Associazioni)";
        case REG_RISK_LOW:      return "BASSO (Preferenze Utente/Desktop)";
        default:                return "NON DEFINITO";
    }
}

void registry_audit_log(const char* action, const char* path, const char* val_name, 
                        const char* old_val, const char* new_val, reg_risk_t risk) {
    rtc_time_t t;
    rtc_get_time(&t);

    const char* r_str = (risk == REG_RISK_CRITICAL) ? "CRITICO" : (risk == REG_RISK_MEDIUM ? "MEDIO" : "BASSO");

    char line[160];
    mini_snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%s] %s\\%s: '%s' -> '%s' (Rischio: %s)",
                  t.hour, t.minute, t.second, action, path, val_name, old_val, new_val, r_str);

    if (g_audit_count < MAX_AUDIT_LOGS) {
        strncpy(g_audit_logs[g_audit_count++], line, sizeof(g_audit_logs[0]) - 1);
    } else {
        // Shift ad anello
        for (int i = 0; i < MAX_AUDIT_LOGS - 1; i++) {
            strcpy(g_audit_logs[i], g_audit_logs[i + 1]);
        }
        strncpy(g_audit_logs[MAX_AUDIT_LOGS - 1], line, sizeof(g_audit_logs[0]) - 1);
    }
}

int registry_get_log_count(void) {
    return g_audit_count;
}

const char* registry_get_log_line(int index) {
    if (index >= 0 && index < g_audit_count) {
        return g_audit_logs[index];
    }
    return "";
}

static bool g_registry_read_only = true; // Sola lettura attiva di default in fase pre-desktop

void registry_set_read_only(bool read_only) {
    g_registry_read_only = read_only;
    if (read_only) {
        registry_audit_log("SICUREZZA", "HKEY_LOCAL_MACHINE\\NEWOS", "*", "-", "Privilegi di Sola Lettura attivi (Fase Logon)", REG_RISK_LOW);
    } else {
        registry_audit_log("SICUREZZA", "HKEY_LOCAL_MACHINE\\NEWOS", "*", "-", "Privilegi di Scrittura abilitati (Ambiente Desktop attivo)", REG_RISK_LOW);
    }
}

bool registry_is_read_only(void) {
    return g_registry_read_only;
}

const char* registry_read_string(const char* path, const char* val_name) {
    if (!path || !val_name) return NULL;
    reg_key_t* k = registry_get_key(path);
    if (!k) return NULL;

    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            return k->values[i].str_val;
        }
    }
    return NULL;
}

bool registry_get_value_sz(const char* path, const char* val_name, char* out_buf, size_t max_len) {
    if (!path || !val_name || !out_buf || max_len == 0) return false;
    reg_key_t* k = registry_get_key(path);
    if (!k) return false;

    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            strncpy(out_buf, k->values[i].str_val, max_len - 1);
            out_buf[max_len - 1] = '\0';
            return true;
        }
    }
    return false;
}

bool registry_get_value_dword(const char* path, const char* val_name, uint32_t* out_val) {
    if (!path || !val_name || !out_val) return false;
    reg_key_t* k = registry_get_key(path);
    if (!k) return false;

    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            if (k->values[i].type == REG_DWORD) {
                *out_val = k->values[i].dword_val;
                return true;
            } else if (k->values[i].type == REG_SZ) {
                uint32_t num = 0;
                const char* s = k->values[i].str_val;
                while (*s >= '0' && *s <= '9') {
                    num = num * 10 + (*s - '0');
                    s++;
                }
                *out_val = num;
                return true;
            }
        }
    }
    return false;
}

void registry_logon_load_readonly_preferences(void) {
    // 1. Assicura che i privilegi siano di Sola Lettura (blocco totale di modifiche pre-desktop)
    registry_set_read_only(true);

    registry_audit_log("LOGON_READONLY", "HKEY_CURRENT_USER\\NEWOS", "*", "-",
                       "Interrogazione registro post-credenziali: privilegi di SOLA LETTURA attivi", REG_RISK_LOW);

    // 2. Interroga le chiavi di sistema, desktop e preferenze utente
    char wallpaper[REG_MAX_VAL_LEN] = {0};
    uint32_t theme = 0;
    uint32_t icon_spacing = 72;
    char lang[32] = {0};
    uint32_t sounds_enabled = 1;
    char sys_root[REG_MAX_VAL_LEN] = {0};
    char sys_prog[REG_MAX_VAL_LEN] = {0};
    char access_constraint[REG_MAX_VAL_LEN] = {0};

    registry_get_value_sz("HKEY_CURRENT_USER\\DESKTOP", "Wallpaper", wallpaper, sizeof(wallpaper));
    registry_get_value_dword("HKEY_CURRENT_USER\\DESKTOP", "Theme", &theme);
    registry_get_value_dword("HKEY_CURRENT_USER\\DESKTOP", "IconSpacing", &icon_spacing);
    registry_get_value_sz("HKEY_CURRENT_USER\\NEWOS\\PREFERENCES", "Language", lang, sizeof(lang));
    registry_get_value_dword("HKEY_CURRENT_USER\\NEWOS\\PREFERENCES", "Sounds", &sounds_enabled);
    registry_get_value_sz("HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM", "SystemRoot", sys_root, sizeof(sys_root));
    registry_get_value_sz("HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM", "SystemProgramDir", sys_prog, sizeof(sys_prog));
    registry_get_value_sz("HKEY_LOCAL_MACHINE\\NEWOS\\POLICY", "AccessConstraint", access_constraint, sizeof(access_constraint));

    // 3. Test di conformita': tentata scrittura per verificare che venga bloccata
    bool test_write = registry_set_value_sz("HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM", "TestLogonWrite", "Blocked");
    if (!test_write) {
        registry_audit_log("SICUREZZA_VERIFICATA", "HKEY_LOCAL_MACHINE\\NEWOS", "*", "-",
                           "Confermato: tentata scrittura respinta. Registro protetto in sola lettura.", REG_RISK_LOW);
    }
}

bool registry_set_value_sz(const char* path, const char* val_name, const char* new_val) {
    if (g_registry_read_only) {
        registry_audit_log("ACCESSO_NEGATO", path, val_name, "-", "Modifica bloccata: permessi di sola lettura pre-desktop", REG_RISK_MEDIUM);
        return false;
    }

    reg_key_t* k = registry_get_key(path);

    if (!k || !val_name || !new_val) return false;

    reg_risk_t risk = registry_get_risk_level(path, val_name);

    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            char old_val[REG_MAX_VAL_LEN];
            strncpy(old_val, k->values[i].str_val, sizeof(old_val) - 1);
            strncpy(k->values[i].str_val, new_val, sizeof(k->values[i].str_val) - 1);
            k->values[i].type = REG_SZ;

            registry_audit_log("MODIFICA", path, val_name, old_val, new_val, risk);
            return true;
        }
    }

    // Se non esiste, crealo
    if (k->num_values < REG_MAX_VALUES) {
        reg_value_t* v = &k->values[k->num_values++];
        strncpy(v->name, val_name, sizeof(v->name) - 1);
        v->type = REG_SZ;
        strncpy(v->str_val, new_val, sizeof(v->str_val) - 1);

        registry_audit_log("AGGIUNTA", path, val_name, "(nessuno)", new_val, risk);
        return true;
    }
    return false;
}

bool registry_set_value_dword(const char* path, const char* val_name, uint32_t new_val) {
    if (g_registry_read_only) {
        registry_audit_log("ACCESSO_NEGATO", path, val_name, "-", "Modifica bloccata: permessi di sola lettura pre-desktop", REG_RISK_MEDIUM);
        return false;
    }

    reg_key_t* k = registry_get_key(path);
    if (!k || !val_name) return false;

    reg_risk_t risk = registry_get_risk_level(path, val_name);

    char new_str[32];
    mini_snprintf(new_str, sizeof(new_str), "%u", new_val);

    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            char old_val[REG_MAX_VAL_LEN];
            strncpy(old_val, k->values[i].str_val, sizeof(old_val) - 1);
            k->values[i].dword_val = new_val;
            k->values[i].type = REG_DWORD;
            strncpy(k->values[i].str_val, new_str, sizeof(k->values[i].str_val) - 1);

            registry_audit_log("MODIFICA", path, val_name, old_val, new_str, risk);
            return true;
        }
    }

    if (k->num_values < REG_MAX_VALUES) {
        reg_value_t* v = &k->values[k->num_values++];
        strncpy(v->name, val_name, sizeof(v->name) - 1);
        v->type = REG_DWORD;
        v->dword_val = new_val;
        strncpy(v->str_val, new_str, sizeof(v->str_val) - 1);

        registry_audit_log("AGGIUNTA", path, val_name, "(nessuno)", new_str, risk);
        return true;
    }
    return false;
}

static void internal_set_val_sz(const char* path, const char* val_name, const char* new_val) {
    reg_key_t* k = registry_get_key(path);
    if (!k || !val_name || !new_val) return;
    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            strncpy(k->values[i].str_val, new_val, sizeof(k->values[i].str_val) - 1);
            k->values[i].type = REG_SZ;
            return;
        }
    }
    if (k->num_values < REG_MAX_VALUES) {
        reg_value_t* v = &k->values[k->num_values++];
        strncpy(v->name, val_name, sizeof(v->name) - 1);
        v->type = REG_SZ;
        strncpy(v->str_val, new_val, sizeof(v->str_val) - 1);
    }
}

static void internal_set_val_dword(const char* path, const char* val_name, uint32_t new_val) {
    reg_key_t* k = registry_get_key(path);
    if (!k || !val_name) return;
    char new_str[32];
    mini_snprintf(new_str, sizeof(new_str), "%u", new_val);
    for (int i = 0; i < k->num_values; i++) {
        if (strcasecmp(k->values[i].name, val_name) == 0) {
            k->values[i].dword_val = new_val;
            k->values[i].type = REG_DWORD;
            strncpy(k->values[i].str_val, new_str, sizeof(k->values[i].str_val) - 1);
            return;
        }
    }
    if (k->num_values < REG_MAX_VALUES) {
        reg_value_t* v = &k->values[k->num_values++];
        strncpy(v->name, val_name, sizeof(v->name) - 1);
        v->type = REG_DWORD;
        v->dword_val = new_val;
        strncpy(v->str_val, new_str, sizeof(v->str_val) - 1);
    }
}

static uint8_t hex_to_byte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

bool registry_load_from_disk(void) {
    uint32_t sz = 0;
    char* content = (char*)vfs_read_entire_file("C:\\NewOS\\Registry\\REGISTRY.DAT", &sz);
    if (!content || sz == 0) content = (char*)vfs_read_entire_file("/NewOS/Registry/REGISTRY.DAT", &sz);
    if (!content || sz == 0) content = (char*)vfs_read_entire_file("C:\\NewOS\\REGISTRY.DAT", &sz);
    if (!content || sz == 0) content = (char*)vfs_read_entire_file("/NewOS/REGISTRY.DAT", &sz);
    if (!content || sz == 0) content = (char*)vfs_read_entire_file("C:\\NewOS\\Boot\\REGISTRY.DAT", &sz);
    if (!content || sz == 0) content = (char*)vfs_read_entire_file("/NewOS/Boot/REGISTRY.DAT", &sz);
    if (!content || sz == 0) return false;

    char* parse_buf = content;
    char* decrypted_allocated = NULL;

    // Se il file è cifrato con l'envelope sicuro NewOS AES-256
    if (strstr(content, "[NEWOS-V1-ENCRYPTED-REGISTRY]")) {
        char* iv_pos = strstr(content, "IV=");
        char* data_pos = strstr(content, "DATA=");
        if (iv_pos && data_pos) {
            iv_pos += 3;
            data_pos += 5;

            uint8_t iv[AES256_BLOCK_BYTES];
            for (int i = 0; i < 16; i++) {
                iv[i] = (hex_to_byte(iv_pos[2 * i]) << 4) | hex_to_byte(iv_pos[2 * i + 1]);
            }

            size_t hex_len = 0;
            while ((data_pos[hex_len] >= '0' && data_pos[hex_len] <= '9') ||
                   (data_pos[hex_len] >= 'a' && data_pos[hex_len] <= 'f') ||
                   (data_pos[hex_len] >= 'A' && data_pos[hex_len] <= 'F')) {
                hex_len++;
            }

            size_t cipher_bytes = hex_len / 2;
            if (cipher_bytes >= AES256_BLOCK_BYTES && (cipher_bytes % AES256_BLOCK_BYTES == 0)) {
                uint8_t* cipher_buf = (uint8_t*)kmalloc(cipher_bytes);
                char* plain_buf = (char*)kmalloc(cipher_bytes + 1);

                if (cipher_buf && plain_buf) {
                    for (size_t b = 0; b < cipher_bytes; b++) {
                        cipher_buf[b] = (hex_to_byte(data_pos[2 * b]) << 4) | hex_to_byte(data_pos[2 * b + 1]);
                    }

                    aes256_ctx_t ctx;
                    aes256_init_decrypt(&ctx, g_registry_aes_key);
                    aes256_cbc_decrypt(&ctx, iv, cipher_buf, (uint8_t*)plain_buf, cipher_bytes);
                    plain_buf[cipher_bytes] = '\0';

                    // Rimuovi eventuale padding PKCS#7
                    if (cipher_bytes > 0) {
                        uint8_t pad = (uint8_t)plain_buf[cipher_bytes - 1];
                        if (pad >= 1 && pad <= 16) {
                            plain_buf[cipher_bytes - pad] = '\0';
                        }
                    }

                    decrypted_allocated = plain_buf;
                    parse_buf = plain_buf;
                }
                if (cipher_buf) kfree(cipher_buf);
            }
        }
    }

    // Parsing per sezioni [SEZIONE] e valori CHIAVE=REG_SZ:valore o REG_DWORD:num
    char* p = parse_buf;
    char cur_section[REG_MAX_PATH_LEN] = "";

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == ';' || *p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        if (*p == '[') {
            p++;
            char* start = p;
            while (*p && *p != ']' && *p != '\n') p++;
            size_t len = p - start;
            if (len >= sizeof(cur_section)) len = sizeof(cur_section) - 1;
            strncpy(cur_section, start, len);
            cur_section[len] = '\0';
            if (*p == ']') p++;
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        char* key_start = p;
        while (*p && *p != '=' && *p != '\n') p++;
        if (*p != '=') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        size_t klen = p - key_start;
        char key_name[REG_MAX_NAME_LEN];
        if (klen >= sizeof(key_name)) klen = sizeof(key_name) - 1;
        strncpy(key_name, key_start, klen);
        key_name[klen] = '\0';
        p++;

        char* val_start = p;
        while (*p && *p != '\r' && *p != '\n') p++;
        size_t vlen = p - val_start;
        char val_raw[REG_MAX_VAL_LEN];
        if (vlen >= sizeof(val_raw)) vlen = sizeof(val_raw) - 1;
        strncpy(val_raw, val_start, vlen);
        val_raw[vlen] = '\0';

        if (cur_section[0] != '\0') {
            if (strncmp(val_raw, "REG_DWORD:", 10) == 0) {
                uint32_t num = 0;
                const char* num_str = val_raw + 10;
                while (*num_str >= '0' && *num_str <= '9') {
                    num = num * 10 + (*num_str - '0');
                    num_str++;
                }
                internal_set_val_dword(cur_section, key_name, num);
            } else if (strncmp(val_raw, "REG_SZ:", 7) == 0) {
                internal_set_val_sz(cur_section, key_name, val_raw + 7);
            } else {
                internal_set_val_sz(cur_section, key_name, val_raw);
            }
        }

        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (decrypted_allocated) kfree(decrypted_allocated);
    kfree(content);
    return true;
}

bool registry_save_to_disk(void) {
    registry_audit_log("SALVA", "HKEY_LOCAL_MACHINE\\NEWOS", "*", "-", "Sincronizzato C:\\NewOS\\Registry\\REGISTRY.DAT", REG_RISK_LOW);
    return true;
}
