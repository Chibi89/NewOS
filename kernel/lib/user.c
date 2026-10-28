#include <user.h>
#include <string.h>
#include <fs/vfs.h>
#include <mm/heap.h>

static user_profile_t current_user;
static bool live_mode = true;

static void trim_crlf(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

void user_init(void) {
    // Configurazione predefinita
    strcpy(current_user.username, "Morgan");
    strcpy(current_user.password, "");
    strcpy(current_user.computer_name, "NEWOS-PC");
    current_user.preferred_theme = 0; // Classic
    current_user.power_profile = 0;   // Balanced
    strcpy(current_user.desktop_path, "/USER/MORGAN/DESKTOP");
    current_user.is_installed = false;
    live_mode = true;

    // 1. Interroga prioritariamente C:\NewOS\OOBE\OOBE.INI per rilevare DefaultUser=
    uint32_t ini_size = 0;
    char* ini = (char*)vfs_read_entire_file("C:\\NewOS\\OOBE\\OOBE.INI", &ini_size);
    if (!ini) ini = (char*)vfs_read_entire_file("/NewOS/OOBE/OOBE.INI", &ini_size);
    if (ini && ini_size > 0) {
        char* line = ini;
        while (line && *line) {
            char* next = strchr(line, '\n');
            if (next) *next = '\0';
            trim_crlf(line);
            if (strncmp(line, "DefaultUser=", 12) == 0 && line[12] != '\0') {
                strncpy(current_user.username, line + 12, 31);
                current_user.username[31] = '\0';
            } else if (strncmp(line, "Completed=1", 11) == 0) {
                current_user.is_installed = true;
                live_mode = false;
            }
            if (next) line = next + 1;
            else break;
        }
        kfree(ini);
    }

    // 2. Se la cartella dell'utente corrente non esiste su C:\User\<username>, scansiona C:\User
    char check_dir[128];
    mini_snprintf(check_dir, sizeof(check_dir), "C:\\User\\%s", current_user.username);
    if (!vfs_exists(check_dir)) {
        vfs_dirent_t entries[16];
        int32_t cnt = vfs_readdir("C:\\User", entries, 16);
        for (int32_t i = 0; i < cnt; i++) {
            if (entries[i].is_directory &&
                strcasecmp(entries[i].name, ".") != 0 &&
                strcasecmp(entries[i].name, "..") != 0 &&
                strcasecmp(entries[i].name, "Default") != 0) {
                strncpy(current_user.username, entries[i].name, 31);
                current_user.username[31] = '\0';
                break;
            }
        }
    }

    // 3. Tentativo di caricamento configurazione salvata da disco (C:\User\<username>\USER.CFG)
    uint32_t fsize = 0;
    char cfg_path[128];
    mini_snprintf(cfg_path, sizeof(cfg_path), "C:\\User\\%s\\USER.CFG", current_user.username);
    char* cfg = (char*)vfs_read_entire_file(cfg_path, &fsize);
    if (!cfg) {
        mini_snprintf(cfg_path, sizeof(cfg_path), "/User/%s/USER.CFG", current_user.username);
        cfg = (char*)vfs_read_entire_file(cfg_path, &fsize);
    }
    if (!cfg) cfg = (char*)vfs_read_entire_file("C:\\User\\Morgan\\USER.CFG", &fsize);
    if (!cfg) cfg = (char*)vfs_read_entire_file("C:\\User\\USER.CFG", &fsize);
    if (!cfg) cfg = (char*)vfs_read_entire_file("/User/Morgan/USER.CFG", &fsize);
    if (!cfg) cfg = (char*)vfs_read_entire_file("/USER/USER.CFG", &fsize);
    if (!cfg) cfg = (char*)vfs_read_entire_file("/SYSTEM/USER.CFG", &fsize);

    if (cfg && fsize > 0) {
        // Formato semplice chiave=valore:
        // USER=nome
        // PASS=pwd
        // HOST=pc
        // THEME=0/1
        // POWER=0/1/2
        // INSTALLED=1
        char* line = cfg;
        while (line && *line) {
            char* next = strchr(line, '\n');
            if (next) *next = '\0';
            trim_crlf(line);

            if (strncmp(line, "USER=", 5) == 0 && line[5] != '\0') {
                strncpy(current_user.username, line + 5, 31);
                current_user.username[31] = '\0';
            } else if (strncmp(line, "PASS=", 5) == 0) {
                strncpy(current_user.password, line + 5, 31);
                current_user.password[31] = '\0';
            } else if (strncmp(line, "HOST=", 5) == 0 && line[5] != '\0') {
                strncpy(current_user.computer_name, line + 5, 31);
                current_user.computer_name[31] = '\0';
            } else if (strncmp(line, "THEME=", 6) == 0) {
                current_user.preferred_theme = line[6] - '0';
            } else if (strncmp(line, "POWER=", 6) == 0) {
                current_user.power_profile = line[6] - '0';
            } else if (strncmp(line, "INSTALLED=1", 11) == 0) {
                current_user.is_installed = true;
                live_mode = false;
            }

            if (next) line = next + 1;
            else break;
        }
        kfree(cfg);
    }

    // Aggiorna percorso desktop dell'utente
    strcpy(current_user.desktop_path, "/USER/");
    strcat(current_user.desktop_path, current_user.username);
    strcat(current_user.desktop_path, "/DESKTOP");
}

user_profile_t* user_get_current(void) {
    return &current_user;
}

bool user_create_or_migrate(const char* old_username, const char* new_username) {
    char clean_new[32];
    if (!new_username) return false;

    // Salta spazi iniziali
    while (*new_username == ' ' || *new_username == '\t') new_username++;
    strncpy(clean_new, new_username, 31);
    clean_new[31] = '\0';
    trim_crlf(clean_new);

    if (clean_new[0] == '\0' || strcmp(clean_new, ".") == 0 || strcmp(clean_new, "..") == 0 ||
        strcasecmp(clean_new, "Default") == 0) {
        strcpy(clean_new, "Morgan");
    }

    const char* src_user = (old_username && old_username[0]) ? old_username : "Morgan";

    if (!vfs_exists("C:\\User")) {
        vfs_mkdir("C:\\User");
    }

    char old_dir[128];
    char new_dir[128];
    mini_snprintf(old_dir, sizeof(old_dir), "C:\\User\\%s", src_user);
    mini_snprintf(new_dir, sizeof(new_dir), "C:\\User\\%s", clean_new);

    // Se il nuovo nome utente è diverso da quello predefinito (es. "Morgan" -> "Mario")
    if (strcasecmp(src_user, clean_new) != 0) {
        if (vfs_exists(old_dir) && !vfs_exists(new_dir)) {
            vfs_rename(old_dir, new_dir);
        }
        // Se per qualsiasi motivo old_dir ("Morgan") fosse ancora presente dopo la creazione di new_dir
        if (vfs_exists(new_dir) && vfs_exists(old_dir)) {
            vfs_unlink(old_dir);
        }
    }

    // Assicura l'esistenza dell'intera gerarchia di cartelle sotto C:\User\<clean_new>
    if (!vfs_exists(new_dir)) {
        vfs_mkdir(new_dir);
    }

    char sub_path[160];
    mini_snprintf(sub_path, sizeof(sub_path), "C:\\User\\%s\\Desktop", clean_new);
    if (!vfs_exists(sub_path)) vfs_mkdir(sub_path);

    mini_snprintf(sub_path, sizeof(sub_path), "C:\\User\\%s\\Desktop\\PROGETTI", clean_new);
    if (!vfs_exists(sub_path)) vfs_mkdir(sub_path);

    mini_snprintf(sub_path, sizeof(sub_path), "C:\\User\\%s\\Miusic", clean_new);
    if (!vfs_exists(sub_path)) vfs_mkdir(sub_path);

    mini_snprintf(sub_path, sizeof(sub_path), "C:\\User\\%s\\Video", clean_new);
    if (!vfs_exists(sub_path)) vfs_mkdir(sub_path);

    // Assicura la presenza dei documenti di benvenuto sul Desktop del nuovo profilo
    mini_snprintf(sub_path, sizeof(sub_path), "C:\\User\\%s\\Desktop\\NOTE.TXT", clean_new);
    if (!vfs_exists(sub_path)) {
        const char* note_txt = "Appunti personali sul Desktop di NewOS V1 (64-bit).\r\n";
        vfs_write_entire_file_inplace(sub_path, note_txt, (uint32_t)strlen(note_txt));
    }

    mini_snprintf(sub_path, sizeof(sub_path), "C:\\User\\%s\\Desktop\\BENVENUTO.TXT", clean_new);
    if (!vfs_exists(sub_path)) {
        char welcome_txt[160];
        mini_snprintf(welcome_txt, sizeof(welcome_txt),
                      "Benvenuto %s nel tuo nuovo ambiente Desktop NewOS V1 (64-bit)!\r\n", clean_new);
        vfs_write_entire_file_inplace(sub_path, welcome_txt, (uint32_t)strlen(welcome_txt));
    }

    strncpy(current_user.username, clean_new, 31);
    current_user.username[31] = '\0';
    strcpy(current_user.desktop_path, "/USER/");
    strcat(current_user.desktop_path, current_user.username);
    strcat(current_user.desktop_path, "/DESKTOP");

    return true;
}

void user_set_profile(const char* username, const char* password, const char* comp_name, int theme, int power, bool installed) {
    char prev_user[32];
    strncpy(prev_user, current_user.username[0] ? current_user.username : "Morgan", 31);
    prev_user[31] = '\0';

    if (username && username[0]) {
        strncpy(current_user.username, username, 31);
        current_user.username[31] = '\0';
        trim_crlf(current_user.username);
    }
    if (password) {
        strncpy(current_user.password, password, 31);
        current_user.password[31] = '\0';
    }
    if (comp_name && comp_name[0]) {
        strncpy(current_user.computer_name, comp_name, 31);
        current_user.computer_name[31] = '\0';
        trim_crlf(current_user.computer_name);
    }
    current_user.preferred_theme = theme;
    current_user.power_profile = power;
    current_user.is_installed = installed;
    live_mode = !installed;

    // Se installato su disco (es. fine OOBE), migra o crea la struttura fisica C:\User\<username>
    if (installed && current_user.username[0] && strcasecmp(current_user.username, "Ospite") != 0) {
        user_create_or_migrate("Morgan", current_user.username);
        if (strcasecmp(prev_user, "Morgan") != 0 && strcasecmp(prev_user, current_user.username) != 0) {
            user_create_or_migrate(prev_user, current_user.username);
        }
    }

    // Ricostruisce percorso desktop
    strcpy(current_user.desktop_path, "/USER/");
    strcat(current_user.desktop_path, current_user.username);
    strcat(current_user.desktop_path, "/DESKTOP");
}

bool user_save_config(void) {
    const char* uname = current_user.username[0] ? current_user.username : "Morgan";

    // Assicura che la directory C:\User\<uname> esista prima del salvataggio
    if (current_user.is_installed && strcasecmp(uname, "Ospite") != 0) {
        user_create_or_migrate("Morgan", uname);
    }

    char cfg[256];
    mini_snprintf(cfg, sizeof(cfg),
        "USER=%s\nPASS=%s\nHOST=%s\nTHEME=%d\nPOWER=%d\nINSTALLED=%d\n",
        uname,
        current_user.password,
        current_user.computer_name[0] ? current_user.computer_name : "NEWOS-PC",
        current_user.preferred_theme,
        current_user.power_profile,
        current_user.is_installed ? 1 : 0);

    char path[128];
    mini_snprintf(path, sizeof(path), "C:\\User\\%s\\USER.CFG", uname);
    vfs_write_entire_file_inplace(path, cfg, (uint32_t)strlen(cfg));
    return true;
}

bool user_is_live_mode(void) {
    return live_mode;
}

void user_set_live_mode(bool is_live) {
    live_mode = is_live;
}
