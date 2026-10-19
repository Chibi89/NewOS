#include <apps/devmgmt.h>
#include <drivers/driver_mgr.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <mm/heap.h>
#include <string.h>
#include <fs/vfs.h>

typedef struct {
    int selected_index;
    int scroll_offset;
} devmgmt_state_t;

static icon_id_t get_driver_icon(driver_type_t type) {
    switch (type) {
        case DRIVER_TYPE_BUS:            return ICON_MAINTENANCE;
        case DRIVER_TYPE_STORAGE:        return ICON_HD;
        case DRIVER_TYPE_DISPLAY:        return ICON_DESKTOP;
        case DRIVER_TYPE_AUDIO:          return ICON_SOUND;
        case DRIVER_TYPE_INPUT_MOUSE:    return ICON_MOUSE;
        case DRIVER_TYPE_INPUT_KEYBOARD: return ICON_KEYBOARD;
        case DRIVER_TYPE_TIMER:          return ICON_INFO;
        case DRIVER_TYPE_POWER:          return ICON_ENERGY;
        case DRIVER_TYPE_NETWORK:        return ICON_NET;
        default:                         return ICON_INFO;
    }
}

static void devmgmt_draw(window_t* win) {
    devmgmt_state_t* state = (devmgmt_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo classico finestra
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 1. Header Banner
    gfx_fill_rect(cx + 8, cy + 8, cw - 16, 42, 0xFFFFFF);
    gfx_draw_bevel_rect(cx + 8, cy + 8, cw - 16, 42, true);
    gfx_draw_icon_32(cx + 14, cy + 13, ICON_MAINTENANCE);
    gfx_draw_string(cx + 54, cy + 14, "Gestione periferiche e Driver di Sistema", 0x0A246A, 0, true);
    gfx_draw_string(cx + 54, cy + 30, "Driver modulari caricati da C:\\NewOS\\Driver\\ (.SYS + .INI)", 0x555555, 0, true);

    // 2. Colonna Sinistra: Lista Driver
    int32_t list_x = cx + 8;
    int32_t list_y = cy + 56;
    int32_t list_w = 210;
    int32_t list_h = ch - 96;

    gfx_fill_rect(list_x, list_y, list_w, list_h, 0xFFFFFF);
    gfx_draw_bevel_rect(list_x, list_y, list_w, list_h, true);

    int count = driver_mgr_get_count();
    int item_h = 24;

    for (int i = 0; i < count; i++) {
        const driver_info_t* drv = driver_mgr_get_driver(i);
        if (!drv) continue;

        int32_t iy = list_y + 4 + i * item_h;
        if (iy + item_h > list_y + list_h) break;

        bool is_sel = (i == state->selected_index);
        if (is_sel) {
            gfx_fill_rect(list_x + 2, iy, list_w - 4, item_h, 0x0A246A);
            gfx_draw_icon_16(list_x + 6, iy + 4, get_driver_icon(drv->type));
            gfx_draw_string(list_x + 26, iy + 5, drv->filename, 0xFFFFFF, 0, true);
        } else {
            gfx_draw_icon_16(list_x + 6, iy + 4, get_driver_icon(drv->type));
            gfx_draw_string(list_x + 26, iy + 5, drv->filename, COLOR_WINDOWTEXT, 0, true);
        }
    }

    // 3. Colonna Destra: Dettagli Periferica e Risorse Hardware
    int32_t det_x = list_x + list_w + 8;
    int32_t det_y = list_y;
    int32_t det_w = cw - list_w - 24;
    int32_t det_h = list_h;

    gfx_draw_bevel_rect(det_x, det_y, det_w, det_h, true);
    gfx_fill_rect(det_x + 10, det_y - 6, 120, 12, COLOR_BTNFACE);
    gfx_draw_string(det_x + 12, det_y - 6, " Proprieta Driver ", COLOR_WINDOWTEXT, 0, true);

    const driver_info_t* cur = driver_mgr_get_driver(state->selected_index);
    if (cur) {
        // Icona e Nome completo
        gfx_draw_icon_32(det_x + 14, det_y + 16, get_driver_icon(cur->type));
        gfx_draw_string(det_x + 54, det_y + 16, cur->name, 0x000080, 0, true);
        gfx_draw_string(det_x + 54, det_y + 32, cur->author, 0x666666, 0, true);

        // Separatore
        gfx_draw_bevel_rect(det_x + 14, det_y + 54, det_w - 28, 2, true);

        // Parametri dettagliati
        int32_t py = det_y + 64;
        char buf[128];

        mini_snprintf(buf, sizeof(buf), "Tipo dispositivo: %s", cur->type_str);
        gfx_draw_string(det_x + 16, py, buf, 0x222222, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "File binario:     %s (%u byte)", cur->filename, cur->sys_size);
        gfx_draw_string(det_x + 16, py, buf, 0x222222, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "Configurazione:   %s", cur->ininame);
        gfx_draw_string(det_x + 16, py, buf, 0x222222, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "Versione modulo:  v%s (x86_64 Native)", cur->version);
        gfx_draw_string(det_x + 16, py, buf, 0x222222, 0, true);
        py += 24;

        // Riquadro Risorse Hardware
        gfx_draw_bevel_rect(det_x + 14, py, det_w - 28, 64, true);
        gfx_fill_rect(det_x + 20, py - 6, 120, 12, COLOR_BTNFACE);
        gfx_draw_string(det_x + 22, py - 6, " Risorse Hardware ", COLOR_WINDOWTEXT, 0, true);

        mini_snprintf(buf, sizeof(buf), "IRQ: %d  |  I/O Base: 0x%04X  |  DMA: %d", (int)cur->irq, (unsigned int)cur->io_base, (int)cur->dma);
        gfx_draw_string(det_x + 24, py + 14, buf, 0x000000, 0, true);

        mini_snprintf(buf, sizeof(buf), "Parametri: %s", cur->settings_summary);
        gfx_draw_string(det_x + 24, py + 34, buf, 0x333333, 0, true);

        // Badge Stato Funzionamento
        py += 74;
        gfx_draw_string(det_x + 16, py, "Stato del dispositivo:", COLOR_WINDOWTEXT, 0, true);
        if (cur->status == DRIVER_STATUS_ACTIVE) {
            gfx_draw_string(det_x + 205, py, "Attivo (Funzionante)", 0x008000, 0, true);
        } else {
            gfx_draw_string(det_x + 205, py, driver_mgr_status_to_str(cur->status), 0x805000, 0, true);
        }
    }

    // 4. Barra Pulsanti Inferiori
    int32_t btn_y = cy + ch - 32;

    // [ Apri .INI ]
    int32_t ini_x = cx + 8;
    gfx_fill_rect(ini_x, btn_y, 90, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ini_x, btn_y, 90, 24, false);
    gfx_draw_string(ini_x + 14, btn_y + 5, "Apri .INI", COLOR_WINDOWTEXT, 0, true);

    // [ Ricarica ]
    int32_t rel_x = ini_x + 98;
    gfx_fill_rect(rel_x, btn_y, 90, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(rel_x, btn_y, 90, 24, false);
    gfx_draw_string(rel_x + 16, btn_y + 5, "Ricarica", COLOR_WINDOWTEXT, 0, true);

    // [ Chiudi ]
    int32_t cls_x = cx + cw - 88;
    gfx_fill_rect(cls_x, btn_y, 80, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cls_x, btn_y, 80, 24, false);
    gfx_draw_string(cls_x + 18, btn_y + 5, "Chiudi", COLOR_WINDOWTEXT, 0, true);
}

static void devmgmt_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    devmgmt_state_t* state = (devmgmt_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Clic sulla lista driver
    int32_t list_x = cx + 8;
    int32_t list_y = cy + 56;
    int32_t list_w = 210;
    int32_t list_h = ch - 96;

    if (lx >= list_x && lx <= list_x + list_w && ly >= list_y && ly <= list_y + list_h) {
        int idx = (ly - list_y - 4) / 24;
        int count = driver_mgr_get_count();
        if (idx >= 0 && idx < count) {
            state->selected_index = idx;
        }
        return;
    }

    // Clic sui pulsanti inferiori
    int32_t btn_y = cy + ch - 32;

    // [ Apri .INI ]
    int32_t ini_x = cx + 8;
    if (lx >= ini_x && lx <= ini_x + 90 && ly >= btn_y && ly <= btn_y + 24) {
        const driver_info_t* cur = driver_mgr_get_driver(state->selected_index);
        if (cur) {
            char ini_path[VFS_MAX_PATH];
            mini_snprintf(ini_path, sizeof(ini_path), "C:\\NewOS\\Driver\\%s", cur->ininame);
            uint32_t fsize = 0;
            char* txt = (char*)vfs_read_entire_file(ini_path, &fsize);
            if (!txt) {
                mini_snprintf(ini_path, sizeof(ini_path), "C:\\SYSTEM\\DRIVERS\\%s", cur->ininame);
                txt = (char*)vfs_read_entire_file(ini_path, &fsize);
            }
            if (txt) {
                app_notepad_open_file(cur->ininame, txt);
                kfree(txt);
            } else {
                app_notepad_open_file(cur->ininame, "[Errore lettura file .INI]");
            }
        }
        return;
    }

    // [ Ricarica ]
    int32_t rel_x = ini_x + 98;
    if (lx >= rel_x && lx <= rel_x + 90 && ly >= btn_y && ly <= btn_y + 24) {
        const driver_info_t* cur = driver_mgr_get_driver(state->selected_index);
        if (cur) {
            driver_mgr_reload(cur->name);
        }
        return;
    }

    // [ Chiudi ]
    int32_t cls_x = cx + cw - 88;
    if (lx >= cls_x && lx <= cls_x + 80 && ly >= btn_y && ly <= btn_y + 24) {
        wm_destroy_window(win->id);
        return;
    }
}

void app_devmgmt_launch(void) {
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t dw = 580;
    int32_t dh = 400;
    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - dh) / 2;

    window_t* win = wm_create_window("Gestione periferiche - NewOS V1", dx, dy, dw, dh, APP_DEVMGMT);
    if (!win) return;

    devmgmt_state_t* state = (devmgmt_state_t*)kmalloc(sizeof(devmgmt_state_t));
    if (!state) return;
    state->selected_index = 0;
    state->scroll_offset = 0;

    win->app_data = state;
    win->on_draw = devmgmt_draw;
    win->on_mouse = devmgmt_mouse;
}
