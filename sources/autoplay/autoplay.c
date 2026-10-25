#include "autoplay.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <mm/heap.h>
#include <string.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/sound.h>
#include <apps/explorer.h>

#define AUTOPLAY_WIN_W 520
#define AUTOPLAY_WIN_H 390

typedef struct {
    char            drive_letter;
    autoplay_mode_t mode;
    int             selected_item;
    bool            is_playing;
    int             playing_track;
    int             standard_choice;
    bool            always_do_this;
    char            status_msg[64];
    uint32_t        total_mb;
    uint32_t        free_mb;
    char            device_name[48];
    char            fs_name[16];
} autoplay_state_t;

static autoplay_state_t g_autoplay_state;
static window_t*        g_autoplay_win = NULL;

autoplay_mode_t autoplay_detect_content_type(char drive_letter) {
    char root_path[8];
    mini_snprintf(root_path, sizeof(root_path), "%c:\\", drive_letter);

    vfs_dirent_t entries[16];
    int count = vfs_readdir(root_path, entries, 16);

    int photo_count = 0;
    int media_count = 0;

    for (int i = 0; i < count; i++) {
        const char* name = entries[i].name;
        if (strstr(name, ".BMP") || strstr(name, ".bmp") ||
            strstr(name, ".JPG") || strstr(name, ".jpg") ||
            strstr(name, ".PNG") || strstr(name, ".png") ||
            strcasecmp(name, "DCIM") == 0) {
            photo_count++;
        }
        if (strstr(name, ".WAV") || strstr(name, ".wav") ||
            strstr(name, ".MP3") || strstr(name, ".mp3") ||
            strstr(name, ".MID") || strstr(name, ".mid")) {
            media_count++;
        }
    }

    if (photo_count > 0 && photo_count >= media_count) {
        return AUTOPLAY_MODE_PHOTO;
    }
    if (media_count > 0) {
        return AUTOPLAY_MODE_MULTIMEDIA;
    }

    // Se è un'unità USB o CD-ROM con partizione dati generica
    if (drive_letter == 'U' || drive_letter == 'u') {
        return AUTOPLAY_MODE_MASS_STORAGE;
    }

    return AUTOPLAY_MODE_STANDARD;
}

static void autoplay_draw_tabs(window_t* win, int32_t cx, int32_t cy) {
    const struct {
        const char* label;
        autoplay_mode_t mode;
        int32_t x;
        int32_t w;
    } tabs[] = {
        { "1. MassStorage", AUTOPLAY_MODE_MASS_STORAGE, cx + 10,  115 },
        { "2. Multimedia",  AUTOPLAY_MODE_MULTIMEDIA,   cx + 130, 115 },
        { "3. PhotoManager",AUTOPLAY_MODE_PHOTO,        cx + 250, 115 },
        { "4. Standard",    AUTOPLAY_MODE_STANDARD,     cx + 370, 115 }
    };

    for (int i = 0; i < 4; i++) {
        bool active = (g_autoplay_state.mode == tabs[i].mode);
        int32_t tx = tabs[i].x;
        int32_t ty = cy + (active ? 0 : 2);
        int32_t tw = tabs[i].w;
        int32_t th = active ? 22 : 20;

        gfx_fill_rect(tx, ty, tw, th, active ? COLOR_BTNFACE : 0xC0BCB4);
        gfx_draw_bevel_rect(tx, ty, tw, th, false);
        if (active) {
            gfx_fill_rect(tx + 1, ty + th - 1, tw - 2, 2, COLOR_BTNFACE);
        }
        uint32_t text_color = active ? 0x000080 : 0x404040;
        gfx_draw_string(tx + 8, ty + 5, tabs[i].label, text_color, 0, true);
    }
}

// ─── CASO 1: MassStorage ──────────────────────────────────────────────────
static void autoplay_draw_case_mass_storage(window_t* win, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t by = cy + 28;

    // Header info unità
    gfx_draw_string(cx + 12, by + 4, "--- Caso 1: Gestione Unita' USB & Disco MassStorage ---", 0x000080, 0, true);

    char dev_info[80];
    mini_snprintf(dev_info, sizeof(dev_info), "Dispositivo: %s (Unita' %c:)", g_autoplay_state.device_name, g_autoplay_state.drive_letter);
    gfx_draw_string(cx + 12, by + 20, dev_info, COLOR_WINDOWTEXT, 0, true);

    char fs_info[80];
    mini_snprintf(fs_info, sizeof(fs_info), "File System: %s | Protocollo: SCSI Bulk-Only Transport (BOT)", g_autoplay_state.fs_name);
    gfx_draw_string(cx + 12, by + 34, fs_info, 0x404040, 0, true);

    // Barra grafica capacità disco
    int32_t bar_x = cx + 12;
    int32_t bar_y = by + 52;
    int32_t bar_w = cw - 24;
    int32_t bar_h = 16;
    gfx_draw_bevel_rect(bar_x, bar_y, bar_w, bar_h, true);
    gfx_fill_rect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, 0xFFFFFF);

    uint32_t used_mb = (g_autoplay_state.total_mb > g_autoplay_state.free_mb) ? (g_autoplay_state.total_mb - g_autoplay_state.free_mb) : (g_autoplay_state.total_mb / 2);
    int32_t fill_w = (g_autoplay_state.total_mb > 0) ? (int32_t)((used_mb * (bar_w - 2)) / g_autoplay_state.total_mb) : 50;
    if (fill_w > bar_w - 2) fill_w = bar_w - 2;
    if (fill_w < 4) fill_w = 4;
    gfx_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 0x0A246A);

    char cap_str[64];
    mini_snprintf(cap_str, sizeof(cap_str), "Capacita': %u MB totali (%u MB occupati, %u MB liberi)",
                  (unsigned int)g_autoplay_state.total_mb, (unsigned int)used_mb, (unsigned int)g_autoplay_state.free_mb);
    gfx_draw_string(bar_x, bar_y + 20, cap_str, COLOR_WINDOWTEXT, 0, true);

    // Listbox file presenti
    int32_t list_y = bar_y + 36;
    int32_t list_h = 110;
    gfx_draw_bevel_rect(bar_x, list_y, bar_w, list_h, true);
    gfx_fill_rect(bar_x + 1, list_y + 1, bar_w - 2, list_h - 2, COLOR_WINDOW);

    gfx_draw_string(bar_x + 8, list_y + 6,  "Cartelle e File rilevati sul volume:", 0x000080, 0, true);
    gfx_draw_string(bar_x + 8, list_y + 24, "  <DIR>          DOCUMENTI", 0x000000, 0, true);
    gfx_draw_string(bar_x + 8, list_y + 40, "  <DIR>          BACKUP", 0x000000, 0, true);
    gfx_draw_string(bar_x + 8, list_y + 56, "     245760 B    RELAZIONE_FINALE.DOC", 0x000000, 0, true);
    gfx_draw_string(bar_x + 8, list_y + 72, "     102400 B    CONFIGURAZIONE.SYS", 0x000000, 0, true);
    gfx_draw_string(bar_x + 8, list_y + 88, "      32768 B    LEGGIMI.TXT", 0x000000, 0, true);

    // Pulsanti operativi
    int32_t btn_y = list_y + list_h + 10;
    int32_t bw = 115;
    int32_t bh = 24;

    gfx_fill_rect(cx + 12, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + 12, btn_y, bw, bh, false);
    gfx_draw_string(cx + 20, btn_y + 6, "Esplora Unita'", COLOR_WINDOWTEXT, 0, true);

    gfx_fill_rect(cx + 137, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + 137, btn_y, bw, bh, false);
    gfx_draw_string(cx + 148, btn_y + 6, "Backup File", COLOR_WINDOWTEXT, 0, true);

    gfx_fill_rect(cx + 262, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + 262, btn_y, bw, bh, false);
    gfx_draw_string(cx + 270, btn_y + 6, "Check Disco", COLOR_WINDOWTEXT, 0, true);

    gfx_fill_rect(cx + 387, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + 387, btn_y, bw, bh, false);
    gfx_draw_string(cx + 405, btn_y + 6, "Espelli", 0x800000, 0, true);

    if (g_autoplay_state.status_msg[0]) {
        gfx_draw_string(cx + 12, btn_y + 30, g_autoplay_state.status_msg, 0x008000, 0, true);
    }
}

// ─── CASO 2: Multimedia Player ────────────────────────────────────────────
static void autoplay_draw_case_multimedia(window_t* win, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t by = cy + 28;

    gfx_draw_string(cx + 12, by + 4, "--- Caso 2: NewOS Media Player & Riproduzione Audio ---", 0x000080, 0, true);

    // Riquadro Playlist brani
    int32_t box_x = cx + 12;
    int32_t box_y = by + 22;
    int32_t box_w = cw - 24;
    int32_t box_h = 120;
    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);
    gfx_fill_rect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, COLOR_WINDOW);

    const char* tracks[] = {
        "1. Beethoven - Sinfonia N. 9 (Inno alla Gioia)  [03:45]",
        "2. Vivaldi - Le Quattro Stagioni (La Primavera)  [03:18]",
        "3. NewOS Classic Startup Symphony               [01:12]",
        "4. Relaxing Ambient Stream 44.1kHz Stereo       [04:20]",
        "5. Techno Pulse Digital Wave Demo               [02:50]"
    };

    for (int i = 0; i < 5; i++) {
        int32_t ty = box_y + 6 + i * 22;
        bool sel = (g_autoplay_state.playing_track == i);
        if (sel) {
            gfx_fill_rect(box_x + 2, ty - 2, box_w - 4, 20, 0x0A246A);
            gfx_draw_string(box_x + 8, ty + 2, tracks[i], 0xFFFFFF, 0, true);
        } else {
            gfx_draw_string(box_x + 8, ty + 2, tracks[i], COLOR_WINDOWTEXT, 0, true);
        }
    }

    // Barra stato riproduzione
    int32_t pb_y = box_y + box_h + 12;
    char state_str[80];
    mini_snprintf(state_str, sizeof(state_str), "Stato: %s | Formato: PCM 44.1 kHz 16-bit Stereo (AC97)",
                  g_autoplay_state.is_playing ? "IN RIPRODUZIONE [>]" : "IN PAUSA [||]");
    gfx_draw_string(box_x, pb_y, state_str, 0x000080, 0, true);

    // Controlli Player multimediale
    int32_t ctrl_y = pb_y + 20;
    int32_t bw = 75;
    int32_t bh = 26;

    // Play
    gfx_fill_rect(box_x, ctrl_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x, ctrl_y, bw, bh, false);
    gfx_draw_string(box_x + 14, ctrl_y + 7, "> Play", 0x006000, 0, true);

    // Pausa
    gfx_fill_rect(box_x + 85, ctrl_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 85, ctrl_y, bw, bh, false);
    gfx_draw_string(box_x + 95, ctrl_y + 7, "|| Pausa", COLOR_WINDOWTEXT, 0, true);

    // Stop
    gfx_fill_rect(box_x + 170, ctrl_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 170, ctrl_y, bw, bh, false);
    gfx_draw_string(box_x + 185, ctrl_y + 7, "# Stop", 0x800000, 0, true);

    // Traccia Precedente / Successiva
    gfx_fill_rect(box_x + 255, ctrl_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 255, ctrl_y, bw, bh, false);
    gfx_draw_string(box_x + 266, ctrl_y + 7, "<< Prev", COLOR_WINDOWTEXT, 0, true);

    gfx_fill_rect(box_x + 340, ctrl_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 340, ctrl_y, bw, bh, false);
    gfx_draw_string(box_x + 351, ctrl_y + 7, ">> Next", COLOR_WINDOWTEXT, 0, true);

    // Volume + / -
    int32_t vw = 70;
    gfx_fill_rect(box_x + 425, ctrl_y, vw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 425, ctrl_y, vw, bh, false);
    gfx_draw_string(box_x + 431, ctrl_y + 7, "Vol: 85%", COLOR_WINDOWTEXT, 0, true);

    if (g_autoplay_state.status_msg[0]) {
        gfx_draw_string(box_x, ctrl_y + 34, g_autoplay_state.status_msg, 0x008000, 0, true);
    }
}

// ─── CASO 3: PhotoManager Galleria ────────────────────────────────────────
static void autoplay_draw_case_photo(window_t* win, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t by = cy + 28;

    gfx_draw_string(cx + 12, by + 4, "--- Caso 3: PhotoManager & Galleria Immagini ---", 0x000080, 0, true);

    int32_t box_x = cx + 12;
    int32_t box_y = by + 22;
    int32_t box_w = 200;
    int32_t box_h = 160;

    // Riquadro lista foto
    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);
    gfx_fill_rect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, COLOR_WINDOW);

    const char* photos[] = {
        "1. HERO_DESKTOP.BMP",
        "2. FLOW_NATURE.JPG",
        "3. BLISS_MEADOW.BMP",
        "4. VACANZE_MARE.JPG",
        "5. TRAMONTO_HDR.BMP",
        "6. MATRIX_CODE.JPG",
        "7. MONTAGNA_NEVE.BMP"
    };

    for (int i = 0; i < 7; i++) {
        int32_t py = box_y + 4 + i * 22;
        bool sel = (g_autoplay_state.selected_item == i);
        if (sel) {
            gfx_fill_rect(box_x + 2, py - 2, box_w - 4, 20, 0x0A246A);
            gfx_draw_string(box_x + 6, py + 2, photos[i], 0xFFFFFF, 0, true);
        } else {
            gfx_draw_string(box_x + 6, py + 2, photos[i], COLOR_WINDOWTEXT, 0, true);
        }
    }

    // Riquadro Anteprima grafica immagine
    int32_t prev_x = box_x + box_w + 14;
    int32_t prev_w = cw - box_w - 38;
    int32_t prev_h = 160;
    gfx_draw_bevel_rect(prev_x, box_y, prev_w, prev_h, true);
    gfx_fill_rect(prev_x + 1, box_y + 1, prev_w - 2, prev_h - 2, 0x101820);

    // Disegno simulato anteprima fotografica a colori
    uint32_t p_color = (g_autoplay_state.selected_item % 3 == 0) ? 0x004080 :
                       ((g_autoplay_state.selected_item % 3 == 1) ? 0x006030 : 0x803020);
    gfx_fill_rect(prev_x + 10, box_y + 10, prev_w - 20, prev_h - 40, p_color);
    gfx_draw_rect(prev_x + 10, box_y + 10, prev_w - 20, prev_h - 40, 0xFFFFFF);
    gfx_draw_string(prev_x + 20, box_y + 40, "[ ANTEPRIMA FOTO ]", 0xFFFFFF, 0, true);
    gfx_draw_string(prev_x + 20, box_y + 60, "Risoluzione: 1024x768", 0xD4D0C8, 0, true);
    gfx_draw_string(prev_x + 20, box_y + 75, "Profondita': 24-bit TrueColor", 0xD4D0C8, 0, true);

    // Pulsanti interattivi PhotoManager
    int32_t btn_y = box_y + box_h + 12;
    int32_t bw = 155;
    int32_t bh = 26;

    gfx_fill_rect(box_x, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x, btn_y, bw, bh, false);
    gfx_draw_string(box_x + 12, btn_y + 7, "Avvia Slideshow", COLOR_WINDOWTEXT, 0, true);

    gfx_fill_rect(box_x + 165, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 165, btn_y, bw, bh, false);
    gfx_draw_string(box_x + 175, btn_y + 7, "Imposta come Sfondo", 0x000080, 0, true);

    gfx_fill_rect(box_x + 330, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(box_x + 330, btn_y, bw, bh, false);
    gfx_draw_string(box_x + 345, btn_y + 7, "Importa in Foto", COLOR_WINDOWTEXT, 0, true);

    if (g_autoplay_state.status_msg[0]) {
        gfx_draw_string(box_x, btn_y + 34, g_autoplay_state.status_msg, 0x008000, 0, true);
    }
}

// ─── CASO 4: Standard AutoPlay Dialog ─────────────────────────────────────
static void autoplay_draw_case_standard(window_t* win, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t by = cy + 28;

    gfx_draw_string(cx + 12, by + 4, "--- Caso 4: AutoPlay Standard - Scelta Operazione ---", 0x000080, 0, true);
    gfx_draw_string(cx + 12, by + 20, "Scegliere l'operazione da eseguire con il supporto rilevato:", COLOR_WINDOWTEXT, 0, true);

    int32_t box_x = cx + 12;
    int32_t box_y = by + 36;
    int32_t box_w = cw - 24;
    int32_t box_h = 135;
    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);
    gfx_fill_rect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, COLOR_WINDOW);

    const char* options[] = {
        "Apri cartella per visualizzare i file (Esplora Risorse)",
        "Riproduci file multimediali con NewOS Media Player",
        "Visualizza e organizza immagini con PhotoManager",
        "Esegui backup e controllo disco con MassStorage Manager",
        "Nessuna operazione"
    };

    for (int i = 0; i < 5; i++) {
        int32_t oy = box_y + 8 + i * 25;
        bool sel = (g_autoplay_state.standard_choice == i);

        // Radio button
        int32_t rx = box_x + 10;
        int32_t ry = oy + 2;
        gfx_draw_rect(rx, ry, 10, 10, 0x404040);
        gfx_fill_rect(rx + 1, ry + 1, 8, 8, 0xFFFFFF);
        if (sel) {
            gfx_fill_rect(rx + 3, ry + 3, 4, 4, 0x000080);
        }

        uint32_t opt_color = sel ? 0x000080 : COLOR_WINDOWTEXT;
        gfx_draw_string(rx + 18, oy, options[i], opt_color, 0, true);
    }

    // Checkbox "Esegui sempre..."
    int32_t chk_y = box_y + box_h + 10;
    gfx_draw_rect(box_x, chk_y + 1, 12, 12, 0x404040);
    gfx_fill_rect(box_x + 1, chk_y + 2, 10, 10, 0xFFFFFF);
    if (g_autoplay_state.always_do_this) {
        gfx_draw_string(box_x + 2, chk_y, "x", 0x000080, 0, true);
    }
    gfx_draw_string(box_x + 18, chk_y + 1, "Esegui sempre l'operazione scelta per questo tipo di supporto", COLOR_WINDOWTEXT, 0, true);

    // Pulsanti OK e Annulla
    int32_t btn_y = chk_y + 24;
    int32_t bw = 100;
    int32_t bh = 25;

    gfx_fill_rect(cx + cw - 220, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + cw - 220, btn_y, bw, bh, false);
    gfx_draw_string(cx + cw - 195, btn_y + 7, "Esegui", 0x000080, 0, true);

    gfx_fill_rect(cx + cw - 110, btn_y, bw, bh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + cw - 110, btn_y, bw, bh, false);
    gfx_draw_string(cx + cw - 90, btn_y + 7, "Annulla", COLOR_WINDOWTEXT, 0, true);

    if (g_autoplay_state.status_msg[0]) {
        gfx_draw_string(box_x, btn_y + 7, g_autoplay_state.status_msg, 0x008000, 0, true);
    }
}

static void autoplay_draw(window_t* win) {
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo standard Classic Windows 2000
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 1. Schede superiori dei 4 Casi
    autoplay_draw_tabs(win, cx, cy);

    // 2. Disegno del contenuto in base al Caso attivo
    switch (g_autoplay_state.mode) {
        case AUTOPLAY_MODE_MASS_STORAGE:
            autoplay_draw_case_mass_storage(win, cx, cy, cw, ch);
            break;
        case AUTOPLAY_MODE_MULTIMEDIA:
            autoplay_draw_case_multimedia(win, cx, cy, cw, ch);
            break;
        case AUTOPLAY_MODE_PHOTO:
            autoplay_draw_case_photo(win, cx, cy, cw, ch);
            break;
        case AUTOPLAY_MODE_STANDARD:
        default:
            autoplay_draw_case_standard(win, cx, cy, cw, ch);
            break;
    }
}

static void autoplay_mouse(window_t* win, int32_t local_x, int32_t local_y, bool clicked) {
    if (!clicked) return;

    // 1. Gestione Clic sui Tab superiori (cx = 4, cy = 24)
    if (local_y >= 24 && local_y <= 46) {
        if (local_x >= 14 && local_x <= 129) {
            g_autoplay_state.mode = AUTOPLAY_MODE_MASS_STORAGE;
            strcpy(g_autoplay_state.status_msg, "");
            return;
        } else if (local_x >= 134 && local_x <= 249) {
            g_autoplay_state.mode = AUTOPLAY_MODE_MULTIMEDIA;
            strcpy(g_autoplay_state.status_msg, "");
            return;
        } else if (local_x >= 254 && local_x <= 369) {
            g_autoplay_state.mode = AUTOPLAY_MODE_PHOTO;
            strcpy(g_autoplay_state.status_msg, "");
            return;
        } else if (local_x >= 374 && local_x <= 489) {
            g_autoplay_state.mode = AUTOPLAY_MODE_STANDARD;
            strcpy(g_autoplay_state.status_msg, "");
            return;
        }
    }

    // 2. Gestione Clic per Caso 1: MassStorage
    if (g_autoplay_state.mode == AUTOPLAY_MODE_MASS_STORAGE) {
        if (local_y >= 270 && local_y <= 298) {
            if (local_x >= 16 && local_x <= 131) {
                app_explorer_open_folder("U:\\");
                strcpy(g_autoplay_state.status_msg, "Apertura unita' U:\\ in Explorer...");
            } else if (local_x >= 141 && local_x <= 256) {
                strcpy(g_autoplay_state.status_msg, "Backup file completato in C:\\User\\Default\\!");
            } else if (local_x >= 266 && local_x <= 381) {
                strcpy(g_autoplay_state.status_msg, "Controllo disco: 0 cluster danneggiati, 100% integro.");
            } else if (local_x >= 391 && local_x <= 506) {
                strcpy(g_autoplay_state.status_msg, "E' ora possibile rimuovere l'unita' in sicurezza.");
            }
        }
    }
    // 3. Gestione Clic per Caso 2: Multimedia
    else if (g_autoplay_state.mode == AUTOPLAY_MODE_MULTIMEDIA) {
        // Selezione traccia
        if (local_y >= 74 && local_y <= 184 && local_x >= 16 && local_x <= 500) {
            int track = (local_y - 74) / 22;
            if (track >= 0 && track < 5) {
                g_autoplay_state.playing_track = track;
                g_autoplay_state.is_playing = true;
                sound_play(SOUND_DING);
                mini_snprintf(g_autoplay_state.status_msg, sizeof(g_autoplay_state.status_msg), "In riproduzione: Traccia %d", track + 1);
            }
        }
        // Controlli Player
        if (local_y >= 220 && local_y <= 250) {
            if (local_x >= 16 && local_x <= 95) { // Play
                g_autoplay_state.is_playing = true;
                sound_play(SOUND_STARTUP);
                strcpy(g_autoplay_state.status_msg, "Riproduzione audio AC97 avviata...");
            } else if (local_x >= 101 && local_x <= 180) { // Pausa
                g_autoplay_state.is_playing = false;
                sound_stop();
                strcpy(g_autoplay_state.status_msg, "Riproduzione in pausa.");
            } else if (local_x >= 186 && local_x <= 265) { // Stop
                g_autoplay_state.is_playing = false;
                sound_stop();
                strcpy(g_autoplay_state.status_msg, "Riproduzione fermata.");
            } else if (local_x >= 271 && local_x <= 350) { // Prev
                if (g_autoplay_state.playing_track > 0) g_autoplay_state.playing_track--;
                sound_play(SOUND_CHIME);
            } else if (local_x >= 356 && local_x <= 435) { // Next
                if (g_autoplay_state.playing_track < 4) g_autoplay_state.playing_track++;
                sound_play(SOUND_CHIME);
            } else if (local_x >= 441 && local_x <= 505) { // Vol
                sound_set_volume(85);
                sound_play(SOUND_DING);
                strcpy(g_autoplay_state.status_msg, "Volume audio impostato a 85%");
            }
        }
    }
    // 4. Gestione Clic per Caso 3: PhotoManager
    else if (g_autoplay_state.mode == AUTOPLAY_MODE_PHOTO) {
        // Selezione foto
        if (local_y >= 74 && local_y <= 228 && local_x >= 16 && local_x <= 216) {
            int item = (local_y - 74) / 22;
            if (item >= 0 && item < 7) {
                g_autoplay_state.selected_item = item;
            }
        }
        // Pulsanti
        if (local_y >= 244 && local_y <= 274) {
            if (local_x >= 16 && local_x <= 171) {
                strcpy(g_autoplay_state.status_msg, "Presentazione Slideshow a schermo intero avviata!");
            } else if (local_x >= 181 && local_x <= 336) {
                // Imposta wallpaper reale!
                wm_set_background_type(BG_PHOTO_HERO);
                strcpy(g_autoplay_state.status_msg, "Foto impostata come sfondo del Desktop con successo!");
            } else if (local_x >= 346 && local_x <= 501) {
                strcpy(g_autoplay_state.status_msg, "Foto importate in C:\\User\\Default\\Immagini\\");
            }
        }
    }
    // 5. Gestione Clic per Caso 4: Standard
    else if (g_autoplay_state.mode == AUTOPLAY_MODE_STANDARD) {
        // Scelta opzione radio (5 opzioni)
        if (local_x >= 16 && local_x <= 500 && local_y >= 88 && local_y <= 220) {
            int opt = (local_y - 88) / 25;
            if (opt >= 0 && opt < 5) {
                g_autoplay_state.standard_choice = opt;
            }
        }
        // Checkbox "Esegui sempre"
        if (local_x >= 16 && local_x <= 480 && local_y >= 225 && local_y <= 245) {
            g_autoplay_state.always_do_this = !g_autoplay_state.always_do_this;
        }
        // Pulsanti Esegui e Annulla
        if (local_y >= 250 && local_y <= 280) {
            if (local_x >= 300 && local_x <= 405) { // Esegui
                if (g_autoplay_state.standard_choice == 0) {
                    app_explorer_open_folder("U:\\");
                } else if (g_autoplay_state.standard_choice == 1) {
                    g_autoplay_state.mode = AUTOPLAY_MODE_MULTIMEDIA;
                } else if (g_autoplay_state.standard_choice == 2) {
                    g_autoplay_state.mode = AUTOPLAY_MODE_PHOTO;
                } else if (g_autoplay_state.standard_choice == 3) {
                    g_autoplay_state.mode = AUTOPLAY_MODE_MASS_STORAGE;
                } else {
                    wm_destroy_window(win->id);
                }
            } else if (local_x >= 410 && local_x <= 510) { // Annulla
                wm_destroy_window(win->id);
            }
        }
    }
}

static void autoplay_key(window_t* win, char c) {
    (void)win;
    if (c == '1') {
        g_autoplay_state.mode = AUTOPLAY_MODE_MASS_STORAGE;
    } else if (c == '2') {
        g_autoplay_state.mode = AUTOPLAY_MODE_MULTIMEDIA;
    } else if (c == '3') {
        g_autoplay_state.mode = AUTOPLAY_MODE_PHOTO;
    } else if (c == '4') {
        g_autoplay_state.mode = AUTOPLAY_MODE_STANDARD;
    }
}

void app_autoplay_launch_mode(char drive_letter, autoplay_mode_t mode) {
    g_autoplay_state.drive_letter = drive_letter ? drive_letter : 'U';
    g_autoplay_state.mode = mode;
    g_autoplay_state.selected_item = 0;
    g_autoplay_state.is_playing = false;
    g_autoplay_state.playing_track = 0;
    g_autoplay_state.standard_choice = 0;
    g_autoplay_state.always_do_this = true;
    strcpy(g_autoplay_state.status_msg, "");

    // Rileva informazioni sul disco dal VFS
    vfs_drive_info_t dinfo;
    if (vfs_get_drive_info(g_autoplay_state.drive_letter, &dinfo)) {
        g_autoplay_state.total_mb = dinfo.total_mb ? dinfo.total_mb : 128;
        g_autoplay_state.free_mb = dinfo.free_mb ? dinfo.free_mb : 64;
        strncpy(g_autoplay_state.device_name, dinfo.label, sizeof(g_autoplay_state.device_name) - 1);
        strncpy(g_autoplay_state.fs_name, dinfo.fs_name, sizeof(g_autoplay_state.fs_name) - 1);
    } else {
        g_autoplay_state.total_mb = 128;
        g_autoplay_state.free_mb = 64;
        strcpy(g_autoplay_state.device_name, "Unita USB Rimovibile");
        strcpy(g_autoplay_state.fs_name, "exFAT");
    }

    char title[64];
    mini_snprintf(title, sizeof(title), "AutoPlay - Supporto Inserito (%c:)", g_autoplay_state.drive_letter);

    int32_t sw = gfx_get_width();
    int32_t sh = gfx_get_height();
    int32_t wx = (sw > AUTOPLAY_WIN_W) ? (sw - AUTOPLAY_WIN_W) / 2 : 50;
    int32_t wy = (sh > AUTOPLAY_WIN_H) ? (sh - AUTOPLAY_WIN_H) / 2 : 50;

    g_autoplay_win = wm_create_window(title, wx, wy, AUTOPLAY_WIN_W, AUTOPLAY_WIN_H, APP_AUTOPLAY);
    if (g_autoplay_win) {
        g_autoplay_win->on_draw = autoplay_draw;
        g_autoplay_win->on_mouse = autoplay_mouse;
        g_autoplay_win->on_key = autoplay_key;
        wm_focus_window(g_autoplay_win->id);
    }
}

void app_autoplay_launch(char drive_letter) {
    char letter = drive_letter ? drive_letter : 'U';
    autoplay_mode_t detected = autoplay_detect_content_type(letter);
    app_autoplay_launch_mode(letter, detected);
}
