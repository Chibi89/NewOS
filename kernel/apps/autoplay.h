#ifndef APPS_AUTOPLAY_H
#define APPS_AUTOPLAY_H

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

#endif // APPS_AUTOPLAY_H
