#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

/**
 * control_panel/control_panel.h
 * Sottosistema Pannello di Controllo e Applet (.CPL) per NewOS V1 (64-bit)
 * Sottosistema autonomo scorporato dal kernel core.
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include <types.h>

// Applet Proprietà Schermo (DISPLAY.CPL)
void app_display_props_launch(void);

// Applet Proprietà Audio e Mixer (MMSYS.CPL)
void app_sound_props_launch(void);

// Applet Opzioni Risparmio Energia e Dialog Chiusura Sessione (POWERCFG.CPL)
void app_power_props_launch(void);
void app_shutdown_dialog_launch(void);

// Applet Gestione Periferiche Hardware (DEVMGMT.MSC / DEVMGMT.CPL)
void app_devmgmt_launch(void);

// Applet Informazioni su NewOS (WINVER.EXE / SYSDM.CPL)
void app_about_launch(void);

#endif // CONTROL_PANEL_H
