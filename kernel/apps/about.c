#include <gui/graphics.h>
#include <gui/wm.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/cpu.h>
#include <drivers/ata.h>
#include <drivers/ac97.h>
#include <user.h>

#define MAX_ABOUT_LINES 14

static char about_lines[MAX_ABOUT_LINES][96];
static int num_about_lines = 0;

static void about_init_lines(void) {
    num_about_lines = 0;

    user_profile_t* u = user_get_current();
    const cpu_info_t* cpu = cpu_get_info();
    const BootInfo* binfo = kernel_get_boot_info();
    const ata_device_t* pm = ata_get_primary_master();
    const char* audio_name = ac97_get_device_name();

    // Utente e computer
    strcpy(about_lines[num_about_lines++], "Registrato a nome di:");
    mini_snprintf(about_lines[num_about_lines++], 96, "  %s (%s)",
                  (u && u->username[0]) ? u->username : "Utente NewOS",
                  (u && u->computer_name[0]) ? u->computer_name : "NEWOS-PC");
    strcpy(about_lines[num_about_lines++], "");

    // Specifiche hardware reali
    strcpy(about_lines[num_about_lines++], "Specifiche di sistema reali:");

    // CPU
    mini_snprintf(about_lines[num_about_lines++], 96, "  Processore: %s (%d Core)",
                  (cpu && cpu->brand[0]) ? cpu->brand : "CPU x86_64",
                  (cpu && cpu->logical_cores) ? cpu->logical_cores : 1);

    // RAM
    uint32_t ram_mb = (binfo && binfo->total_memory_bytes) ? (uint32_t)(binfo->total_memory_bytes / (1024 * 1024)) : 256;
    mini_snprintf(about_lines[num_about_lines++], 96, "  Memoria RAM: %d MB rilevati (Paginazione PML4 64-bit)", ram_mb);

    // Display
    if (binfo) {
        mini_snprintf(about_lines[num_about_lines++], 96, "  Grafica: VESA VBE %dx%d a %d-bit (LFB 0x%x)",
                      binfo->width, binfo->height, binfo->bpp, (uint32_t)binfo->framebuffer_base);
    } else {
        strcpy(about_lines[num_about_lines++], "  Grafica: VESA VBE 1024x768 a 32-bit");
    }

    // Storage
    if (pm && pm->present) {
        if (pm->size_gb > 0) {
            mini_snprintf(about_lines[num_about_lines++], 96, "  Disco C:\\: %s (%d GB, %s)",
                          pm->model, pm->size_gb, pm->part_fs[0] ? pm->part_fs : "exFAT");
        } else {
            mini_snprintf(about_lines[num_about_lines++], 96, "  Disco C:\\: %s (%d MB, %s)",
                          pm->model, pm->size_mb, pm->part_fs[0] ? pm->part_fs : "exFAT");
        }
    } else {
        strcpy(about_lines[num_about_lines++], "  Disco: Controller ATA IDE standard");
    }

    // Audio
    mini_snprintf(about_lines[num_about_lines++], 96, "  Audio PCI: %s", audio_name);

    // Periferiche
    int dev_count = ata_get_device_count();
    mini_snprintf(about_lines[num_about_lines++], 96, "  I/O: Tastiera PS/2, Mouse PS/2, PIT Timer (%d Unita' ATA)", dev_count);

    // Copyright NewSoft
    strcpy(about_lines[num_about_lines++], "");
    strcpy(about_lines[num_about_lines++], "  Copyright (C) @ Newspoint Software - NewSoft - NewOS V1");
}

static void about_draw(window_t *win) {
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo finestra grigio standard Windows 2000
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 1. Banner superiore con logo Windows 2000
    gfx_draw_windows_logo(cx + 18, cy + 14);
    gfx_draw_string(cx + 44, cy + 10, "NewOS V1 64-bit Edition", 0x000080, 0, true);
    gfx_draw_string(cx + 44, cy + 24, "Build 2026 x86_64 - @ Newspoint Software", 0x404040, 0, true);

    // 2. Riquadro scavato (Group Box) con bordo in rilievo per le informazioni
    int32_t box_x = cx + 12;
    int32_t box_y = cy + 56;
    int32_t box_w = cw - 24;
    int32_t box_h = ch - 96;

    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);
    gfx_fill_rect(box_x + 10, box_y - 6, 170, 12, COLOR_BTNFACE);
    gfx_draw_string(box_x + 12, box_y - 6, " Informazioni di Sistema ", COLOR_WINDOWTEXT, 0, true);

    // Disegno dinamico e omogeneo di tutte le righe di specifiche
    for (int i = 0; i < num_about_lines; i++) {
        const char *line = about_lines[i];
        if (!line || line[0] == '\0') continue;

        int32_t line_y = box_y + 14 + i * 18;
        if (line[0] == ' ' && line[1] == ' ') {
            // Voce dettagliata con bullet point blu stile Win2k
            gfx_fill_rect(box_x + 14, line_y + 6, 4, 4, 0x0A246A);
            gfx_draw_string(box_x + 24, line_y, line + 2, 0x000000, 0, true);
        } else {
            // Intestazione sezione in grassetto blu
            gfx_draw_string(box_x + 12, line_y, line, 0x0A246A, 0, true);
        }
    }

    // 3. Pulsante "OK" perfettamente centrato
    int32_t btn_w = 78;
    int32_t btn_h = 24;
    int32_t btn_x = cx + (cw - btn_w) / 2;
    int32_t btn_y = cy + ch - 32;

    gfx_fill_rect(btn_x, btn_y, btn_w, btn_h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(btn_x, btn_y, btn_w, btn_h, false);
    gfx_draw_string(btn_x + (btn_w - 16) / 2, btn_y + 4, "OK", COLOR_WINDOWTEXT, 0, true);
}

static void about_click(window_t *win, int32_t local_x, int32_t local_y, bool clicked) {
    if (!clicked) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    int32_t btn_w = 78;
    int32_t btn_h = 24;
    int32_t btn_x = cx + (cw - btn_w) / 2;
    int32_t btn_y = cy + ch - 32;

    if (local_x >= btn_x && local_x < btn_x + btn_w && local_y >= btn_y && local_y < btn_y + btn_h) {
        wm_destroy_window(win->id);
    }
}

void app_about_launch(void) {
    about_init_lines();

    // Calcola dinamicamente la larghezza massima in base al testo presente
    int max_len = 0;
    for (int i = 0; i < num_about_lines; i++) {
        int len = (int)strlen(about_lines[i]);
        if (len > max_len) max_len = len;
    }

    int h1_len = (int)strlen("NewOS V1 64-bit Edition") + 6;
    int h2_len = (int)strlen("Build 2026 x86_64 - @ Newspoint Software") + 6;
    if (h1_len > max_len) max_len = h1_len;
    if (h2_len > max_len) max_len = h2_len;

    int win_w = max_len * 8 + 68;
    if (win_w < 580) win_w = 580;

    int win_h = 28 + 52 + (num_about_lines * 18) + 52;
    if (win_h < 330) win_h = 330;

    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();
    int win_x = ((int)sw - win_w) / 2;
    int win_y = ((int)sh - 28 - win_h) / 2;
    if (win_x < 20) win_x = 20;
    if (win_y < 20) win_y = 20;

    window_t *win = wm_create_window("Informazioni su NewOS V1", win_x, win_y, win_w, win_h, APP_ABOUT);
    if (!win) return;

    win->on_draw = about_draw;
    win->on_mouse = about_click;
}
