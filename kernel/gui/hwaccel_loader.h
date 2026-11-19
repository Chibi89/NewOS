#ifndef GUI_HWACCEL_LOADER_H
#define GUI_HWACCEL_LOADER_H

#include <hwaccel.h>
#include <types.h>

// Inizializza il sottosistema di accelerazione hardware e DPI caricando HWACCEL.DLL
bool hwaccel_init(uint64_t lfb_base, uint32_t width, uint32_t height);

// Stato di caricamento del modulo esterno
bool hwaccel_is_ready(void);

// Copia veloce 32bpp (SSE2 / Non-Temporal + Write-Combining)
bool hwaccel_blit_32(void *dst, const void *src, uint32_t num_pixels);

// Riempimento veloce 32bpp
bool hwaccel_fill_32(void *dst, uint32_t color, uint32_t num_pixels);

// Notifica cambio risoluzione per ricalcolo DPI/scaling automatico
void hwaccel_on_resolution_changed(uint32_t width, uint32_t height);

// Imposta modalità scala (0 = Auto, 1 = Manuale) e percentuale (100, 125, 150, 175, 200)
void hwaccel_set_scale(uint32_t mode, uint32_t percent);

// Restituisce i parametri hardware e DPI correnti
const hwaccel_info_t *hwaccel_get_info(void);

#endif // GUI_HWACCEL_LOADER_H

