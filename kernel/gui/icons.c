#include <gui/icons.h>
#include <gui/graphics.h>
#include <drivers/ata.h>
#include <mm/heap.h>

#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <screen_modules.h>
#include <string.h>

#define ICONS_DISK_LBA      1024
#define ICONS_SECTOR_COUNT  460
#define ICONS_TOTAL_BYTES   (ICONS_SECTOR_COUNT * 512)

typedef const uint8_t* (*icons_dll_entry_fn)(const kernel_api_t* api, uint32_t* out_size);

static uint8_t*       icon_pkg_buf = NULL;
static const uint8_t* icon_mico_ptr = NULL;
static uint32_t       icon_mico_sz = 0;
static bool           icons_loaded = false;

void icons_init(void) {
    if (icons_loaded) return;

    // 1. Caricamento dinamico Out-of-Kernel da C:\NewOS\SystemLibrary\ICONS.DLL
    uint32_t dll_sz = 0;
    void* dll_data = vfs_read_entire_file("C:\\NewOS\\SystemLibrary\\ICONS.DLL", &dll_sz);
    if (!dll_data) {
        dll_data = vfs_read_entire_file("/NewOS/SystemLibrary/ICONS.DLL", &dll_sz);
    }
    if (dll_data && dll_sz >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(dll_data, dll_sz, MODULE_TYPE_SYSTEM_LIBRARY, "ICONS")) {
            const module_header_t* h = (const module_header_t*)dll_data;
            icons_dll_entry_fn entry = (icons_dll_entry_fn)((const uint8_t*)dll_data + h->entry_offset);
            uint32_t mico_sz = 0;
            const uint8_t* mico = entry(screen_mgr_get_api(), &mico_sz);
            if (mico && mico_sz >= 2048 && mico[0] == 'M' && mico[1] == 'I' && mico[2] == 'C' && mico[3] == 'O') {
                icon_pkg_buf = (uint8_t*)dll_data;
                icon_mico_ptr = mico;
                icon_mico_sz = mico_sz;
                icons_loaded = true;
                return;
            }
        }
        kfree(dll_data);
    }

    // 2. Fallback su lettura a blocchi diretti dal disco ATA/AHCI (LBA 1024 per Boot Menu pre-VFS)
    if (!icon_pkg_buf) {
        icon_pkg_buf = (uint8_t*)kmalloc(ICONS_TOTAL_BYTES);
    }

    if (icon_pkg_buf) {
        if (ata_read_sectors(ICONS_DISK_LBA, ICONS_SECTOR_COUNT, icon_pkg_buf)) {
            if (icon_pkg_buf[0] == 'M' && icon_pkg_buf[1] == 'I' &&
                icon_pkg_buf[2] == 'C' && icon_pkg_buf[3] == 'O') {
                icon_mico_ptr = icon_pkg_buf;
                icon_mico_sz = ICONS_TOTAL_BYTES;
                icons_loaded = true;
                return;
            }
        }
    }
}

const uint32_t* icon_get_32(icon_id_t id) {
    if (!icons_loaded) icons_init();
    if (!icons_loaded || !icon_mico_ptr || id < 0 || id >= NUM_ICONS) {
        return NULL;
    }
    uint32_t entry_off = 16 + (uint32_t)id * 32;
    uint32_t off32 = *(const uint32_t*)(icon_mico_ptr + entry_off + 20);
    if (off32 >= icon_mico_sz) return NULL;
    return (const uint32_t*)(icon_mico_ptr + off32);
}

const uint32_t* icon_get_16(icon_id_t id) {
    if (!icons_loaded) icons_init();
    if (!icons_loaded || !icon_mico_ptr || id < 0 || id >= NUM_ICONS) {
        return NULL;
    }
    uint32_t entry_off = 16 + (uint32_t)id * 32;
    uint32_t off16 = *(const uint32_t*)(icon_mico_ptr + entry_off + 24);
    if (off16 >= icon_mico_sz) return NULL;
    return (const uint32_t*)(icon_mico_ptr + off16);
}

void gfx_draw_sprite_32_alpha(int32_t x, int32_t y, const uint32_t* pixels) {
    if (!pixels) return;
    int32_t sw = (int32_t)gfx_get_width();
    int32_t sh = (int32_t)gfx_get_height();

    for (int32_t py = 0; py < 32; py++) {
        int32_t sy = y + py;
        if (sy < 0 || sy >= sh) continue;

        for (int32_t px = 0; px < 32; px++) {
            int32_t sx = x + px;
            if (sx < 0 || sx >= sw) continue;

            uint32_t src = pixels[py * 32 + px];
            uint32_t a = (src >> 24) & 0xFF;
            if (a == 0) continue; // Trasparente

            if (a >= 250) {
                gfx_draw_pixel(sx, sy, src & 0x00FFFFFF);
            } else {
                uint32_t bg = gfx_get_pixel(sx, sy);
                uint32_t sr = (src >> 16) & 0xFF;
                uint32_t sg = (src >> 8)  & 0xFF;
                uint32_t sb = src & 0xFF;

                uint32_t dr = (bg >> 16) & 0xFF;
                uint32_t dg = (bg >> 8)  & 0xFF;
                uint32_t db = bg & 0xFF;

                uint32_t inv_a = 255 - a;
                uint32_t r = (sr * a + dr * inv_a) / 255;
                uint32_t g = (sg * a + dg * inv_a) / 255;
                uint32_t b = (sb * a + db * inv_a) / 255;

                gfx_draw_pixel(sx, sy, (r << 16) | (g << 8) | b);
            }
        }
    }
}

void gfx_draw_sprite_16_alpha(int32_t x, int32_t y, const uint32_t* pixels) {
    if (!pixels) return;
    int32_t sw = (int32_t)gfx_get_width();
    int32_t sh = (int32_t)gfx_get_height();

    for (int32_t py = 0; py < 16; py++) {
        int32_t sy = y + py;
        if (sy < 0 || sy >= sh) continue;

        for (int32_t px = 0; px < 16; px++) {
            int32_t sx = x + px;
            if (sx < 0 || sx >= sw) continue;

            uint32_t src = pixels[py * 16 + px];
            uint32_t a = (src >> 24) & 0xFF;
            if (a == 0) continue; // Trasparente

            if (a >= 250) {
                gfx_draw_pixel(sx, sy, src & 0x00FFFFFF);
            } else {
                uint32_t bg = gfx_get_pixel(sx, sy);
                uint32_t sr = (src >> 16) & 0xFF;
                uint32_t sg = (src >> 8)  & 0xFF;
                uint32_t sb = src & 0xFF;

                uint32_t dr = (bg >> 16) & 0xFF;
                uint32_t dg = (bg >> 8)  & 0xFF;
                uint32_t db = bg & 0xFF;

                uint32_t inv_a = 255 - a;
                uint32_t r = (sr * a + dr * inv_a) / 255;
                uint32_t g = (sg * a + dg * inv_a) / 255;
                uint32_t b = (sb * a + db * inv_a) / 255;

                gfx_draw_pixel(sx, sy, (r << 16) | (g << 8) | b);
            }
        }
    }
}

void gfx_draw_icon_32(int32_t x, int32_t y, icon_id_t id) {
    const uint32_t* pixels = icon_get_32(id);
    if (pixels) {
        gfx_draw_sprite_32_alpha(x, y, pixels);
    } else {
        // Fallback su icone vettoriali se non ancora caricato dal disco
        switch (id) {
            case ICON_THIS_PC:        gfx_draw_icon_computer(x, y); break;
            case ICON_FOLDER:
            case ICON_FOLDER_VOID:    gfx_draw_icon_folder(x, y); break;
            case ICON_DOCUMENT:       gfx_draw_icon_doc(x, y); break;
            case ICON_RUN:            gfx_draw_icon_cmd(x, y); break;
            case ICON_INFO:           gfx_draw_windows_logo(x + 6, y + 6); break;
            default:                  gfx_draw_icon_calc(x, y); break;
        }
    }
}

void gfx_draw_icon_16(int32_t x, int32_t y, icon_id_t id) {
    const uint32_t* pixels = icon_get_16(id);
    if (pixels) {
        gfx_draw_sprite_16_alpha(x, y, pixels);
    } else {
        // Fallback semplice a 16x16
        switch (id) {
            case ICON_FOLDER:
            case ICON_FOLDER_VOID:    gfx_draw_icon_folder(x, y); break;
            case ICON_DOCUMENT:       gfx_draw_icon_doc(x, y); break;
            default:                  gfx_draw_icon_calc(x, y); break;
        }
    }
}
