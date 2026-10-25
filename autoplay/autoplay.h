#ifndef AUTOPLAY_H
#define AUTOPLAY_H

/**
 * autoplay/autoplay.h
 * Sottosistema AutoPlay per NewOS V1 (64-bit).
 * Componente autonomo scorporato dal kernel core.
 * Gestione dialog di inserimento supporti rimovibili (USB, CD/DVD, Dischi).
 * (C) Newspoint Software - NewSoft Media Subsystem
 */

#include <types.h>

typedef enum {
    AUTOPLAY_MODE_MASS_STORAGE = 1, // Caso 1: MassStorage
    AUTOPLAY_MODE_MULTIMEDIA   = 2, // Caso 2: Multimedia Player
    AUTOPLAY_MODE_PHOTO        = 3, // Caso 3: PhotoManager Galleria
    AUTOPLAY_MODE_STANDARD     = 4  // Caso 4: Standard AutoPlay Dialog
} autoplay_mode_t;

// Avvia la finestra AutoPlay con rilevamento dinamico automatico dei contenuti
void app_autoplay_launch(char drive_letter);

// Avvia la finestra AutoPlay forzando uno specifico Caso (1, 2, 3 o 4)
void app_autoplay_launch_mode(char drive_letter, autoplay_mode_t mode);

// Analizza i contenuti dell'unità montata per determinare il caso appropriato
autoplay_mode_t autoplay_detect_content_type(char drive_letter);

#endif // AUTOPLAY_H
