#ifndef HWACCEL_H
#define HWACCEL_H

#include <types.h>

typedef enum {
    HWACCEL_CMD_INIT       = 0,
    HWACCEL_CMD_BLIT32     = 1,
    HWACCEL_CMD_FILL32     = 2,
    HWACCEL_CMD_RECALC_DPI = 3,
    HWACCEL_CMD_SET_SCALE  = 4
} hwaccel_cmd_t;

typedef struct {
    bool initialized;
    bool pat_wc_active;
    bool sse2_supported;
    bool avx_supported;
    bool erms_supported;
    bool edid_valid;
    uint16_t pci_vendor;
    uint16_t pci_device;
    uint64_t lfb_base;
    uint32_t vram_mb;
    uint32_t phys_width_mm;
    uint32_t phys_height_mm;
    uint32_t detected_dpi;
    uint32_t scale_mode;    // 0 = Auto (EDID/DPI), 1 = Manuale
    uint32_t scale_percent; // 100, 125, 150, 175, 200
    char gpu_name[64];
    char monitor_name[32];
} hwaccel_info_t;

typedef struct {
    hwaccel_cmd_t cmd;
    uint64_t lfb_base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    const void *src;
    void *dst;
    uint32_t count_pixels;
    uint32_t fill_color;
    uint32_t scale_mode;
    uint32_t scale_percent;
    hwaccel_info_t *info_out;
} hwaccel_params_t;

#endif // HWACCEL_H

