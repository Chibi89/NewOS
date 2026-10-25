#ifndef OOBE_H
#define OOBE_H

/**
 * oobe/oobe.h
 * Procedura Guidata di Configurazione Iniziale OOBE (Out-Of-Box Experience)
 * Componente autonomo scorporato dal kernel core.
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include <types.h>

// Esecuzione modale dell'OOBE a livello di Login (pre-desktop)
int oobe_run_modal(void);

// Avvia la procedura guidata OOBE (Out-Of-Box Experience come finestra desktop manuale)
void app_oobe_launch(void);

// Verifica se l'OOBE e' gia' stato completato su questa installazione
bool oobe_is_completed(void);

// Imposta l'OOBE come completato in modo permanente
void oobe_mark_completed(void);

#endif // OOBE_H
