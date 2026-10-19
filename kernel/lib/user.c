#include <user.h>
#include <string.h>
#include <fs/vfs.h>
#include <mm/heap.h>

static user_profile_t current_user;
static bool live_mode = true;

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

    // Tentativo di caricamento configurazione salvata da disco (C:\User\Morgan\USER.CFG o C:\User\USER.CFG)
    uint32_t fsize = 0;
    char* cfg = (char*)vfs_read_entire_file("C:\\User\\Morgan\\USER.CFG", &fsize);
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

            if (strncmp(line, "USER=", 5) == 0) {
                strncpy(current_user.username, line + 5, 31);
            } else if (strncmp(line, "PASS=", 5) == 0) {
                strncpy(current_user.password, line + 5, 31);
            } else if (strncmp(line, "HOST=", 5) == 0) {
                strncpy(current_user.computer_name, line + 5, 31);
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

        // Aggiorna percorso desktop dell'utente
        strcpy(current_user.desktop_path, "/USER/");
        strcat(current_user.desktop_path, current_user.username);
        strcat(current_user.desktop_path, "/DESKTOP");
    }
}

user_profile_t* user_get_current(void) {
    return &current_user;
}

void user_set_profile(const char* username, const char* password, const char* comp_name, int theme, int power, bool installed) {
    if (username && username[0]) {
        strncpy(current_user.username, username, 31);
    }
    if (password) {
        strncpy(current_user.password, password, 31);
    }
    if (comp_name && comp_name[0]) {
        strncpy(current_user.computer_name, comp_name, 31);
    }
    current_user.preferred_theme = theme;
    current_user.power_profile = power;
    current_user.is_installed = installed;
    live_mode = !installed;

    // Ricostruisce percorso desktop
    strcpy(current_user.desktop_path, "/USER/");
    strcat(current_user.desktop_path, current_user.username);
    strcat(current_user.desktop_path, "/DESKTOP");
}

bool user_save_config(void) {
    char cfg[256];
    mini_snprintf(cfg, sizeof(cfg),
        "USER=%s\nPASS=%s\nHOST=%s\nTHEME=%d\nPOWER=%d\nINSTALLED=%d\n",
        current_user.username[0] ? current_user.username : "Morgan",
        current_user.password,
        current_user.computer_name[0] ? current_user.computer_name : "NEWOS-PC",
        current_user.preferred_theme,
        current_user.power_profile,
        current_user.is_installed ? 1 : 0);

    char path[128];
    mini_snprintf(path, sizeof(path), "C:\\User\\%s\\USER.CFG", current_user.username[0] ? current_user.username : "Morgan");
    vfs_write_entire_file_inplace(path, cfg, strlen(cfg));
    return true;
}

bool user_is_live_mode(void) {
    return live_mode;
}

void user_set_live_mode(bool is_live) {
    live_mode = is_live;
}
