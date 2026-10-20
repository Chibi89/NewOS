/**
 * WebBrowser/webbrowser.h
 * NewSoft Web Browser per NewOS V1 (64-bit)
 * (C) Copyright Newspoint Software - NewSoft - NewOS V1
 * @credits: Basato sui principi e architettura del motore NetSurf (C89/C99)
 * @thanks: NetSurf Project (http://www.netsurf-browser.org/)
 */

#ifndef WEBBROWSER_H
#define WEBBROWSER_H

#include <types.h>

#define BROWSER_VERSION_STR "NewSoft Web Browser v1.0 (64-bit)"
#define BROWSER_DEFAULT_HOME "about:home"

// Funzioni di avvio dell'applicazione
void app_browser_launch(void);
void app_browser_open_url(const char* url);

#endif // WEBBROWSER_H
