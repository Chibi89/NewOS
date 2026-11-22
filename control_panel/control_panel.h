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

// Applet Proprietà Mouse e Puntatori (MAIN.CPL)
void app_mouse_props_launch(void);

// Applet Gestione Account Utente Out-of-Kernel (USERACCT.CPL -> USERACCT.DLL)
void app_user_accounts_launch(void);

// Applet Data, Ora e Lingua Out-of-Kernel (TIMEDATE.CPL -> TIMEDATE.DLL)
void app_timedate_props_launch(void);

// Applet Connessioni di Rete Out-of-Kernel (NCPA.CPL -> NETCPL.DLL)
void app_net_props_launch(void);

// Applet Proprietà Tastiera Out-of-Kernel (KEYBOARD.CPL -> KEYBCPL.DLL)
void app_keyboard_props_launch(void);

// Applet Centro Sicurezza e Permessi Out-of-Kernel (SECCPL.CPL -> SECCPL.DLL)
void app_seccpl_launch(void);

// Applet Gestione Caratteri e Font TrueType Out-of-Kernel (FONTS.CPL -> FONTSCPL.DLL)
void app_fonts_props_launch(void);

// Servizio Standardizzato Obbligatorio Installazione Software Terzi (PKGSVC.DLL)
void app_pkgsvc_launch(void);

// Sottosistema Porte Parallele Legacy, Seriali COM e Porte Virtuali (PORTS.DLL)
void app_ports_props_launch(void);
void app_bench_props_launch(void);
void app_imgview_props_launch(void);

// Applet Informazioni su NewOS (WINVER.EXE / SYSDM.CPL)
void app_about_launch(void);

#endif // CONTROL_PANEL_H
