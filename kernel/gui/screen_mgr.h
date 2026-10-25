#ifndef GUI_SCREEN_MGR_H
#define GUI_SCREEN_MGR_H

#include <types.h>
#include <screen_modules.h>

// Inizializza il Gestore dei Moduli Schermata Protetti e l'Export Table
void screen_mgr_init(void);

// Restituisce la tabella API esposta ai moduli
const kernel_api_t* screen_mgr_get_api(void);

// Esegue il Bootscreen (carica C:\SYSTEM\SCREENS\BOOTSCR.MOD con fallback sicuro)
bootscreen_result_t screen_mgr_run_bootscreen(void);

// Esegue il Loginscreen (carica C:\SYSTEM\SCREENS\LOGIN.MOD con fallback sicuro)
loginscreen_result_t screen_mgr_run_loginscreen(void);

// Esegue il Logoutscreen (carica C:\SYSTEM\SCREENS\LOGOUT.MOD con fallback sicuro)
logoutscreen_result_t screen_mgr_run_logoutscreen(void);

// Esegue il modulo BootSetup per l'installazione del sistema (BOOTSETUP.MOD)
bootsetup_result_t screen_mgr_run_bootsetup(void);

// Controlla se e' presente un'azione pendente straordinaria di aggiornamento in C:\NewOS\Boot\boot_action.ini
bool screen_mgr_has_pending_update(void);

// Esegue il modulo protetto di Aggiornamento Guidato (UPDATE.MOD) fuori sessione
update_result_t screen_mgr_run_update(void);

// Verifica e autentica crittograficamente un modulo protetto (.MOD)
bool screen_mgr_verify_module(const void* buffer, uint32_t size, uint32_t expected_type, const char* expected_name);

#endif // GUI_SCREEN_MGR_H
