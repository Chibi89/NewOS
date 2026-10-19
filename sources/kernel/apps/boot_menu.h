#ifndef APPS_BOOT_MENU_H
#define APPS_BOOT_MENU_H

#include <types.h>

// Mostra la schermata iniziale di boot e restituisce la modalità scelta:
// 1 = Modalità Live
// 2 = Installa NewOS V1 sul PC (apertura automatica installer)
// 3 = Risoluzione Schermo e Impostazioni Video (VGA.SYS)
int boot_menu_show(void);

#endif // APPS_BOOT_MENU_H
