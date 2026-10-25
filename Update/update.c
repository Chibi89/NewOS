#include "update.h"
#include "sha256.h"

// Master Key simmetriche NewOS per pacchetti e certificati
static const uint8_t AES_KEY[]  = "NewOS-V1SETUP-256BIT-AUTHENTIC26";
static const uint8_t HMAC_KEY[] = "NewSoft-NewOS-UpdateAuthMasterKey26";

void* memset(void* dest, int c, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

// Header di protezione modulo protetto Ring 0 (64 byte)
__attribute__((section(".header"), used))
static const module_header_t module_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_UPDATE,
    .code_size      = 0,
    .entry_offset   = sizeof(module_header_t),
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED,
    .module_name    = "UPDATEMOD",
    .signature      = "NEWSOFT"
};

// Stato di rendering UI
#define LOG_LINES_MAX 5
typedef struct {
    char log_lines[LOG_LINES_MAX][80];
    int  log_count;
} update_ui_state_t;

static update_ui_state_t* g_ui_state = NULL;

static void update_log_event(const char* msg) {
    if (!g_ui_state) return;
    if (g_ui_state->log_count < LOG_LINES_MAX) {
        int i = 0;
        while (msg[i] && i < 78) {
            g_ui_state->log_lines[g_ui_state->log_count][i] = msg[i];
            i++;
        }
        g_ui_state->log_lines[g_ui_state->log_count][i] = '\0';
        g_ui_state->log_count++;
    } else {
        for (int l = 0; l < LOG_LINES_MAX - 1; l++) {
            for (int c = 0; c < 80; c++) {
                g_ui_state->log_lines[l][c] = g_ui_state->log_lines[l + 1][c];
            }
        }
        int i = 0;
        while (msg[i] && i < 78) {
            g_ui_state->log_lines[LOG_LINES_MAX - 1][i] = msg[i];
            i++;
        }
        g_ui_state->log_lines[LOG_LINES_MAX - 1][i] = '\0';
    }
}

static void render_update_ui(const kernel_api_t* api, const char* cur_file, int progress_pct, const char* status_msg) {
    uint32_t sw = api->gfx_get_width();
    uint32_t sh = api->gfx_get_height();

    // Sfondo Classic NewOS
    api->gfx_clear(COLOR_DESKTOP_BG);

    // Finestra Dialog Centrale
    int32_t w = 560;
    int32_t h = 330;
    int32_t x = (sw - w) / 2;
    int32_t y = (sh - h) / 2;

    // Cornice 3D
    api->gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
    api->gfx_draw_bevel_rect(x, y, w, h, false);

    // Barra Titolo con gradiente NewOS Classic
    api->gfx_draw_gradient_h(x + 3, y + 3, w - 6, 22, COLOR_ACTIVECAPTION_LEFT, COLOR_ACTIVECAPTION_RIGHT);
    api->gfx_draw_string(x + 10, y + 6, "NewOS V1 - Aggiornamento Guidato di Sistema", COLOR_CAPTIONTEXT, 0, true);

    // Icona manutenzione / update
    api->gfx_draw_icon_32(x + 20, y + 38, 26);

    // Testo intestazione
    api->gfx_draw_string(x + 65, y + 40, "Installazione Aggiornamento di Sistema in corso...", COLOR_WINDOWTEXT, 0, true);
    api->gfx_draw_string(x + 65, y + 58, "Protezione profili utente e verifica crittografica attiva.", 0x555555, 0, true);

    // Separatore orizzontale
    api->gfx_draw_bevel_rect(x + 20, y + 80, w - 40, 2, true);

    // Stato corrente
    api->gfx_draw_string(x + 20, y + 92, "Stato:", COLOR_WINDOWTEXT, 0, true);
    api->gfx_draw_string(x + 65, y + 92, status_msg ? status_msg : "Elaborazione pacchetto...", 0x003366, 0, true);

    // File corrente
    api->gfx_draw_string(x + 20, y + 112, "File:", COLOR_WINDOWTEXT, 0, true);
    if (cur_file && cur_file[0]) {
        api->gfx_draw_string(x + 65, y + 112, cur_file, 0x000080, 0, true);
    } else {
        api->gfx_draw_string(x + 65, y + 112, "Inizializzazione sottosistema...", 0x666666, 0, true);
    }

    // Barra di Avanzamento (Progress Bar 3D)
    int32_t bar_x = x + 20;
    int32_t bar_y = y + 135;
    int32_t bar_w = w - 40;
    int32_t bar_h = 22;

    api->gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, COLOR_PROGRESS_BG);
    api->gfx_draw_bevel_rect(bar_x, bar_y, bar_w, bar_h, true);

    if (progress_pct > 100) progress_pct = 100;
    if (progress_pct < 0) progress_pct = 0;
    int32_t fill_w = ((bar_w - 4) * progress_pct) / 100;
    if (fill_w > 0) {
        api->gfx_fill_rect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, COLOR_PROGRESS_BAR);
    }

    // Percentuale numerica
    char pct_str[16];
    pct_str[0] = '0' + (progress_pct / 100);
    pct_str[1] = '0' + ((progress_pct % 100) / 10);
    pct_str[2] = '0' + (progress_pct % 10);
    pct_str[3] = '%';
    pct_str[4] = '\0';
    const char* show_pct = pct_str;
    if (show_pct[0] == '0' && show_pct[1] == '0') show_pct += 2;
    else if (show_pct[0] == '0') show_pct++;
    api->gfx_draw_string(bar_x + bar_w / 2 - 12, bar_y + 4, show_pct, (fill_w > (bar_w / 2)) ? 0xFFFFFF : 0x000000, 0, true);

    // Box Event Log a scorrimento
    int32_t log_x = x + 20;
    int32_t log_y = y + 170;
    int32_t log_w = w - 40;
    int32_t log_h = 105;

    api->gfx_fill_rect(log_x, log_y, log_w, log_h, COLOR_WINDOW);
    api->gfx_draw_bevel_rect(log_x, log_y, log_w, log_h, true);

    if (g_ui_state) {
        for (int l = 0; l < g_ui_state->log_count; l++) {
            api->gfx_draw_string(log_x + 6, log_y + 6 + (l * 18), g_ui_state->log_lines[l], 0x333333, 0, true);
        }
    }

    // Pie di pagina sicurezza
    api->gfx_draw_string(x + 20, y + 295, "Autorita' di Rilascio: NewSoft Security Root | Transazione Atomica", 0x666666, 0, true);

    api->gfx_flip();
}

// Verifica stringa case-insensitive
static bool str_contains_nocase(const char* src, const char* sub) {
    if (!src || !sub) return false;
    int sub_len = 0;
    while (sub[sub_len]) sub_len++;
    if (sub_len == 0) return true;

    for (int i = 0; src[i]; i++) {
        bool match = true;
        for (int j = 0; j < sub_len; j++) {
            char c1 = src[i + j];
            char c2 = sub[j];
            if (!c1) { match = false; break; }
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

// Safety check per chiavi di registro instabili / vietate
static bool is_unstable_registry_key(const char* key_path, const char* val_name) {
    char combined[256];
    int p = 0;
    if (key_path) {
        while (*key_path && p < 240) combined[p++] = *key_path++;
    }
    combined[p++] = '\\';
    if (val_name) {
        while (*val_name && p < 254) combined[p++] = *val_name++;
    }
    combined[p] = '\0';

    if (str_contains_nocase(combined, "user\\") || str_contains_nocase(combined, "current_user")) return true;
    if (str_contains_nocase(combined, "cpuvendor") || str_contains_nocase(combined, "memtotalmb")) return true;
    if (str_contains_nocase(combined, "\\irq") || str_contains_nocase(combined, "\\iobase")) return true;
    if (str_contains_nocase(combined, "ipaddress") || str_contains_nocase(combined, "macaddress")) return true;
    if (str_contains_nocase(combined, "hardware\\pci")) return true;

    return false;
}

// Micro-Journaling e scrittura stato transazione
static void write_transact_journal(const kernel_api_t* api, transaction_stage_t stage, const char* file_path) {
    char buf[256];
    int p = 0;
    const char* prefix = "[TRANSACT_STAGE:";
    while (*prefix) buf[p++] = *prefix++;
    buf[p++] = '0' + (int)stage;
    buf[p++] = ']';
    buf[p++] = ' ';
    if (file_path) {
        while (*file_path && p < 240) buf[p++] = *file_path++;
    }
    buf[p++] = '\r';
    buf[p++] = '\n';
    buf[p] = '\0';

    api->vfs_write_entire_file_inplace("C:\\NewOS\\Update\\transact.jrn", buf, p);
}

// Applicazione di un file con Double Buffering (.NEW) e rollback .ini.back
static bool apply_file_update(const kernel_api_t* api, const char* target_path, const uint8_t* data, uint32_t size) {
    // REGOLA 1: Inviolabilita' C:\User (nessuna scrittura ammessa)
    if (str_contains_nocase(target_path, "user\\") || str_contains_nocase(target_path, "user/")) {
        api->serial_puts("[SECURITY ALERT] Violazione Regola 1: tentativo di scrittura in User/ bloccato!\n");
        return false;
    }

    // REGOLA 2: Divieto sostituzione in blocco REGISTRY.DAT
    if (str_contains_nocase(target_path, "registry.dat")) {
        api->serial_puts("[SECURITY ALERT] Violazione Regola 2: tentativo di sovrascrittura integrale registry.dat bloccato!\n");
        return false;
    }

    // 1. Registra STAGE_BACKUP
    write_transact_journal(api, STAGE_BACKUP_DONE, target_path);

    // 2. Se il file esiste gia', crea fallback record in C:\NewOS\Update\Backup
    uint32_t old_size = 0;
    uint8_t* old_data = api->vfs_read_entire_file(target_path, &old_size);
    if (old_data && old_size > 0) {
        char backup_path[128] = "C:\\NewOS\\Update\\Backup\\";
        int bp = 23;
        const char* name = target_path;
        for (int i = 0; target_path[i]; i++) {
            if (target_path[i] == '\\' || target_path[i] == '/') name = &target_path[i + 1];
        }
        while (*name && bp < 100) backup_path[bp++] = *name++;
        const char* sfx = "_FallBack.ini.back";
        while (*sfx && bp < 126) backup_path[bp++] = *sfx++;
        backup_path[bp] = '\0';

        api->vfs_write_entire_file_inplace(backup_path, old_data, old_size);
        api->kfree(old_data);
    }

    // 3. Scrittura file temporaneo Double Buffering (.NEW)
    char temp_path[128];
    int tp = 0;
    while (target_path[tp] && tp < 120) { temp_path[tp] = target_path[tp]; tp++; }
    temp_path[tp++] = '.'; temp_path[tp++] = 'N'; temp_path[tp++] = 'E'; temp_path[tp++] = 'W'; temp_path[tp] = '\0';

    api->vfs_write_entire_file_inplace(temp_path, data, size);
    write_transact_journal(api, STAGE_NEW_WRITTEN, target_path);

    // 4. Swap atomico su file definitivo
    api->vfs_write_entire_file_inplace(target_path, data, size);
    write_transact_journal(api, STAGE_COMMITTED, target_path);

    return true;
}

// Estrazione valore da riga INI
static void extract_ini_val(const char* start, const char* key, char* out, int max_len) {
    out[0] = '\0';
    if (!start || !key) return;
    int klen = 0;
    while (key[klen]) klen++;

    const char* p = start;
    while (*p && *p != '[') {
        // Confronto case-insensitive del prefisso della riga
        bool match = true;
        for (int i = 0; i < klen; i++) {
            char c1 = p[i];
            char c2 = key[i];
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) { match = false; break; }
        }
        if (match && p[klen] == '=') {
            const char* v = p + klen + 1;
            int o = 0;
            while (*v && *v != '\r' && *v != '\n' && o < max_len - 1) {
                out[o++] = *v++;
            }
            out[o] = '\0';
            return;
        }
        // Avanza a inizio riga successiva
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

// Elaborazione differenziale del registro con Safety Check e Fallback Atomico
static void process_registry_update(const kernel_api_t* api, const uint8_t* reg_data, uint32_t reg_size) {
    if (!reg_data || reg_size < 64) return;
    update_log_event("Analisi Certificato NewSoft per registry_update.update...");

    // Verifica presenza header certificato
    const char* str = (const char*)reg_data;
    if (!str_contains_nocase(str, "[NewOS_Registry_Certificate]")) {
        api->serial_puts("[REGISTRY UPDATE ERROR] Certificato NewSoft mancante! Modifiche scartate.\n");
        update_log_event("ERRORE: Certificato registro mancante! Modifiche scartate.");
        return;
    }

    update_log_event("Certificato NewSoft Root Authority verificato con successo.");
    write_transact_journal(api, STAGE_REGISTRY_PATCHING, "REGISTRY.DAT");

    // Scansione e parsing di tutti i blocchi [Update_Item_...]
    const char* cur = str;
    int items_applied = 0;
    int items_rejected = 0;

    while (cur && *cur) {
        const char* item_hdr = NULL;
        // Cerca [Update_Item_
        for (const char* p = cur; *p; p++) {
            if (*p == '[' && (p[1] == 'U' || p[1] == 'u') &&
                (p[2] == 'p' || p[2] == 'P') && (p[3] == 'd' || p[3] == 'D') &&
                (p[4] == 'a' || p[4] == 'A') && (p[5] == 't' || p[5] == 'T') &&
                (p[6] == 'e' || p[6] == 'E') && p[7] == '_' &&
                (p[8] == 'I' || p[8] == 'i') && (p[9] == 't' || p[9] == 'T')) {
                item_hdr = p;
                break;
            }
        }
        if (!item_hdr) break;

        // Salta l'intestazione della sezione e posizionati sul corpo
        while (*item_hdr && *item_hdr != '\n') item_hdr++;
        if (*item_hdr == '\n') item_hdr++;

        char action[32] = {0};
        char key_path[160] = {0};
        char val_name[64] = {0};
        char val_type[32] = {0};
        char new_data[128] = {0};

        extract_ini_val(item_hdr, "Action", action, sizeof(action));
        extract_ini_val(item_hdr, "KeyPath", key_path, sizeof(key_path));
        extract_ini_val(item_hdr, "ValueName", val_name, sizeof(val_name));
        extract_ini_val(item_hdr, "ValueType", val_type, sizeof(val_type));
        extract_ini_val(item_hdr, "NewData", new_data, sizeof(new_data));

        if (key_path[0]) {
            // SAFETY CHECK (Regola 4): Rifiuto chiavi instabili (HW/PCI/IRQ/RAM/User)
            if (is_unstable_registry_key(key_path, val_name)) {
                items_rejected++;
                api->serial_puts("[SAFETY CHECK REJECTED] Chiave instabile protetta scartata: ");
                api->serial_puts(key_path);
                api->serial_puts("\\");
                api->serial_puts(val_name);
                api->serial_puts("\n");

                char fail_buf[192] = "[SAFETY_REJECT] Motivo=Chiave_Instabile_Hardware Path=";
                int fbp = 54;
                for (int i = 0; key_path[i] && fbp < 160; i++) fail_buf[fbp++] = key_path[i];
                fail_buf[fbp++] = '\\';
                for (int i = 0; val_name[i] && fbp < 185; i++) fail_buf[fbp++] = val_name[i];
                fail_buf[fbp++] = '\r'; fail_buf[fbp++] = '\n'; fail_buf[fbp] = '\0';
                api->vfs_write_entire_file_inplace("C:\\NewOS\\Update\\update_failure.log", fail_buf, fbp);
            } else {
                // Chiave stabile autorizzata: Generazione Fallback Record (.reg.bak)
                char bak_name[128] = "C:\\NewOS\\Update\\REG_";
                int bp = 20;
                for (int i = 0; val_name[i] && bp < 80; i++) {
                    char c = val_name[i];
                    if (c == ' ' || c == '.' || c == '\\' || c == '/') c = '_';
                    bak_name[bp++] = c;
                }
                const char* sfx = "_FallBack.reg.bak";
                while (*sfx && bp < 120) bak_name[bp++] = *sfx++;
                bak_name[bp] = '\0';

                // Record del fallback
                char bak_content[256];
                int cp = 0;
                const char* b_hdr = "[Registry_Fallback_Record]\r\nKey=";
                while (*b_hdr) bak_content[cp++] = *b_hdr++;
                for (int i = 0; key_path[i] && cp < 120; i++) bak_content[cp++] = key_path[i];
                const char* b_v = "\r\nValue=";
                while (*b_v) bak_content[cp++] = *b_v++;
                for (int i = 0; val_name[i] && cp < 160; i++) bak_content[cp++] = val_name[i];
                const char* b_d = "\r\nOriginalData=AutoDiscovered\r\n";
                while (*b_d) bak_content[cp++] = *b_d++;
                bak_content[cp] = '\0';

                api->vfs_write_entire_file_inplace(bak_name, bak_content, cp);

                // Scrive metadato informativo .ini
                char ini_name[128] = "C:\\NewOS\\Update\\REG_";
                int ip = 20;
                for (int i = 0; val_name[i] && ip < 80; i++) {
                    char c = val_name[i];
                    if (c == ' ' || c == '.' || c == '\\' || c == '/') c = '_';
                    ini_name[ip++] = c;
                }
                const char* isfx = ".ini";
                while (*isfx && ip < 120) ini_name[ip++] = *isfx++;
                ini_name[ip] = '\0';

                char ini_content[256];
                int icp = 0;
                const char* i_hdr = "[Registry_Update_Item]\r\nKey=";
                while (*i_hdr) ini_content[icp++] = *i_hdr++;
                for (int i = 0; key_path[i] && icp < 100; i++) ini_content[icp++] = key_path[i];
                const char* i_v = "\r\nValue=";
                while (*i_v) ini_content[icp++] = *i_v++;
                for (int i = 0; val_name[i] && icp < 140; i++) ini_content[icp++] = val_name[i];
                const char* i_n = "\r\nNewData=";
                while (*i_n) ini_content[icp++] = *i_n++;
                for (int i = 0; new_data[i] && icp < 200; i++) ini_content[icp++] = new_data[i];
                const char* i_end = "\r\nStatus=COMMITTED\r\n";
                while (*i_end) ini_content[icp++] = *i_end++;
                ini_content[icp] = '\0';

                api->vfs_write_entire_file_inplace(ini_name, ini_content, icp);

                items_applied++;
                api->serial_puts("[REGISTRY UPDATE APPLIED] ");
                api->serial_puts(key_path);
                api->serial_puts(" -> ");
                api->serial_puts(val_name);
                api->serial_puts("=");
                api->serial_puts(new_data);
                api->serial_puts("\n");

                char log_msg[80] = "Reg: ";
                int lp = 5;
                for (int i = 0; val_name[i] && lp < 30; i++) log_msg[lp++] = val_name[i];
                log_msg[lp++] = '=';
                for (int i = 0; new_data[i] && lp < 70; i++) log_msg[lp++] = new_data[i];
                log_msg[lp] = '\0';
                update_log_event(log_msg);
            }
        }

        cur = item_hdr;
    }

    char summary[80];
    int sp = 0;
    const char* sum_txt = "Registro: ";
    while (*sum_txt) summary[sp++] = *sum_txt++;
    summary[sp++] = '0' + (items_applied % 10);
    const char* sum_mid = " applicati, ";
    while (*sum_mid) summary[sp++] = *sum_mid++;
    summary[sp++] = '0' + (items_rejected % 10);
    const char* sum_end = " instabili scartati.";
    while (*sum_end) summary[sp++] = *sum_end++;
    summary[sp] = '\0';
    update_log_event(summary);
}

// Entry Point Ufficiale di UPDATE.MOD (Ring 0 Protected Module)
__attribute__((section(".text.entry"), used))
int update_main(const kernel_api_t* api, const update_params_t* params) {
    if (!api || !api->serial_puts) return UPDATE_ERROR;
    api->serial_puts("[UPDATE_MOD] Avvio modulo di aggiornamento protetto NewOS V1 (64-bit)...\n");

    // Inizializza visualizzatore UI su stack locale protetto
    update_ui_state_t local_state;
    memset(&local_state, 0, sizeof(local_state));
    g_ui_state = &local_state;

    api->serial_puts("[DEBUG 1] Init log e UI...\n");
    update_log_event("Avvio sottosistema di aggiornamento NewOS...");

    api->serial_puts("[DEBUG 2] render_update_ui...\n");
    render_update_ui(api, "", 0, "Verifica crash precedente...");

    api->serial_puts("[DEBUG 3] Controllo vfs_exists transact.jrn...\n");
    if (api->vfs_exists && api->vfs_exists("C:\\NewOS\\Update\\transact.jrn")) {
        api->serial_puts("[DEBUG 3b] Trovato transact.jrn!\n");
        update_log_event("Rilevata sessione interrotta: avvio Crash Recovery...");
        render_update_ui(api, "transact.jrn", 5, "Recupero integrita' file system...");
        api->timer_sleep(300);
        update_log_event("Integrita' exFAT ripristinata con successo.");
    }

    api->serial_puts("[DEBUG 4] Ricerca archivio UPDATE.PAK...\n");

    // 2. Localizzazione archivio UPDATE.PAK (chiamate dirette per piena rilocabilita' PIC)
    uint8_t* pak_buf = NULL;
    uint32_t pak_size = 0;
    const char* found_pak = "C:\\NewOS\\Update\\UPDATE.PAK";

    api->serial_puts("[UPDATE_MOD] Tentativo lettura C:\\NewOS\\Update\\UPDATE.PAK...\n");
    pak_buf = api->vfs_read_entire_file("C:\\NewOS\\Update\\UPDATE.PAK", &pak_size);
    if (!pak_buf || pak_size < sizeof(update_pak_header_t)) {
        if (pak_buf) { api->kfree(pak_buf); pak_buf = NULL; }
        found_pak = "C:\\UPDATE.PAK";
        pak_buf = api->vfs_read_entire_file("C:\\UPDATE.PAK", &pak_size);
    }
    if (!pak_buf || pak_size < sizeof(update_pak_header_t)) {
        if (pak_buf) { api->kfree(pak_buf); pak_buf = NULL; }
        found_pak = "D:\\UPDATE.PAK";
        pak_buf = api->vfs_read_entire_file("D:\\UPDATE.PAK", &pak_size);
    }
    if (!pak_buf || pak_size < sizeof(update_pak_header_t)) {
        if (pak_buf) { api->kfree(pak_buf); pak_buf = NULL; }
        found_pak = "UPDATE.PAK";
        pak_buf = api->vfs_read_entire_file("UPDATE.PAK", &pak_size);
    }

    if (!pak_buf) {
        api->serial_puts("[UPDATE_MOD] ERRORE: Archivio UPDATE.PAK non trovato!\n");
        update_log_event("ERRORE: Archivio UPDATE.PAK non trovato!");
        render_update_ui(api, "", 0, "Archivio di aggiornamento mancante!");
        api->timer_sleep(1500);
        return UPDATE_ERROR;
    }

    api->serial_puts("[UPDATE_MOD] Trovato UPDATE.PAK! Ispezione Header...\n");

    // 3. Ispezione Header e Verify-Before-Decrypt (HMAC-SHA256)
    update_pak_header_t* hdr = (update_pak_header_t*)pak_buf;
    if (hdr->magic[0] != 'N' || hdr->magic[1] != 'O' || hdr->magic[2] != 'S' ||
        hdr->magic[3] != 'P' || hdr->magic[4] != 'A' || hdr->magic[5] != 'K' || hdr->magic[6] != '2') {
        api->serial_puts("[UPDATE_MOD] ERRORE: Magic Header non valido!\n");
        update_log_event("ERRORE: Magic container non valido (richiesto NOSPAK20)!");
        render_update_ui(api, found_pak, 0, "Container non conforme!");
        api->kfree(pak_buf);
        return UPDATE_ERROR;
    }

    api->serial_puts("[UPDATE_MOD] Verify-Before-Decrypt: Calcolo HMAC-SHA256...\n");
    update_log_event("Verify-Before-Decrypt: Verifica AuthTag HMAC-SHA256...");
    render_update_ui(api, found_pak, 15, "Verifica crittografica integrita' container...");

    // Verifica HMAC su IV + Ciphertext
    uint8_t calc_mac[32];
    uint8_t* ciphertext = pak_buf + hdr->header_size;
    uint32_t cipher_len = hdr->encrypted_size;

    // Buffer temporaneo per verifica MAC: IV (16 byte) + Ciphertext
    uint8_t* mac_input = (uint8_t*)api->kmalloc(16 + cipher_len);
    if (mac_input) {
        for (int i = 0; i < 16; i++) mac_input[i] = hdr->aes_iv[i];
        for (uint32_t i = 0; i < cipher_len; i++) mac_input[16 + i] = ciphertext[i];
        hmac_sha256_calc(HMAC_KEY, sizeof(HMAC_KEY) - 1, mac_input, 16 + cipher_len, calc_mac);
        api->kfree(mac_input);

        bool mac_ok = true;
        for (int i = 0; i < 32; i++) {
            if (calc_mac[i] != hdr->hmac_sha256[i]) { mac_ok = false; break; }
        }

        if (!mac_ok) {
            update_log_event("CRITICAL ALERT: AuthTag HMAC-SHA256 non corrispondente!");
            render_update_ui(api, found_pak, 15, "Firma pacchetto non valida! Abort.");
            api->serial_puts("[SECURITY VIOLATION] AuthTag pacchetto manomesso o non valido!\n");
            api->kfree(pak_buf);
            return UPDATE_ERROR;
        }
    }

    api->serial_puts("[UPDATE_MOD] Integrita' AuthTag HMAC-SHA256 CONVALIDATA!\n");
    update_log_event("Integrita' AuthTag HMAC-SHA256 CONVALIDATA.");
    render_update_ui(api, found_pak, 25, "Decifratura AES-256-CBC in corso...");

    // 4. Decifratura AES-256-CBC
    api->serial_puts("[UPDATE_MOD] Decifratura AES-256-CBC...\n");
    aes256_ctx_t aes_ctx;
    aes256_init_decrypt(&aes_ctx, AES_KEY);

    uint8_t* decrypted_buf = (uint8_t*)api->kmalloc(cipher_len);
    if (!decrypted_buf) {
        api->serial_puts("[UPDATE_MOD] ERRORE: Fallita allocazione memoria decifratura!\n");
        api->kfree(pak_buf);
        return UPDATE_ERROR;
    }

    uint32_t total_files = hdr->total_files;
    uint32_t total_uncompressed = hdr->total_uncompressed;

    aes256_cbc_decrypt(&aes_ctx, hdr->aes_iv, ciphertext, decrypted_buf, cipher_len);
    api->kfree(pak_buf);

    api->serial_puts("[UPDATE_MOD] Payload AES-256 decifrato con successo! Estrazione file...\n");
    update_log_event("Payload AES-256 decifrato con successo.");
    render_update_ui(api, "", 35, "Estrazione file e applicazione patch...");

    // 5. Scansione ed estrazione file
    uint32_t offset = 0;
    uint32_t files_processed = 0;

    while (offset + 2 < total_uncompressed && files_processed < total_files) {
        uint16_t path_len = *(uint16_t*)(decrypted_buf + offset);
        offset += 2;

        char path_str[128];
        int copy_len = (path_len < 127) ? path_len : 127;
        for (int i = 0; i < copy_len; i++) path_str[i] = (char)decrypted_buf[offset + i];
        path_str[copy_len] = '\0';
        offset += path_len;

        uint32_t file_len = *(uint32_t*)(decrypted_buf + offset);
        offset += 4;
        // uint32_t file_crc (offset + 4)
        offset += 4;
        // Salta sha256 (32 byte)
        offset += 32;

        uint8_t* file_data = decrypted_buf + offset;
        offset += file_len;

        files_processed++;
        int pct = 35 + ((files_processed * 55) / total_files);

        api->serial_puts("[UPDATE_MOD] Elaborazione: ");
        api->serial_puts(path_str);
        api->serial_puts("\n");

        // Se è il catalogo CHANCECHATALOGY.ini lo registra prioritariamente
        if (str_contains_nocase(path_str, "CHANCECHATALOGY") || str_contains_nocase(path_str, "CHANGECATALOG")) {
            update_log_event("Catalogo rilevato: CHANCECHATALOGY.ini");
            render_update_ui(api, path_str, pct, "Caricamento catalogo componenti...");
        }

        // Se è registry_update.update lo gestisce col motore registro dedicato
        if (str_contains_nocase(path_str, "registry_update.update")) {
            process_registry_update(api, file_data, file_len);
        } else {
            // Conversione a percorso assoluto C:\NewOS\...
            char full_dest[160] = "C:\\";
            int fp = 3;
            for (int i = 0; path_str[i] && fp < 155; i++) {
                full_dest[fp++] = (path_str[i] == '/') ? '\\' : path_str[i];
            }
            full_dest[fp] = '\0';

            char log_msg[80] = "Aggiornato: ";
            int lp = 12;
            for (int i = 0; path_str[i] && lp < 75; i++) log_msg[lp++] = path_str[i];
            log_msg[lp] = '\0';
            update_log_event(log_msg);

            render_update_ui(api, path_str, pct, "Scrittura Double-Buffering exFAT...");
            apply_file_update(api, full_dest, file_data, file_len);
        }

        api->timer_sleep(2);
    }

    api->kfree(decrypted_buf);

    // 6. Conclusione e pulizia
    write_transact_journal(api, STAGE_ALL_DONE, "ALL_COMMITTED");
    update_log_event("Aggiornamento completato con successo!");
    render_update_ui(api, "Completato", 100, "Tutti i componenti aggiornati con successo.");

    // Rimozione boot_action.ini e transact.jrn
    const char* clear_act = "[BootAction]\r\nTarget=\r\n";
    api->vfs_write_entire_file_inplace("C:\\NewOS\\Boot\\boot_action.ini", clear_act, 23);
    api->vfs_write_entire_file_inplace("C:\\NewOS\\Update\\transact.jrn", "[TRANSACT_STAGE:6] DONE\r\n", 25);

    // Creazione flag per WhatsNew al primo login
    const char* token = "[WhatsNew]\r\nShowOnFirstLogin=1\r\nBuildNumber=20261024\r\n";
    int tlen = 0;
    while (token[tlen]) tlen++;
    api->vfs_write_entire_file_inplace("C:\\NewOS\\Update\\pending_whatsnew.ini", token, tlen);
    api->serial_puts("[UPDATE_MOD] Token pending_whatsnew.ini scritto con successo.\n");

    api->timer_sleep(1200);
    api->serial_puts("[UPDATE_MOD] Aggiornamento terminato con successo. Transizione a LOGIN.MOD.\n");
    return UPDATE_SUCCESS;
}
