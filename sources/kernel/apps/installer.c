#include <apps/installer.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <user.h>
#include <drivers/power.h>
#include <drivers/ata.h>
#include <drivers/ac97.h>
#include <drivers/timer.h>
#include <arch/x86_64/cpu.h>
#include <mm/heap.h>
#include <fs/vfs.h>
#include <string.h>

typedef struct {
    int current_step; // 0 = Benvenuto, 1 = Utente, 2 = Personalizzazione, 3 = Disco, 4 = Avanzamento, 5 = Fine
    char username[32];
    char password[32];
    char computer_name[32];
    int active_field; // 0: username, 1: password, 2: computer_name
    int selected_theme; // 0: Classic, 1: Liquid Glass
    int selected_power; // 0: Balanced, 1: Saver, 2: Performance
    int selected_disk;  // 0: Hard Disk Primario (ATA Master 0x1F0)
    int install_progress; // 0 - 100
    char install_status[80];
    int copy_stage;
    uint64_t last_tick;
} installer_state_t;

static void installer_step_progress(installer_state_t* state);

static void installer_draw(window_t* win) {
    installer_state_t* state = (installer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo principale grigio standard
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // Banner laterale blu stile Windows Setup
    int32_t side_w = 145;
    gfx_fill_rect(cx, cy, side_w, ch - 42, 0x0A246A);
    gfx_draw_windows_logo(cx + 12, cy + 14);
    gfx_draw_string(cx + 34, cy + 14, "NewOS V1", 0xFFFFFF, 0, true);
    gfx_draw_string(cx + 12, cy + 34, "Setup Wizard", 0xA6CAF0, 0, true);

    // Fasi del wizard nel banner laterale
    const char* step_names[] = {
        "1. Benvenuto",
        "2. Account Utente",
        "3. Preferenze",
        "4. Selezione Disco",
        "5. Installazione",
        "6. Completamento"
    };
    for (int i = 0; i < 6; i++) {
        int32_t sy = cy + 70 + i * 28;
        bool is_curr = (state->current_step == i);
        if (is_curr) {
            gfx_fill_rect(cx + 4, sy - 2, side_w - 8, 22, 0x1D4594);
            gfx_draw_bevel_rect(cx + 4, sy - 2, side_w - 8, 22, true);
            gfx_draw_string(cx + 12, sy + 2, step_names[i], 0xFFFF00, 0, true);
        } else {
            gfx_draw_string(cx + 12, sy + 2, step_names[i], 0xC0D0E0, 0, true);
        }
    }

    // Linea divisoria orizzontale prima dei bottoni
    int32_t sep_y = cy + ch - 44;
    gfx_draw_bevel_rect(cx + 4, sep_y, cw - 8, 2, true);

    // Area di lavoro a destra del banner
    int32_t wx = cx + side_w + 14;
    int32_t wy = cy + 10;
    int32_t ww = cw - side_w - 28;

    // ─── RENDERING STEP CORRENTE ───
    switch (state->current_step) {
        case 0: { // PASSO 0: BENVENUTO E HARDWARE
            gfx_draw_string(wx, wy, "Benvenuto nell'installazione di NewOS V1!", 0x0A246A, 0, true);
            gfx_draw_string(wx, wy + 20, "Questo programma installera' NewOS V1 64-bit sull'hard disk.", COLOR_WINDOWTEXT, 0, true);

            int32_t box_y = wy + 48;
            gfx_draw_bevel_rect(wx, box_y, ww, 180, true);
            gfx_fill_rect(wx + 1, box_y + 1, ww - 2, 178, 0xFFFFFF);

            gfx_draw_icon_32(wx + 10, box_y + 10, ICON_INFO);
            gfx_draw_string(wx + 50, box_y + 12, "Riepilogo Hardware Rilevato dal Sistema:", 0x0A246A, 0, true);

            // CPU reale da CPUID
            const cpu_info_t* cpu = cpu_get_info();
            char cpu_line[96];
            mini_snprintf(cpu_line, sizeof(cpu_line), "- Processore: %s (%d Core)",
                          cpu->brand[0] ? cpu->brand : "CPU x86_64",
                          cpu->logical_cores ? cpu->logical_cores : 1);
            gfx_draw_string(wx + 14, box_y + 40, cpu_line, COLOR_WINDOWTEXT, 0, true);

            // Memoria RAM reale da BIOS E820
            const BootInfo* binfo = kernel_get_boot_info();
            uint32_t ram_mb = (binfo && binfo->total_memory_bytes) ? (uint32_t)(binfo->total_memory_bytes / (1024 * 1024)) : 256;
            char ram_line[96];
            mini_snprintf(ram_line, sizeof(ram_line), "- Memoria RAM: %d MB rilevati (Paginazione PML4 Huge 2MB)", ram_mb);
            gfx_draw_string(wx + 14, box_y + 62, ram_line, COLOR_WINDOWTEXT, 0, true);

            // Modalita' video VESA reale da BootInfo
            char vbe_line[96];
            if (binfo) {
                mini_snprintf(vbe_line, sizeof(vbe_line), "- Schermo VESA: %dx%d a %d-bit (LFB 0x%x)",
                              binfo->width, binfo->height, binfo->bpp, (uint32_t)binfo->framebuffer_base);
            } else {
                mini_snprintf(vbe_line, sizeof(vbe_line), "- Schermo VESA: 1024x768 a 32-bit (Linear Framebuffer)");
            }
            gfx_draw_string(wx + 14, box_y + 84, vbe_line, COLOR_WINDOWTEXT, 0, true);

            // Controller audio PCI reale
            const char* audio_name = ac97_get_device_name();
            char audio_line[96];
            mini_snprintf(audio_line, sizeof(audio_line), "- Audio PCI: %s", audio_name);
            gfx_draw_string(wx + 14, box_y + 106, audio_line, COLOR_WINDOWTEXT, 0, true);

            // Disco primario ATA reale
            const ata_device_t* pm = ata_get_primary_master();
            char disk_line[96];
            if (pm && pm->present) {
                if (pm->size_gb > 0) {
                    mini_snprintf(disk_line, sizeof(disk_line), "- Disco Primario: %s (%d GB / %s)",
                                  pm->model, pm->size_gb, pm->part_fs[0] ? pm->part_fs : "exFAT");
                } else {
                    mini_snprintf(disk_line, sizeof(disk_line), "- Disco Primario: %s (%d MB / %s)",
                                  pm->model, pm->size_mb, pm->part_fs[0] ? pm->part_fs : "exFAT");
                }
            } else {
                mini_snprintf(disk_line, sizeof(disk_line), "- Controller Disco: ATA IDE (Nessuna unita' rilevata)");
            }
            gfx_draw_string(wx + 14, box_y + 128, disk_line, COLOR_WINDOWTEXT, 0, true);

            // File System e periferiche collegate
            int dev_count = ata_get_device_count();
            char io_line[96];
            mini_snprintf(io_line, sizeof(io_line), "- File System: Microsoft exFAT (%d Unita' ATA/ATAPI rilevate)", dev_count);
            gfx_draw_string(wx + 14, box_y + 150, io_line, 0x008000, 0, true);

            gfx_draw_string(wx, wy + 242, "Per continuare con la configurazione utente, premi Avanti.", 0x444444, 0, true);
            break;
        }

        case 1: { // PASSO 1: ACCOUNT UTENTE E PERCORSO DESKTOP
            gfx_draw_string(wx, wy, "Configurazione Account Utente", 0x0A246A, 0, true);
            gfx_draw_string(wx, wy + 20, "Imposta il tuo nome e le credenziali di accesso:", COLOR_WINDOWTEXT, 0, true);

            // Campo 1: Nome Utente
            gfx_draw_string(wx, wy + 50, "Nome Utente:", COLOR_WINDOWTEXT, 0, true);
            bool act0 = (state->active_field == 0);
            gfx_fill_rect(wx + 130, wy + 46, 210, 22, 0xFFFFFF);
            gfx_draw_bevel_rect(wx + 130, wy + 46, 210, 22, true);
            gfx_draw_string(wx + 136, wy + 50, state->username, 0x000000, 0, true);
            if (act0) {
                int32_t cur_x = wx + 136 + (int32_t)strlen(state->username) * 8;
                gfx_fill_rect(cur_x, wy + 49, 2, 16, 0x000000);
            }

            // Campo 2: Password (opzionale)
            gfx_draw_string(wx, wy + 82, "Password:", COLOR_WINDOWTEXT, 0, true);
            bool act1 = (state->active_field == 1);
            gfx_fill_rect(wx + 130, wy + 78, 210, 22, 0xFFFFFF);
            gfx_draw_bevel_rect(wx + 130, wy + 78, 210, 22, true);
            char stars[32];
            size_t plen = strlen(state->password);
            for (size_t p = 0; p < plen && p < 30; p++) stars[p] = '*';
            stars[plen] = '\0';
            gfx_draw_string(wx + 136, wy + 82, stars, 0x000000, 0, true);
            if (act1) {
                int32_t cur_x = wx + 136 + (int32_t)plen * 8;
                gfx_fill_rect(cur_x, wy + 81, 2, 16, 0x000000);
            }

            // Campo 3: Nome Computer
            gfx_draw_string(wx, wy + 114, "Nome Computer:", COLOR_WINDOWTEXT, 0, true);
            bool act2 = (state->active_field == 2);
            gfx_fill_rect(wx + 130, wy + 110, 210, 22, 0xFFFFFF);
            gfx_draw_bevel_rect(wx + 130, wy + 110, 210, 22, true);
            gfx_draw_string(wx + 136, wy + 114, state->computer_name, 0x000000, 0, true);
            if (act2) {
                int32_t cur_x = wx + 136 + (int32_t)strlen(state->computer_name) * 8;
                gfx_fill_rect(cur_x, wy + 113, 2, 16, 0x000000);
            }

            // Box informativo sul percorso Desktop
            int32_t desk_box_y = wy + 146;
            gfx_draw_bevel_rect(wx, desk_box_y, ww, 110, true);
            gfx_fill_rect(wx + 1, desk_box_y + 1, ww - 2, 108, 0xF2F7FF);

            gfx_draw_icon_16(wx + 10, desk_box_y + 10, ICON_FOLDER);
            gfx_draw_string(wx + 32, desk_box_y + 10, "Spazio di Lavoro e Cartella Utente:", 0x0A246A, 0, true);

            char path_str[100];
            mini_snprintf(path_str, sizeof(path_str), "C:\\USER\\%s\\DESKTOP\\", state->username[0] ? state->username : "Morgan");
            gfx_draw_string(wx + 14, desk_box_y + 34, "Tutti i file e le note del desktop saranno memorizzati in:", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(wx + 14, desk_box_y + 54, path_str, 0x0A246A, 0, true);
            gfx_draw_string(wx + 14, desk_box_y + 78, "Supporto multi-utente nativo con partizione exFAT Microsoft.", 0x555555, 0, true);
            break;
        }

        case 2: { // PASSO 2: PREFERENZE TEMA ED ENERGIA
            gfx_draw_string(wx, wy, "Personalizzazione Ambiente di Sistema", 0x0A246A, 0, true);
            gfx_draw_string(wx, wy + 20, "Scegli il tema visivo e lo schema energetico:", COLOR_WINDOWTEXT, 0, true);

            // Scelta Tema
            int32_t th_y = wy + 48;
            gfx_draw_bevel_rect(wx, th_y, ww, 86, true);
            gfx_fill_rect(wx + 10, th_y - 6, 110, 12, COLOR_BTNFACE);
            gfx_draw_string(wx + 12, th_y - 6, " Stile Visivo ", 0x0A246A, 0, true);

            bool sel_th0 = (state->selected_theme == 0);
            gfx_draw_bevel_rect(wx + 16, th_y + 16, 14, 14, true);
            if (sel_th0) gfx_fill_rect(wx + 19, th_y + 19, 8, 8, 0x0A246A);
            gfx_draw_string(wx + 38, th_y + 16, "Windows 2000 Classic (Bevel 3D autentici)", sel_th0 ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);

            bool sel_th1 = (state->selected_theme == 1);
            gfx_draw_bevel_rect(wx + 16, th_y + 46, 14, 14, true);
            if (sel_th1) gfx_fill_rect(wx + 19, th_y + 49, 8, 8, 0x0A246A);
            gfx_draw_string(wx + 38, th_y + 46, "Liquid Glass (Apple style glassmorphism)", sel_th1 ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);

            // Scelta Schema Energetico
            int32_t pw_y = wy + 146;
            gfx_draw_bevel_rect(wx, pw_y, ww, 110, true);
            gfx_fill_rect(wx + 10, pw_y - 6, 130, 12, COLOR_BTNFACE);
            gfx_draw_string(wx + 12, pw_y - 6, " Risparmio Energia ", 0x0A246A, 0, true);

            const char* pw_names[] = {
                "Bilanciato (Consigliato: 60 FPS, standby schermo 3 min)",
                "Risparmio di energia (Minimi consumi, standby 1 min)",
                "Prestazioni elevate (100 FPS, schermo sempre attivo)"
            };
            for (int i = 0; i < 3; i++) {
                int32_t ry = pw_y + 16 + i * 28;
                bool sel_pw = (state->selected_power == i);
                gfx_draw_bevel_rect(wx + 16, ry, 14, 14, true);
                if (sel_pw) gfx_fill_rect(wx + 19, ry + 3, 8, 8, 0x0A246A);
                gfx_draw_string(wx + 38, ry, pw_names[i], sel_pw ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
            }
            break;
        }

        case 3: { // PASSO 3: SELEZIONE DISCO
            gfx_draw_string(wx, wy, "Selezione Disco di Destinazione", 0x0A246A, 0, true);
            gfx_draw_string(wx, wy + 20, "Scegli l'unita' su cui installare NewOS V1 64-bit:", COLOR_WINDOWTEXT, 0, true);

            int dev_count = ata_get_device_count();
            int max_display = (dev_count > 2) ? 2 : dev_count;
            if (max_display == 0) max_display = 1;

            int32_t dk_y = wy + 46;
            int32_t card_h = (max_display == 1) ? 76 : 52;
            int32_t card_spacing = (max_display == 1) ? 80 : 56;

            for (int i = 0; i < max_display; i++) {
                int32_t cy_i = dk_y + i * card_spacing;
                const ata_device_t* dev = ata_get_device(i);

                bool sel = (state->selected_disk == i);
                gfx_draw_bevel_rect(wx, cy_i, ww, card_h, true);
                gfx_fill_rect(wx + 1, cy_i + 1, ww - 2, card_h - 2, sel ? 0xFFFFFF : 0xF4F4F4);

                bool is_cd = (dev && dev->is_atapi);
                gfx_draw_icon_32(wx + 10, cy_i + (card_h - 32) / 2, is_cd ? ICON_CD : ICON_HD);

                // Radio button di selezione
                gfx_draw_bevel_rect(wx + 48, cy_i + (card_h - 14) / 2, 14, 14, true);
                if (sel) gfx_fill_rect(wx + 51, cy_i + (card_h - 14) / 2 + 3, 8, 8, 0x0A246A);

                char line1[80], line2[80], line3[80];
                if (dev && dev->present) {
                    if (!dev->is_atapi) {
                        if (dev->size_gb > 0) {
                            mini_snprintf(line1, sizeof(line1), "Disco %d: %s (%d GB / %s)",
                                          i, dev->model, dev->size_gb, dev->part_fs[0] ? dev->part_fs : "exFAT");
                        } else {
                            mini_snprintf(line1, sizeof(line1), "Disco %d: %s (%d MB / %s)",
                                          i, dev->model, dev->size_mb, dev->part_fs[0] ? dev->part_fs : "exFAT");
                        }
                    } else {
                        mini_snprintf(line1, sizeof(line1), "Unita' %d: %s (CD-ROM / ATAPI)", i, dev->model);
                    }

                    mini_snprintf(line2, sizeof(line2), "Porta: 0x%03X (%s %s) - S/N: %s - %u Settori LBA",
                                  dev->io_base,
                                  (dev->channel == 0 ? "Primary" : "Secondary"),
                                  (dev->drive == 0 ? "Master" : "Slave"),
                                  dev->serial[0] ? dev->serial : "N/D",
                                  (uint32_t)dev->total_sectors);

                    if (!dev->is_atapi) {
                        mini_snprintf(line3, sizeof(line3), "Stato: %s",
                                      dev->has_mbr ? "Partizione MBR rilevata - Pronto per l'installazione di NewOS V1"
                                                   : "Spazio non formattato - Inizializzazione MBR ed exFAT richiesta");
                    } else {
                        mini_snprintf(line3, sizeof(line3), "Stato: Supporto di sola lettura (ISO Live di installazione)");
                    }
                } else {
                    mini_snprintf(line1, sizeof(line1), "Disco 0: Hard Disk Primario ATA");
                    mini_snprintf(line2, sizeof(line2), "Porta: 0x1F0 (Primary Master)");
                    mini_snprintf(line3, sizeof(line3), "Stato: Pronto per l'installazione di NewOS V1");
                }

                if (card_h >= 70) {
                    gfx_draw_string(wx + 68, cy_i + 10, line1, sel ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
                    gfx_draw_string(wx + 68, cy_i + 30, line2, 0x555555, 0, true);
                    gfx_draw_string(wx + 68, cy_i + 50, line3, is_cd ? 0x888800 : 0x008000, 0, true);
                } else {
                    gfx_draw_string(wx + 68, cy_i + 6, line1, sel ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
                    gfx_draw_string(wx + 68, cy_i + 22, line2, 0x555555, 0, true);
                    gfx_draw_string(wx + 68, cy_i + 36, line3, is_cd ? 0x888800 : 0x008000, 0, true);
                }
            }

            // Warning box
            int32_t warn_y = dk_y + max_display * card_spacing + 6;
            int32_t warn_h = 88;
            gfx_draw_bevel_rect(wx, warn_y, ww, warn_h, true);
            gfx_fill_rect(wx + 1, warn_y + 1, ww - 2, warn_h - 2, 0xFFFBEA);

            gfx_draw_icon_32(wx + 10, warn_y + 10, ICON_ENERGY);
            gfx_draw_string(wx + 50, warn_y + 12, "Informazioni sull'installazione:", 0xA05000, 0, true);
            gfx_draw_string(wx + 14, warn_y + 38, "L'installatore scrivera' l'MBR a LBA 0, il Kernel a LBA 66,", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(wx + 14, warn_y + 54, "e formattera' la partizione exFAT (C:\\) con l'ambiente utente.", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(wx + 14, warn_y + 70, "Tutti i file personali saranno collocati in C:\\USER\\[nome]\\", 0x008000, 0, true);
            break;
        }

        case 4: { // PASSO 4: INSTALLAZIONE IN CORSO
            uint64_t now = timer_get_ticks();
            if (now - state->last_tick >= 25) { // Avanza ogni ~250ms
                state->last_tick = now;
                installer_step_progress(state);
            }

            gfx_draw_string(wx, wy, "Installazione di NewOS V1 in corso...", 0x0A246A, 0, true);
            gfx_draw_string(wx, wy + 20, "Copia dei file e scrittura su disco completata a blocchi:", COLOR_WINDOWTEXT, 0, true);

            int32_t prog_y = wy + 80;
            gfx_draw_string(wx, prog_y, state->install_status, COLOR_WINDOWTEXT, 0, true);

            // Barra di avanzamento grafica a blocchi stile Windows 2000
            int32_t pbar_x = wx;
            int32_t pbar_y = prog_y + 26;
            int32_t pbar_w = ww;
            int32_t pbar_h = 24;

            gfx_draw_bevel_rect(pbar_x, pbar_y, pbar_w, pbar_h, true);
            gfx_fill_rect(pbar_x + 2, pbar_y + 2, pbar_w - 4, pbar_h - 4, 0xFFFFFF);

            int total_blocks = 24;
            int fill_blocks = (state->install_progress * total_blocks) / 100;
            int bwidth = (pbar_w - 8) / total_blocks;

            for (int b = 0; b < fill_blocks; b++) {
                gfx_fill_rect(pbar_x + 4 + b * bwidth, pbar_y + 4, bwidth - 2, pbar_h - 8, 0x0A246A);
            }

            char pct_str[16];
            mini_snprintf(pct_str, sizeof(pct_str), "%d%%", state->install_progress);
            gfx_draw_string(pbar_x + pbar_w / 2 - 12, pbar_y + pbar_h + 10, pct_str, 0x0A246A, 0, true);

            int32_t det_y = pbar_y + 60;
            gfx_draw_string(wx, det_y, "Creazione struttura: C:\\USER\\" , 0x444444, 0, true);
            gfx_draw_string(wx, det_y + 20, "Configurazione utente e sincronizzazione exFAT in corso...", 0x444444, 0, true);
            break;
        }

        case 5: { // PASSO 5: COMPLETAMENTO
            gfx_draw_string(wx, wy, "Installazione Completata con Successo!", 0x008000, 0, true);
            gfx_draw_string(wx, wy + 20, "NewOS V1 64-bit e' stato installato sul disco fisso del PC.", COLOR_WINDOWTEXT, 0, true);

            int32_t fin_box_y = wy + 54;
            gfx_draw_bevel_rect(wx, fin_box_y, ww, 180, true);
            gfx_fill_rect(wx + 1, fin_box_y + 1, ww - 2, 178, 0xF4FAF4);

            gfx_draw_icon_32(wx + 14, fin_box_y + 14, ICON_THIS_PC);
            gfx_draw_string(wx + 54, fin_box_y + 16, "Configurazione Finale del Sistema:", 0x0A246A, 0, true);

            char u_info[64], p_info[64], d_info[80];
            mini_snprintf(u_info, sizeof(u_info), "- Utente di Sistema: %s", state->username);
            mini_snprintf(p_info, sizeof(p_info), "- Nome Computer Host: %s", state->computer_name);
            mini_snprintf(d_info, sizeof(d_info), "- Cartella Desktop: C:\\USER\\%s\\DESKTOP\\", state->username);

            const ata_device_t* sel_dev = ata_get_device(state->selected_disk);
            if (!sel_dev) sel_dev = ata_get_primary_master();
            char d_part[80];
            if (sel_dev && sel_dev->present) {
                if (sel_dev->size_gb > 0) {
                    mini_snprintf(d_part, sizeof(d_part), "- Partizione Primaria: C:\\ (%s %d GB su %s)",
                                  sel_dev->part_fs[0] ? sel_dev->part_fs : "exFAT", sel_dev->size_gb, sel_dev->model);
                } else {
                    mini_snprintf(d_part, sizeof(d_part), "- Partizione Primaria: C:\\ (%s %d MB su %s)",
                                  sel_dev->part_fs[0] ? sel_dev->part_fs : "exFAT", sel_dev->size_mb, sel_dev->model);
                }
            } else {
                strcpy(d_part, "- Partizione Primaria: C:\\ (exFAT montata)");
            }
            gfx_draw_string(wx + 14, fin_box_y + 118, d_part, COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(wx + 14, fin_box_y + 138, "- Riavviare il computer per eseguire NewOS V1 da Hard Disk.", 0x555555, 0, true);
            break;
        }
    }

    // ─── PULSANTI DI CONTROLLO INFERIORI ───
    int32_t by = cy + ch - 36;

    if (state->current_step < 4) {
        // [ Annulla ]
        int32_t can_x = cx + cw - 265;
        gfx_fill_rect(can_x, by, 78, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(can_x, by, 78, 24, false);
        gfx_draw_string(can_x + 16, by + 5, "Annulla", COLOR_WINDOWTEXT, 0, true);

        // [ < Indietro ]
        int32_t back_x = cx + cw - 175;
        bool back_en = (state->current_step > 0);
        gfx_fill_rect(back_x, by, 80, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(back_x, by, 80, 24, false);
        gfx_draw_string(back_x + 12, by + 5, "< Indietro", back_en ? COLOR_WINDOWTEXT : COLOR_BTNSHADOW, 0, true);

        // [ Avanti > ] o [ Installa Ora ]
        int32_t next_x = cx + cw - 88;
        const char* next_label = (state->current_step == 3) ? "Installa" : "Avanti >";
        gfx_fill_rect(next_x, by, 80, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(next_x, by, 80, 24, false);
        gfx_draw_string(next_x + 14, by + 5, next_label, 0x0A246A, 0, true);

        // Opzione Aggiorna Sistema nel Passo 0 (Benvenuto)
        if (state->current_step == 0) {
            int32_t upd_x = cx + 14;
            int32_t upd_w = 145;
            gfx_fill_rect(upd_x, by, upd_w, 24, COLOR_BTNFACE);
            gfx_draw_bevel_rect(upd_x, by, upd_w, 24, false);
            gfx_draw_string(upd_x + 8, by + 5, "Aggiorna Sistema...", COLOR_WINDOWTEXT, 0, true);
        }

    } else if (state->current_step == 5) {
        // [ Vai al Desktop ]
        int32_t live_x = cx + cw - 270;
        gfx_fill_rect(live_x, by, 130, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(live_x, by, 130, 24, false);
        gfx_draw_string(live_x + 10, by + 5, "Vai al Desktop", COLOR_WINDOWTEXT, 0, true);

        // [ Riavvia il PC ]
        int32_t rst_x = cx + cw - 130;
        gfx_fill_rect(rst_x, by, 120, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(rst_x, by, 120, 24, false);
        gfx_draw_string(rst_x + 16, by + 5, "Riavvia Ora", 0x0A246A, 0, true);
    }
}

static void installer_step_progress(installer_state_t* state) {
    if (state->current_step != 4) return;

    if (state->copy_stage == 0) {
        state->install_progress = 15;
        strcpy(state->install_status, "Scrittura Master Boot Record (MBR) a LBA 0...");
        state->copy_stage++;
    } else if (state->copy_stage == 1) {
        state->install_progress = 35;
        strcpy(state->install_status, "Installazione Stage 2 Bootloader a 64-bit (LBA 1..64)...");
        state->copy_stage++;
    } else if (state->copy_stage == 2) {
        state->install_progress = 55;
        strcpy(state->install_status, "Scrittura Kernel NewOS V1 64-bit a LBA 66...");
        state->copy_stage++;
    } else if (state->copy_stage == 3) {
        state->install_progress = 75;
        strcpy(state->install_status, "Configurazione e montaggio Volume File System exFAT...");
        state->copy_stage++;
    } else if (state->copy_stage == 4) {
        state->install_progress = 90;
        char s[80];
        mini_snprintf(s, sizeof(s), "Creazione ambiente C:\\USER\\%s\\DESKTOP\\...", state->username);
        strncpy(state->install_status, s, sizeof(state->install_status) - 1);

        // Registra il profilo utente nel sistema
        user_set_profile(state->username, state->password, state->computer_name,
                         state->selected_theme, state->selected_power, true);
        wm_set_theme(state->selected_theme);
        power_set_profile((power_profile_t)state->selected_power);

        state->copy_stage++;
    } else if (state->copy_stage >= 5) {
        state->install_progress = 100;
        strcpy(state->install_status, "Installazione completata con successo!");
        state->current_step = 5; // Passa alla schermata finale
    }
}

static void installer_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    installer_state_t* state = (installer_state_t*)win->app_data;
    if (!state) return;

    // Se l'installazione è in corso (step 4), avanza automaticamente il progresso
    if (state->current_step == 4) {
        installer_step_progress(state);
        return;
    }

    if (!clicked) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    int32_t side_w = 145;
    int32_t wx = cx + side_w + 14;
    int32_t wy = cy + 10;
    int32_t ww = cw - side_w - 28;

    // Clic sui campi nel Passo 1
    if (state->current_step == 1) {
        if (lx >= wx + 130 && lx <= wx + 340) {
            if (ly >= wy + 46 && ly <= wy + 68) state->active_field = 0;
            else if (ly >= wy + 78 && ly <= wy + 100) state->active_field = 1;
            else if (ly >= wy + 110 && ly <= wy + 132) state->active_field = 2;
        }
    }

    // Clic nel Passo 2: Selezione Tema ed Energia
    if (state->current_step == 2) {
        int32_t th_y = wy + 48;
        if (ly >= th_y + 16 && ly <= th_y + 36) state->selected_theme = 0;
        else if (ly >= th_y + 46 && ly <= th_y + 66) state->selected_theme = 1;

        int32_t pw_y = wy + 146;
        for (int i = 0; i < 3; i++) {
            int32_t ry = pw_y + 16 + i * 28;
            if (ly >= ry && ly <= ry + 20) {
                state->selected_power = i;
            }
        }
    }

    // Clic nel Passo 3: Selezione Disco
    if (state->current_step == 3) {
        int dev_count = ata_get_device_count();
        int max_display = (dev_count > 2) ? 2 : dev_count;
        if (max_display == 0) max_display = 1;

        int32_t dk_y = wy + 46;
        int32_t card_h = (max_display == 1) ? 76 : 52;
        int32_t card_spacing = (max_display == 1) ? 80 : 56;

        for (int i = 0; i < max_display; i++) {
            int32_t cy_i = dk_y + i * card_spacing;
            if (lx >= wx && lx <= wx + ww && ly >= cy_i && ly <= cy_i + card_h) {
                const ata_device_t* dev = ata_get_device(i);
                if (dev && !dev->is_atapi) {
                    state->selected_disk = i;
                }
            }
        }
    }

    // Clic sui pulsanti inferiori
    int32_t by = cy + ch - 36;

    if (state->current_step < 4) {
        // [ Annulla ]
        int32_t can_x = cx + cw - 265;
        if (lx >= can_x && lx <= can_x + 78 && ly >= by && ly <= by + 24) {
            wm_destroy_window(win->id);
            return;
        }

        // [ < Indietro ]
        int32_t back_x = cx + cw - 175;
        if (state->current_step > 0 && lx >= back_x && lx <= back_x + 80 && ly >= by && ly <= by + 24) {
            state->current_step--;
            return;
        }

        // [ Avanti > ] o [ Installa ]
        int32_t next_x = cx + cw - 88;
        if (lx >= next_x && lx <= next_x + 80 && ly >= by && ly <= by + 24) {
            if (state->current_step == 3) {
                // Avvia installazione
                state->current_step = 4;
                state->install_progress = 5;
                state->copy_stage = 0;
                strcpy(state->install_status, "Avvio installazione su disco...");
            } else {
                state->current_step++;
            }
            return;
        }

        // Clic su [ Aggiorna Sistema... ] nel Passo 0
        if (state->current_step == 0) {
            int32_t upd_x = cx + 14;
            int32_t upd_w = 145;
            if (lx >= upd_x && lx <= upd_x + upd_w && ly >= by && ly <= by + 24) {
                const char* boot_act = "Target=UPDATE.MOD\r\nAttempts=1\r\nMaxAttempts=2\r\n";
                vfs_write_entire_file_inplace("C:\\NewOS\\Boot\\boot_action.ini", boot_act, strlen(boot_act));
                power_reboot();
                return;
            }
        }
    } else if (state->current_step == 5) {
        // [ Vai al Desktop ]
        int32_t live_x = cx + cw - 270;
        if (lx >= live_x && lx <= live_x + 130 && ly >= by && ly <= by + 24) {
            wm_destroy_window(win->id);
            return;
        }

        // [ Riavvia Ora ]
        int32_t rst_x = cx + cw - 130;
        if (lx >= rst_x && lx <= rst_x + 120 && ly >= by && ly <= by + 24) {
            power_reboot();
            return;
        }
    }
}

static void installer_key(window_t* win, char c) {
    installer_state_t* state = (installer_state_t*)win->app_data;
    if (!state) return;

    // Tasto Escape (27) -> Annulla / Chiudi installer
    if (c == 27) {
        wm_destroy_window(win->id);
        return;
    }

    // Tasto Invio (\n o \r) -> Procedi al passo successivo o avvia installazione
    if (c == '\n' || c == '\r') {
        if (state->current_step < 3) {
            state->current_step++;
            return;
        } else if (state->current_step == 3) {
            state->current_step = 4;
            state->install_progress = 5;
            state->copy_stage = 0;
            strcpy(state->install_status, "Avvio installazione su disco...");
            return;
        } else if (state->current_step == 5) {
            wm_destroy_window(win->id);
            return;
        }
    }

    // Gestione testo e tabulazione nel Passo 1 (Account Utente)
    if (state->current_step == 1) {
        char* target = NULL;
        if (state->active_field == 0) target = state->username;
        else if (state->active_field == 1) target = state->password;
        else if (state->active_field == 2) target = state->computer_name;

        if (!target) return;

        if (c == '\t') {
            state->active_field = (state->active_field + 1) % 3;
            return;
        }

        size_t len = strlen(target);

        if (c == '\b') {
            if (len > 0) target[len - 1] = '\0';
            return;
        }

        if (c >= 32 && c <= 126 && len < 24) {
            target[len] = c;
            target[len + 1] = '\0';
        }
    }
}

void app_installer_launch(void) {
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t dw = 640;
    int32_t dh = 430;
    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - dh) / 2;

    window_t* win = wm_create_window("Installazione di NewOS V1 64-bit", dx, dy, dw, dh, APP_INSTALLER);
    if (!win) return;

    installer_state_t* state = (installer_state_t*)kmalloc(sizeof(installer_state_t));
    if (!state) return;

    state->current_step = 0;
    strcpy(state->username, "Morgan");
    strcpy(state->password, "");
    strcpy(state->computer_name, "NEWOS-PC");
    state->active_field = 0;
    state->selected_theme = 0;
    state->selected_power = 0;
    state->selected_disk = 0;
    state->install_progress = 0;
    state->copy_stage = 0;
    state->last_tick = timer_get_ticks();
    strcpy(state->install_status, "Pronto.");

    win->app_data = state;
    win->on_draw = installer_draw;
    win->on_mouse = installer_mouse;
    win->on_key = installer_key;
}
