#ifndef APPS_BOOT_MENU_H
#define APPS_BOOT_MENU_H

#include <types.h>

// Mostra la schermata iniziale di boot e restituisce la modalità scelta:
// 1 = Modalità Live
// 2 = Installa NewOS V1 sul PC (apertura automatica installer)
// 3 = Risoluzione Schermo e Impostazioni Video (VGA.SYS)
int boot_menu_show(void);

// Verifica se il sistema è già installato e configurato (SetupStage = Completed / OOBECompleted = 1)
// per attivare il bypass atomico automatico del menu di boot.
bool boot_menu_should_bypass(void);

// Notifica e audit del bypass del boot manager con transizione diretta a LOGIN.MOD
void boot_action_set_direct_login(void);

#endif // APPS_BOOT_MENU_H
