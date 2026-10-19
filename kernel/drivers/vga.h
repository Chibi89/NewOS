#ifndef DRIVERS_VGA_H
#define DRIVERS_VGA_H

#include <types.h>

// ─── Risoluzioni Video Supportate ─────────────────────────────────────────
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    const char* label;
    const char* aspect_ratio;
} vga_mode_info_t;

#define VGA_MODE_800x600    0
#define VGA_MODE_1024x768   1
#define VGA_MODE_1280x720   2
#define VGA_MODE_1280x1024  3
#define VGA_MODE_1440x900   4
#define VGA_MODE_1920x1080  5
#define VGA_MODE_COUNT      6

// Inizializzazione e verifica compatibilità BGA (Bochs/QEMU/VirtualBox)
bool vga_init(void);
bool vga_is_bga_supported(void);

// Modalità disponibili
int vga_get_mode_count(void);
const vga_mode_info_t* vga_get_mode_info(int mode_index);
int vga_get_current_mode_index(void);

// Commutazione dinamica della risoluzione video
bool vga_set_resolution(uint32_t width, uint32_t height);
bool vga_set_mode_index(int mode_index);

#endif // DRIVERS_VGA_H
