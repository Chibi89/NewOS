#include "control_panel.h"
#include "cpl_theme.h"
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

    cpl_theme_t theme;
    cpl_theme_init(&theme);

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo finestra coordinato con il tema attivo
    cpl_draw_dialog_bg(win, &theme, cx, cy, cw, ch);

    // 1. Header Banner
    gfx_fill_rect(cx + 8, cy + 8, cw - 16, 42, theme.input_bg);
    if (theme.is_classic) {
        gfx_draw_bevel_rect(cx + 8, cy + 8, cw - 16, 42, true);
    } else {
        gfx_draw_rounded_rect(cx + 8, cy + 8, cw - 16, 42, theme.control_radius, theme.card_border);
    }
    gfx_draw_icon_32(cx + 14, cy + 13, ICON_MAINTENANCE);
    gfx_draw_string(cx + 54, cy + 14, "Gestione periferiche e Driver di Sistema", theme.accent, 0, true);
    gfx_draw_string(cx + 54, cy + 30, "Driver modulari caricati da C:\\NewOS\\Driver\\ (.SYS + .INI)", theme.text_secondary, 0, true);

    // 2. Colonna Sinistra: Lista Driver
    int32_t list_x = cx + 8;
    int32_t list_y = cy + 56;
    int32_t list_w = 210;
    int32_t list_h = ch - 96;

    gfx_fill_rect(list_x, list_y, list_w, list_h, theme.input_bg);
    if (theme.is_classic) {
        gfx_draw_bevel_rect(list_x, list_y, list_w, list_h, true);
    } else {
        gfx_draw_rounded_rect(list_x, list_y, list_w, list_h, theme.control_radius, theme.card_border);
    }

    int count = driver_mgr_get_count();
    int item_h = 24;

    for (int i = 0; i < count; i++) {
        const driver_info_t* drv = driver_mgr_get_driver(i);
        if (!drv) continue;

        int32_t iy = list_y + 4 + i * item_h;
        if (iy + item_h > list_y + list_h) break;

        bool is_sel = (i == state->selected_index);
        if (is_sel) {
            if (theme.is_classic) {
                gfx_fill_rect(list_x + 2, iy, list_w - 4, item_h, theme.sel_bg);
            } else {
                gfx_fill_rounded_rect(list_x + 2, iy, list_w - 4, item_h, 3, theme.sel_bg);
            }
            gfx_draw_icon_16(list_x + 6, iy + 4, get_driver_icon(drv->type));
            gfx_draw_string(list_x + 26, iy + 5, drv->filename, theme.sel_text, 0, true);
        } else {
            gfx_draw_icon_16(list_x + 6, iy + 4, get_driver_icon(drv->type));
            gfx_draw_string(list_x + 26, iy + 5, drv->filename, theme.text_primary, 0, true);
        }
    }

    // 3. Colonna Destra: Dettagli Periferica e Risorse Hardware
    int32_t det_x = list_x + list_w + 8;
    int32_t det_y = list_y;
    int32_t det_w = cw - list_w - 24;
    int32_t det_h = list_h;

    cpl_draw_groupbox(det_x, det_y, det_w, det_h, " Proprieta Driver ", &theme);

    const driver_info_t* cur = driver_mgr_get_driver(state->selected_index);
    if (cur) {
        // Icona e Nome completo
        gfx_draw_icon_32(det_x + 14, det_y + 16, get_driver_icon(cur->type));
        gfx_draw_string(det_x + 54, det_y + 16, cur->name, theme.accent, 0, true);
        gfx_draw_string(det_x + 54, det_y + 32, cur->author, theme.text_secondary, 0, true);

        // Separatore
        if (theme.is_classic) {
            gfx_draw_bevel_rect(det_x + 14, det_y + 54, det_w - 28, 2, true);
        } else {
            gfx_fill_rect(det_x + 14, det_y + 54, det_w - 28, 1, theme.card_border);
        }

        // Parametri dettagliati
        int32_t py = det_y + 64;
        char buf[128];

        mini_snprintf(buf, sizeof(buf), "Tipo dispositivo: %s", cur->type_str);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "File binario:     %s (%u byte)", cur->filename, cur->sys_size);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "Configurazione:   %s", cur->ininame);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 20;

        mini_snprintf(buf, sizeof(buf), "Versione modulo:  v%s (x86_64 Native)", cur->version);
        gfx_draw_string(det_x + 16, py, buf, theme.text_primary, 0, true);
        py += 24;

        // Riquadro Risorse Hardware
        cpl_draw_groupbox(det_x + 14, py, det_w - 28, 64, " Risorse Hardware ", &theme);

        mini_snprintf(buf, sizeof(buf), "IRQ: %d  |  I/O Base: 0x%04X  |  DMA: %d", (int)cur->irq, (unsigned int)cur->io_base, (int)cur->dma);
        gfx_draw_string(det_x + 24, py + 14, buf, theme.text_primary, 0, true);

        mini_snprintf(buf, sizeof(buf), "Parametri: %s", cur->settings_summary);
        gfx_draw_string(det_x + 24, py + 34, buf, theme.text_secondary, 0, true);

        // Badge Stato Funzionamento
        py += 74;
        gfx_draw_string(det_x + 16, py, "Stato del dispositivo:", theme.text_primary, 0, true);
        if (cur->status == DRIVER_STATUS_ACTIVE) {
            uint32_t drv_green = theme.is_dark ? 0x2ECC71 : 0x008000;
            gfx_draw_string(det_x + 205, py, "Attivo (Funzionante)", drv_green, 0, true);
        } else {
            gfx_draw_string(det_x + 205, py, driver_mgr_status_to_str(cur->status), theme.accent, 0, true);
        }
    }

    // 4. Barra Pulsanti Inferiori
    int32_t btn_y = cy + ch - 32;

    int32_t ini_x = cx + 8;
    int32_t rel_x = ini_x + 98;
    int32_t cls_x = cx + cw - 88;

    wm_draw_themed_button(ini_x, btn_y, 90, 24, "Apri .INI", false);
    wm_draw_themed_button(rel_x, btn_y, 90, 24, "Ricarica", false);
    wm_draw_themed_button(cls_x, btn_y, 80, 24, "Chiudi", false);
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
            return;
        }
    }

    // Clic sui bottoni inferiori
    int32_t btn_y = cy + ch - 32;
    int32_t ini_x = cx + 8;
    int32_t rel_x = ini_x + 98;
    int32_t cls_x = cx + cw - 88;

    // Click su [ Apri .INI ]
    if (lx >= ini_x && lx <= ini_x + 90 && ly >= btn_y && ly <= btn_y + 24) {
        const driver_info_t* cur = driver_mgr_get_driver(state->selected_index);
        if (cur) {
            char path[128];
            mini_snprintf(path, sizeof(path), "C:/NewOS/Driver/%s", cur->ininame);
            vfs_file_t* node = vfs_open(path);
            if (node) {
                uint32_t sz = node->size;
                if (sz > 4096) sz = 4096;
                char* buf = (char*)kmalloc(sz + 1);
                if (buf) {
                    vfs_read(node, buf, sz);
                    buf[sz] = '\0';
                    app_notepad_open_file(cur->ininame, buf);
                    kfree(buf);
                }
                vfs_close(node);
            }
        }
        return;
    }

    // Click su [ Ricarica ]
    if (lx >= rel_x && lx <= rel_x + 90 && ly >= btn_y && ly <= btn_y + 24) {
        driver_mgr_init();
        return;
    }

    // Click su [ Chiudi ]
    if (lx >= cls_x && lx <= cls_x + 80 && ly >= btn_y && ly <= btn_y + 24) {
        wm_destroy_window(win->id);
        return;
    }
}

void app_devmgmt_launch(void) {
    window_t* win = wm_create_window("Gestione periferiche hardware", 100, 70, 560, 420, APP_DEVMGMT);
    if (!win) return;

    devmgmt_state_t* state = (devmgmt_state_t*)kmalloc(sizeof(devmgmt_state_t));
    if (state) {
        state->selected_index = 0;
        state->scroll_offset = 0;

        win->app_data = state;
        win->on_draw = devmgmt_draw;
        win->on_mouse = devmgmt_mouse;
    }
}
