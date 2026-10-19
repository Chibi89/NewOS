#include <drivers/vga.h>
#include <gui/graphics.h>
#include <arch/x86_64/io.h>
#include <string.h>

#define BGA_INDEX_PORT          0x01CE
#define BGA_DATA_PORT           0x01CF

#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_GETCAPS           0x02
#define VBE_DISPI_8BIT_DAC          0x20
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

static bool g_bga_detected = false;
static uint16_t g_bga_version = 0;

static const vga_mode_info_t g_vga_modes[VGA_MODE_COUNT] = {
    { 800,  600,  32, "800x600",   "4:3 SVGA" },
    { 1024, 768,  32, "1024x768",  "4:3 XGA (Predefinita)" },
    { 1280, 720,  32, "1280x720",  "16:9 HD 720p" },
    { 1280, 1024, 32, "1280x1024", "5:4 SXGA" },
    { 1440, 900,  32, "1440x900",  "16:10 WXGA+" },
    { 1920, 1080, 32, "1920x1080", "16:9 Full HD 1080p" }
};

static void bga_write(uint16_t index, uint16_t data) {
    outw(BGA_INDEX_PORT, index);
    outw(BGA_DATA_PORT, data);
}

static uint16_t bga_read(uint16_t index) {
    outw(BGA_INDEX_PORT, index);
    return inw(BGA_DATA_PORT);
}

bool vga_init(void) {
    // Prova a leggere l'ID BGA dai registri standard
    g_bga_version = bga_read(VBE_DISPI_INDEX_ID);
    if ((g_bga_version & 0xFFF0) == 0xB0C0) {
        g_bga_detected = true;
    } else {
        // Tenta scrittura ID4 per verifica compatibilità avanzata Bochs/QEMU/VirtualBox
        bga_write(VBE_DISPI_INDEX_ID, 0xB0C4);
        g_bga_version = bga_read(VBE_DISPI_INDEX_ID);
        if ((g_bga_version & 0xFFF0) == 0xB0C0) {
            g_bga_detected = true;
        }
    }
    return g_bga_detected;
}

bool vga_is_bga_supported(void) {
    return g_bga_detected;
}

int vga_get_mode_count(void) {
    return VGA_MODE_COUNT;
}

const vga_mode_info_t* vga_get_mode_info(int mode_index) {
    if (mode_index >= 0 && mode_index < VGA_MODE_COUNT) {
        return &g_vga_modes[mode_index];
    }
    return NULL;
}

int vga_get_current_mode_index(void) {
    uint32_t w = gfx_get_width();
    uint32_t h = gfx_get_height();
    for (int i = 0; i < VGA_MODE_COUNT; i++) {
        if (g_vga_modes[i].width == w && g_vga_modes[i].height == h) {
            return i;
        }
    }
    return VGA_MODE_1024x768; // Default
}

bool vga_set_resolution(uint32_t width, uint32_t height) {
    if (!g_bga_detected) {
        vga_init();
        if (!g_bga_detected) return false;
    }

    if (width < 640 || width > 1920 || height < 480 || height > 1200) {
        return false;
    }

    // 1. Programma i registri BGA della scheda video
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bga_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bga_write(VBE_DISPI_INDEX_BPP, 32);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

    // 2. Riconfigura il sottosistema grafico del kernel
    return gfx_set_resolution(width, height);
}

bool vga_set_mode_index(int mode_index) {
    if (mode_index < 0 || mode_index >= VGA_MODE_COUNT) return false;
    return vga_set_resolution(g_vga_modes[mode_index].width, g_vga_modes[mode_index].height);
}
