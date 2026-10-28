#ifndef USER_H
#define USER_H

#include <types.h>

typedef struct {
    char username[32];
    char password[32];
    char computer_name[32];
    int preferred_theme;    // 0: THEME_CLASSIC, 1: THEME_LIQUID_GLASS
    int power_profile;      // 0: Balanced, 1: Saver, 2: Performance
    char desktop_path[64];  // "/USER/[username]/DESKTOP"
    bool is_installed;      // true se installato su HD, false se Live USB
} user_profile_t;

// Inizializza il profilo utente corrente cercando configurazioni su disco
void user_init(void);

// Restituisce il profilo utente attivo
user_profile_t* user_get_current(void);

// Configura i parametri utente (richiamato dall'Installer o dal Pannello di Controllo)
void user_set_profile(const char* username, const char* password, const char* comp_name, int theme, int power, bool installed);

// Crea o migra la directory fisica del profilo utente in C:\User\<new_username>
bool user_create_or_migrate(const char* old_username, const char* new_username);

// Salva la configurazione utente corrente
bool user_save_config(void);

// Modalità Live
bool user_is_live_mode(void);
void user_set_live_mode(bool is_live);

#endif // USER_H
