#include "bootscreen.h"

// Header di protezione del modulo (esattamente 64 byte all'inizio del file)
__attribute__((section(".header"), used))
module_header_t g_bootscreen_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_BOOTSCREEN,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "BOOTSCREEN",
    .signature      = MODULE_AUTH_TAG
};

static const char* g_boot_steps[] = {
    "Inizializzazione sottosistema memoria e heap a 64-bit...",
    "Rilevamento processore x86_64, bus PCI e timer PIT...",
    "Montaggio volume file system Microsoft exFAT (C:\\)...",
    "Caricamento driver modulari da C:\\NewOS\\Driver\\...",
    "Avvio stack di rete TCP/IP ed interfacce Ethernet...",
    "Verifica crittografica e autenticazione moduli protetti...",
    "Preparazione interfaccia utente e servizi di sistema..."
};

__attribute__((section(".text.entry"), used))
int bootscreen_main(const kernel_api_t* api, const bootscreen_params_t* params) {
    if (!api) return BOOTSCREEN_ERROR;

    if (api->serial_puts) {
        api->serial_puts("[BOOTSCR.MOD] Esecuzione Modulo Bootscreen esterno protetto...\n");
    }

    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    // Tentativo di caricamento dell'immagine di sfondo ufficiale inserita dall'utente
    image_t* bg_img = NULL;
    if (api->image_load_from_file) {
        bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\Screens\\BOOTSCR.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\BOOTSCR.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resource\\wallpapers\\BOOTSCR.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("/NewOS/Resources/Screens/BOOTSCR.BMP");
    }

    if (bg_img && api->serial_puts) {
        api->serial_puts("[BOOTSCR.MOD] Immagine bootScreen caricata con successo.\n");
    }

    int total_steps = sizeof(g_boot_steps) / sizeof(g_boot_steps[0]);
    int total_frames = 120; // Circa 1.8 - 2.0 secondi a ~60fps
    int frame = 0;

    // Dimensioni barra di avanzamento centrale (posizionata elegantemente sotto la scritta NewOS)
    int32_t bar_w = 420;
    int32_t bar_h = 16;
    int32_t bar_x = (sw - bar_w) / 2;
    int32_t bar_y = bg_img ? (sh / 2 + 205) : (sh / 2 + 70);

    int32_t block_w = 16;
    int32_t block_gap = 4;
    int32_t group_w = 3 * block_w + 2 * block_gap; // 3 blocchetti scorrevoli stile Windows XP/2000

    // Suono di avvio
    if (api->sound_play_wav_file) {
        if (!api->sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\STARTUP.WAV")) {
            api->sound_play_wav_file("/NewOS/Resources/Sounds/STARTUP.WAV");
        }
    } else if (api->sound_play) {
        api->sound_play(1); // SOUND_STARTUP
    }

    while (frame < total_frames) {
        // Controllo interruzione utente se abilitata
        if (params && params->allow_skip) {
            if (api->keyboard_has_char && api->keyboard_has_char()) {
                api->keyboard_get_char();
                if (bg_img && api->image_free) api->image_free(bg_img);
                return BOOTSCREEN_SKIPPED;
            }
        }

        // 1. Sfondo: se presente l'immagine ufficiale dell'utente la disegna a schermo intero!
        if (bg_img && api->gfx_draw_image) {
            api->gfx_draw_image(0, 0, bg_img);
        } else {
            // Fallback procedurale elegante con gradiente blu profondo
            if (api->gfx_draw_gradient_v) {
                api->gfx_draw_gradient_v(0, 0, sw, sh, 0x050C1A, 0x18345C);
            } else if (api->gfx_clear) {
                api->gfx_clear(0x0A246A);
            }

            // Logo Windows / NewOS centrale a 4 quadranti
            int32_t logo_x = sw / 2 - 18;
            int32_t logo_y = sh / 2 - 95;
            if (api->gfx_draw_windows_logo) {
                api->gfx_draw_windows_logo(logo_x, logo_y);
            }

            // Titolo principale con ombra
            const char* title = "NewOS V1 64-bit Edition";
            int32_t title_w = 23 * 8; // ~184 pixel
            int32_t title_x = (sw - title_w) / 2;
            int32_t title_y = logo_y + 42;

            if (api->gfx_draw_string) {
                api->gfx_draw_string(title_x + 1, title_y + 1, title, 0x000000, 0, true);
                api->gfx_draw_string(title_x, title_y, title, 0xFFFFFF, 0, true);
            }

            // Sottotitolo e copyright
            const char* sub = "Copyright (C) @ Newspoint Software - NewSoft - NewOS V1";
            int32_t sub_w = 54 * 8;
            int32_t sub_x = (sw - sub_w) / 2;
            int32_t sub_y = title_y + 22;

            if (api->gfx_draw_string) {
                api->gfx_draw_string(sub_x, sub_y, sub, 0x8DA9C4, 0, true);
            }
        }

        // 2. Cornice della barra di avanzamento (stile beveled 3D)
        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x07152B);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, true);
        }

        // 3. Animazione a blocchi scorrevoli (XP/2000 pulse animation)
        int track_inner_w = bar_w - 4;
        int pos = (frame * 10) % (track_inner_w + group_w) - group_w;

        for (int b = 0; b < 3; b++) {
            int32_t bx = bar_x + 2 + pos + b * (block_w + block_gap);
            int32_t bw = block_w;

            // Ritaglio all'interno della barra
            if (bx < bar_x + 2) {
                bw -= (bar_x + 2 - bx);
                bx = bar_x + 2;
            }
            if (bx + bw > bar_x + bar_w - 2) {
                bw = (bar_x + bar_w - 2) - bx;
            }

            if (bw > 0 && bx >= bar_x + 2 && bx < bar_x + bar_w - 2) {
                if (api->gfx_draw_gradient_v) {
                    api->gfx_draw_gradient_v(bx, bar_y + 2, bw, bar_h - 4, 0x3A86FF, 0x0047AB);
                } else if (api->gfx_fill_rect) {
                    api->gfx_fill_rect(bx, bar_y + 2, bw, bar_h - 4, 0x0A58CA);
                }
            }
        }

        // 4. Messaggio di stato dinamico
        int step_idx = (frame * total_steps) / total_frames;
        if (step_idx >= total_steps) step_idx = total_steps - 1;
        const char* step_msg = g_boot_steps[step_idx];

        int msg_len = 0;
        while (step_msg[msg_len]) msg_len++;
        int32_t msg_x = (sw - (msg_len * 8)) / 2;
        int32_t msg_y = bar_y + bar_h + 14;

        if (api->gfx_draw_string) {
            api->gfx_draw_string(msg_x, msg_y, step_msg, 0xC5D3E8, 0, true);
        }

        // 5. Badge protetto in basso
        const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\SystemProgram\\BOOTSCR.MOD - Ring 0 Native ]";
        int badge_len = 0;
        while (badge[badge_len]) badge_len++;
        int32_t badge_x = (sw - (badge_len * 8)) / 2;
        if (api->gfx_draw_string) {
            api->gfx_draw_string(badge_x, sh - 28, badge, 0x486581, 0, true);
        }

        // 6. Blit a video
        if (api->gfx_flip) {
            api->gfx_flip();
        }

        if (api->timer_sleep) {
            api->timer_sleep(16); // ~60 FPS
        }

        frame++;
    }

    if (bg_img && api->image_free) {
        api->image_free(bg_img);
    }

    if (api->serial_puts) {
        api->serial_puts("[BOOTSCR.MOD] Splash screen completato con successo.\n");
    }

    return BOOTSCREEN_OK;
}
