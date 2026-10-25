/**
 * kernel/apps/player.h
 * Riproduttore Multimediale NewOS Player (PLAYER.EXE)
 * Supporto per riproduzione Audio (WAV, MP3, FLAC) e Video (AV1, AVI, MP4)
 * (C) Newspoint Software - NewSoft Media Subsystem
 */

#ifndef APPS_PLAYER_H
#define APPS_PLAYER_H

#include <types.h>

// Avvia il Riproduttore Multimediale NewOS Player con la playlist predefinita
void app_player_launch(void);

// Apre un file specifico (.WAV, .MP3, .FLAC, .AVI, .MP4, .AV1, .PLAYLIST)
void app_player_open_file(const char* filepath);

// Notifica ciclica per l'aggiornamento animato dello spettro e sincronizzazione A/V
void app_player_update_tick(void);

// Notifica chiusura della finestra del player
void app_player_on_close(void);

#endif // APPS_PLAYER_H
