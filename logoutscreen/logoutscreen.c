#include "logoutscreen.h"

// Header di protezione del modulo (64 byte esatti all'inizio del file)
__attribute__((section(".header"), used))
module_header_t g_logoutscreen_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_LOGOUTSCREEN,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "LOGOUTSCREEN",
    .signature      = MODULE_AUTH_TAG
};

static size_t mod_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static void mod_strcpy(char* dest, const char* src) {
    if (!dest || !src) return;
    while ((*dest++ = *src++));
}

__attribute__((section(".text.entry"), used))
int logoutscreen_main(const kernel_api_t* api, const logoutscreen_params_t* params) {
    if (!api) return LOGOUT_ACTION_CANCEL;

    if (api->serial_puts) {
        api->serial_puts("[LOGOUT.MOD] Schermata di Chiusura Sessione / Arresto in esecuzione...\n");
    }

    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    // Caricamento dell'immagine di sfondo ufficiale inserita dall'utente
    image_t* bg_img = NULL;
    if (api->image_load_from_file) {
        bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\Screens\\LOGOUT.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\LOGOUT.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resource\\wallpapers\\LOGOUT.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("/NewOS/Resources/Screens/LOGOUT.BMP");
    }

    user_profile_t* user = api->user_get_current ? api->user_get_current() : NULL;
    char username[32] = "Morgan";
    if (user && user->username[0]) {
        mod_strcpy(username, user->username);
    } else if (params && params->current_user && params->current_user[0]) {
        mod_strcpy(username, params->current_user);
    }

    int selected = 2; // Default a 2: Arresta il sistema
    bool prev_left = false;

    int32_t dw = 520;
    int32_t dh = 330;
    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - dh) / 2 - 20;

    while (1) {
        // Input da tastiera
        if (api->keyboard_has_char && api->keyboard_has_char()) {
            char key = api->keyboard_get_char();
            if (key == '1') selected = 1;
            else if (key == '2') selected = 2;
            else if (key == '3') selected = 3;
            else if (key == '4') selected = 4;
            else if (key == 27) {
                if (bg_img && api->image_free) api->image_free(bg_img);
                return LOGOUT_ACTION_CANCEL; // ESC
            }
            else if (key == '\n' || key == '\r') {
                goto perform_action;
            }
        }

        // Input mouse
        mouse_state_t mouse = {0};
        if (api->mouse_get_state) {
            api->mouse_get_state(&mouse);
        }
        bool clicked = mouse.left_button && !prev_left;
        prev_left = mouse.left_button;

        // 1. Sfondo elegante con immagine ufficiale NewOS o gradiente
        if (bg_img && api->gfx_draw_image) {
            api->gfx_draw_image(0, 0, bg_img);
        } else {
            if (api->gfx_draw_gradient_v) {
                api->gfx_draw_gradient_v(0, 0, sw, sh, 0x060E1A, 0x142840);
            } else if (api->gfx_clear) {
                api->gfx_clear(0x102030);
            }
        }

        // 2. Finestra centrale di Chiusura Sessione (stile Windows 2000 / NewOS)
        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(dx, dy, dw, dh, 0xD4D0C8); // COLOR_BTNFACE
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(dx, dy, dw, dh, false);
        }

        // Barra del titolo con gradiente
        int32_t th = 22;
        if (api->gfx_draw_gradient_h) {
            api->gfx_draw_gradient_h(dx + 3, dy + 3, dw - 6, th, 0x0A246A, 0xA6CAF0);
        }
        if (api->gfx_draw_windows_logo) {
            api->gfx_draw_windows_logo(dx + 6, dy + 5);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 26, dy + 6, "Chiudi sessione di NewOS V1 (64-bit)", 0xFFFFFF, 0, true);
        }

        // 3. Intestazione con icona
        if (api->gfx_draw_icon_32) {
            api->gfx_draw_icon_32(dx + 20, dy + 36, 25); // ICON_ENERGY o MAINTENANCE
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 64, dy + 36, "Scegliere l'operazione desiderata:", 0x0A246A, 0, true);
            api->gfx_draw_string(dx + 64, dy + 54, "Sessione attiva dell'utente: ", 0x555555, 0, true);
            api->gfx_draw_string(dx + 288, dy + 54, username, 0x000000, 0, true);
        }

        // Linea divisoria
        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(dx + 16, dy + 80, dw - 32, 1, 0x808080);
            api->gfx_fill_rect(dx + 16, dy + 81, dw - 32, 1, 0xFFFFFF);
        }

        // 4. Le 4 Opzioni Selezionabili
        const struct {
            int id;
            const char* title;
            const char* desc;
        } options[] = {
            { 1, "[ 1 ]  Disconnetti sessione utente", "Salva le impostazioni e torna alla schermata di logon" },
            { 2, "[ 2 ]  Arresta il sistema (Poweroff)", "Salva il registro di sistema e spegne il computer via ACPI" },
            { 3, "[ 3 ]  Riavvia il computer", "Chiude tutte le applicazioni e riavvia NewOS V1" },
            { 4, "[ 4 ]  Sospendi / Standby monitor", "Mette il sistema in risparmio energetico immediato" }
        };

        int32_t opt_y = dy + 92;
        int32_t opt_h = 42;

        for (int i = 0; i < 4; i++) {
            int32_t cy = opt_y + (i * (opt_h + 4));
            bool is_sel = (selected == options[i].id);
            bool hov = (mouse.x >= dx + 20 && mouse.x <= dx + dw - 20 && mouse.y >= cy && mouse.y <= cy + opt_h);

            // Sfondo opzione
            uint32_t bg = is_sel ? 0xDCEBFA : (hov ? 0xF0F4FA : 0xD4D0C8);
            if (api->gfx_fill_rect) {
                api->gfx_fill_rect(dx + 20, cy, dw - 40, opt_h, bg);
            }
            if (api->gfx_draw_bevel_rect) {
                api->gfx_draw_bevel_rect(dx + 20, cy, dw - 40, opt_h, is_sel);
            }

            // Radio bullet
            int32_t r_x = dx + 32;
            int32_t r_y = cy + 14;
            if (api->gfx_fill_rect) {
                api->gfx_fill_rect(r_x, r_y, 12, 12, 0xFFFFFF);
            }
            if (api->gfx_draw_bevel_rect) {
                api->gfx_draw_bevel_rect(r_x, r_y, 12, 12, true);
            }
            if (is_sel && api->gfx_fill_rect) {
                api->gfx_fill_rect(r_x + 3, r_y + 3, 6, 6, 0x0A246A);
            }

            // Testi opzione
            if (api->gfx_draw_string) {
                api->gfx_draw_string(dx + 52, cy + 6, options[i].title, is_sel ? 0x0A246A : 0x000000, 0, true);
                api->gfx_draw_string(dx + 52, cy + 24, options[i].desc, 0x555555, 0, true);
            }

            if (clicked && hov) {
                selected = options[i].id;
            }
        }

        // 5. Pulsanti inferiori [ OK ] e [ Annulla ]
        int32_t btn_y = dy + dh - 40;

        int32_t btn_ok_x = dx + dw - 200;
        int32_t btn_ok_w = 85;
        int32_t btn_h = 26;
        bool hov_ok = (mouse.x >= btn_ok_x && mouse.x <= btn_ok_x + btn_ok_w && mouse.y >= btn_y && mouse.y <= btn_y + btn_h);

        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(btn_ok_x, btn_y, btn_ok_w, btn_h, hov_ok ? 0xE5F1FB : 0xD4D0C8);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(btn_ok_x, btn_y, btn_ok_w, btn_h, false);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(btn_ok_x + 32, btn_y + 6, "OK", 0x0A246A, 0, true);
        }

        int32_t btn_canc_x = dx + dw - 105;
        int32_t btn_canc_w = 85;
        bool hov_canc = (mouse.x >= btn_canc_x && mouse.x <= btn_canc_x + btn_canc_w && mouse.y >= btn_y && mouse.y <= btn_y + btn_h);

        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(btn_canc_x, btn_y, btn_canc_w, btn_h, hov_canc ? 0xE5F1FB : 0xD4D0C8);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(btn_canc_x, btn_y, btn_canc_w, btn_h, false);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(btn_canc_x + 18, btn_y + 6, "Annulla", 0x000000, 0, true);
        }

        if (clicked) {
            if (hov_ok) {
                goto perform_action;
            }
            if (hov_canc) {
                if (bg_img && api->image_free) api->image_free(bg_img);
                return LOGOUT_ACTION_CANCEL;
            }
        }

        // Badge protetto
        const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\SystemProgram\\LOGOUT.MOD - Power & Session Service ]";
        int badge_len = (int)mod_strlen(badge);
        int32_t badge_x = (sw - (badge_len * 8)) / 2;
        if (api->gfx_draw_string) {
            api->gfx_draw_string(badge_x, sh - 30, badge, 0x486581, 0, true);
        }

        // Cursore mouse
        if (api->gfx_draw_cursor) {
            api->gfx_draw_cursor(mouse.x, mouse.y);
        }

        // Blit
        if (api->gfx_flip) {
            api->gfx_flip();
        }

        if (api->timer_sleep) {
            api->timer_sleep(20);
        }
    }

perform_action:
    // Schermata di transizione e chiusura
    if (api->gfx_draw_gradient_v) {
        api->gfx_draw_gradient_v(0, 0, sw, sh, 0x020813, 0x0A1828);
    }
    if (api->gfx_draw_windows_logo) {
        api->gfx_draw_windows_logo(sw / 2 - 18, sh / 2 - 40);
    }

    const char* bye_msg = "Arresto di NewOS V1 in corso...";
    if (selected == 1) bye_msg = "Chiusura sessione utente in corso...";
    else if (selected == 3) bye_msg = "Riavvio di NewOS V1 in corso...";
    else if (selected == 4) bye_msg = "Attivazione modalita' di risparmio energia...";

    int msg_len = (int)mod_strlen(bye_msg);
    if (api->gfx_draw_string) {
        api->gfx_draw_string((sw - (msg_len * 8)) / 2, sh / 2 + 10, bye_msg, 0xFFFFFF, 0, true);
        api->gfx_draw_string((sw - (44 * 8)) / 2, sh / 2 + 32, "Salvataggio modifiche al disco exFAT (C:\\)...", 0x8DA9C4, 0, true);
    }
    if (api->gfx_flip) {
        api->gfx_flip();
    }

    // Suono di chiusura
    if (api->sound_play_wav_file) {
        if (!api->sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\RECYCLE.WAV")) {
            api->sound_play_wav_file("/NewOS/Resources/Sounds/RECYCLE.WAV");
        }
    }

    if (api->timer_sleep) {
        api->timer_sleep(1200); // 1.2 secondi di transizione elegante
    }

    if (bg_img && api->image_free) api->image_free(bg_img);

    if (selected == 1) {
        return LOGOUT_ACTION_DISCONNECT;
    } else if (selected == 2) {
        if (api->power_shutdown) {
            api->power_shutdown();
        }
        return LOGOUT_ACTION_SHUTDOWN;
    } else if (selected == 3) {
        if (api->power_reboot) {
            api->power_reboot();
        }
        return LOGOUT_ACTION_REBOOT;
    } else if (selected == 4) {
        return LOGOUT_ACTION_SUSPEND;
    }

    return LOGOUT_ACTION_CANCEL;
}
