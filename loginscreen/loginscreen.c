#include "loginscreen.h"

// Header di protezione del modulo (64 byte esatti all'inizio del file)
__attribute__((section(".header"), used))
module_header_t g_loginscreen_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_LOGINSCREEN,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "LOGINSCREEN",
    .signature      = MODULE_AUTH_TAG
};

// Funzioni helper locali di stringa (per massima indipendenza da libc)
static size_t mod_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int mod_strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void mod_strcpy(char* dest, const char* src) {
    if (!dest || !src) return;
    while ((*dest++ = *src++));
}

__attribute__((section(".text.entry"), used))
int loginscreen_main(const kernel_api_t* api, const loginscreen_params_t* params) {
    if (!api) return -1;

    if (api->serial_puts) {
        api->serial_puts("[LOGIN.MOD] Schermata di Autenticazione e Logon in esecuzione...\n");
    }

    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    // Caricamento dell'immagine di sfondo ufficiale del Logon inserita dall'utente
    image_t* bg_img = NULL;
    if (api->image_load_from_file) {
        bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\Screens\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resources\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\NewOS\\Resource\\wallpapers\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\SYSTEM\\SCREENS\\LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("C:\\SYSTEM\\SCREENS\\LOGIN.JPG");
        if (!bg_img) bg_img = api->image_load_from_file("/NewOS/Resources/Screens/LOGIN.BMP");
        if (!bg_img) bg_img = api->image_load_from_file("/SYSTEM/SCREENS/LOGIN.BMP");
    }

    // Ricava profilo utente
    user_profile_t* user = api->user_get_current ? api->user_get_current() : NULL;
    char username[32] = "Morgan";
    if (user && user->username[0]) {
        mod_strcpy(username, user->username);
    } else if (params && params->default_user[0]) {
        mod_strcpy(username, params->default_user);
    }

    char password[32] = {0};
    int  pwd_len = 0;
    bool auth_error = false;
    uint64_t err_time = 0;

    int32_t dw = 520;
    int32_t dh = 310;
    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - dh) / 2 - 20;

    bool prev_left = false;
    uint64_t cursor_tick = 0;

    while (1) {
        uint64_t now = api->timer_get_ticks ? api->timer_get_ticks() : 0;

        // Gestione input da tastiera
        if (api->keyboard_has_char && api->keyboard_has_char()) {
            char c = api->keyboard_get_char();

            if (c == '\n' || c == '\r') {
                // Invio: convalida credenziali
                bool ok = false;
                if (!user || user->password[0] == '\0') {
                    ok = true; // Nessuna password impostata
                } else if (mod_strcmp(password, user->password) == 0) {
                    ok = true;
                }

                if (ok) {
                    if (api->serial_puts) {
                        api->serial_puts("[LOGIN.MOD] Autenticazione riuscita per l'utente: ");
                        api->serial_puts(username);
                        api->serial_puts("\n");
                    }
                    if (api->sound_play_wav_file) {
                        if (!api->sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\DING.WAV")) {
                            api->sound_play_wav_file("C:\\SYSTEM\\SOUNDS\\DING.WAV");
                        }
                    }
                    if (bg_img && api->image_free) api->image_free(bg_img);
                    return LOGIN_SUCCESS;
                } else {
                    auth_error = true;
                    err_time = now;
                    password[0] = '\0';
                    pwd_len = 0;
                    if (api->sound_play) {
                        api->sound_play(2); // DING / BEEP
                    }
                }
            } else if (c == '\b') {
                if (pwd_len > 0) {
                    password[--pwd_len] = '\0';
                }
            } else if (c >= 32 && c <= 126 && pwd_len < 30) {
                password[pwd_len++] = c;
                password[pwd_len] = '\0';
                auth_error = false;
            }
        }

        // Stato mouse
        mouse_state_t mouse = {0};
        if (api->mouse_get_state) {
            api->mouse_get_state(&mouse);
        }
        bool clicked = mouse.left_button && !prev_left;
        prev_left = mouse.left_button;

        // Reset errore dopo 4 secondi
        if (auth_error && (now - err_time > 400)) {
            auth_error = false;
        }

        // 1. Sfondo desktop logon: Se presente, disegna l'immagine ufficiale (sfondo cosmico e logo NewOS)
        if (bg_img && api->gfx_draw_image) {
            api->gfx_draw_image(0, 0, bg_img);
        } else {
            if (api->gfx_draw_gradient_v) {
                api->gfx_draw_gradient_v(0, 0, sw, sh, 0x004D40, 0x008080);
            } else if (api->gfx_clear) {
                api->gfx_clear(0x008080);
            }
        }

        // 2. Finestra centrale di Logon in rilievo (Classic Windows 2000 Bevel)
        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(dx, dy, dw, dh, 0xD4D0C8); // COLOR_BTNFACE
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(dx, dy, dw, dh, false);
        }

        // Barra del titolo con gradiente blu
        int32_t th = 22;
        if (api->gfx_draw_gradient_h) {
            api->gfx_draw_gradient_h(dx + 3, dy + 3, dw - 6, th, 0x0A246A, 0xA6CAF0);
        }
        if (api->gfx_draw_windows_logo) {
            api->gfx_draw_windows_logo(dx + 6, dy + 5);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 26, dy + 6, "Accesso a NewOS V1 (64-bit Edition)", 0xFFFFFF, 0, true);
        }

        // 3. Pannello laterale avatar / informazioni
        int32_t card_x = dx + 20;
        int32_t card_y = dy + 36;
        if (api->gfx_draw_icon_32) {
            api->gfx_draw_icon_32(card_x, card_y + 10, 0); // ICON_THIS_PC
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(card_x + 44, card_y + 8, "Autenticazione Utente NewOS", 0x0A246A, 0, true);
            api->gfx_draw_string(card_x + 44, card_y + 26, "Inserire la password per avviare la sessione di lavoro.", 0x555555, 0, true);
        }

        // Separatore
        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(dx + 16, dy + 90, dw - 32, 1, 0x808080);
            api->gfx_fill_rect(dx + 16, dy + 91, dw - 32, 1, 0xFFFFFF);
        }

        // 4. Campi del modulo di autenticazione
        int32_t form_y = dy + 110;

        // Label e box "Nome utente:"
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 30, form_y + 6, "Nome utente:", 0x000000, 0, true);
        }
        int32_t field_x = dx + 130;
        int32_t field_w = dw - 160;
        int32_t field_h = 24;

        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(field_x, form_y, field_w, field_h, 0xFFFFFF);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(field_x, form_y, field_w, field_h, true);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(field_x + 8, form_y + 5, username, 0x000000, 0, true);
        }

        // Label e box "Password:"
        int32_t pwd_y = form_y + 36;
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 30, pwd_y + 6, "Password:", 0x000000, 0, true);
        }
        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(field_x, pwd_y, field_w, field_h, 0xFFFFFF);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(field_x, pwd_y, field_w, field_h, true);
        }

        // Disegna asterischi per la password
        char mask_str[36];
        for (int i = 0; i < pwd_len && i < 32; i++) {
            mask_str[i] = '*';
        }
        mask_str[pwd_len] = '\0';

        if (api->gfx_draw_string) {
            api->gfx_draw_string(field_x + 8, pwd_y + 5, mask_str, 0x000000, 0, true);
        }

        // Cursore lampeggiante nel campo password
        cursor_tick++;
        if ((cursor_tick / 20) % 2 == 0) {
            int32_t cur_x = field_x + 8 + (pwd_len * 8);
            if (api->gfx_fill_rect) {
                api->gfx_fill_rect(cur_x, pwd_y + 4, 2, 16, 0x000000);
            }
        }

        // 5. Messaggio di stato / errore o guida
        int32_t status_y = pwd_y + 34;
        if (auth_error) {
            if (api->gfx_draw_string) {
                api->gfx_draw_string(field_x, status_y, "[X] Password non corretta. Riprovare.", 0xCC0000, 0, true);
            }
        } else {
            if (api->gfx_draw_string) {
                api->gfx_draw_string(field_x, status_y, "Premere [Invio] o fare clic su Accedi", 0x666666, 0, true);
            }
        }

        // 6. Pulsanti di azione in basso
        int32_t btn_y = dy + dh - 42;

        // Pulsante [ Accedi ]
        int32_t btn_acc_x = dx + dw - 120;
        int32_t btn_acc_w = 100;
        int32_t btn_h = 26;
        bool hov_acc = (mouse.x >= btn_acc_x && mouse.x <= btn_acc_x + btn_acc_w && mouse.y >= btn_y && mouse.y <= btn_y + btn_h);

        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(btn_acc_x, btn_y, btn_acc_w, btn_h, hov_acc ? 0xE5F1FB : 0xD4D0C8);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(btn_acc_x, btn_y, btn_acc_w, btn_h, false);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(btn_acc_x + 24, btn_y + 6, "Accedi", 0x0A246A, 0, true);
        }

        // Pulsante [ Modalita' Ospite ]
        int32_t btn_gst_x = btn_acc_x - 145;
        int32_t btn_gst_w = 135;
        bool hov_gst = (mouse.x >= btn_gst_x && mouse.x <= btn_gst_x + btn_gst_w && mouse.y >= btn_y && mouse.y <= btn_y + btn_h);

        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(btn_gst_x, btn_y, btn_gst_w, btn_h, hov_gst ? 0xE5F1FB : 0xD4D0C8);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(btn_gst_x, btn_y, btn_gst_w, btn_h, false);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(btn_gst_x + 12, btn_y + 6, "Modalita' Ospite", 0x000000, 0, true);
        }

        // Pulsante [ Spegni PC ]
        int32_t btn_off_x = dx + 20;
        int32_t btn_off_w = 90;
        bool hov_off = (mouse.x >= btn_off_x && mouse.x <= btn_off_x + btn_off_w && mouse.y >= btn_y && mouse.y <= btn_y + btn_h);

        if (api->gfx_fill_rect) {
            api->gfx_fill_rect(btn_off_x, btn_y, btn_off_w, btn_h, hov_off ? 0xE5F1FB : 0xD4D0C8);
        }
        if (api->gfx_draw_bevel_rect) {
            api->gfx_draw_bevel_rect(btn_off_x, btn_y, btn_off_w, btn_h, false);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(btn_off_x + 14, btn_y + 6, "Spegni PC", 0x333333, 0, true);
        }

        // Gestione clic del mouse
        if (clicked) {
            if (hov_acc) {
                // Simula Invio
                bool ok = (!user || user->password[0] == '\0' || mod_strcmp(password, user->password) == 0);
                if (ok) {
                    if (bg_img && api->image_free) api->image_free(bg_img);
                    return LOGIN_SUCCESS;
                } else {
                    auth_error = true;
                    err_time = now;
                    password[0] = '\0';
                    pwd_len = 0;
                }
            } else if (hov_gst) {
                if (api->user_set_profile) {
                    api->user_set_profile("Ospite", "", "NEWOS-GUEST", 0, 0, false);
                }
                if (bg_img && api->image_free) api->image_free(bg_img);
                return LOGIN_GUEST;
            } else if (hov_off) {
                if (api->power_shutdown) {
                    api->power_shutdown();
                }
                if (bg_img && api->image_free) api->image_free(bg_img);
                return LOGIN_SHUTDOWN;
            }
        }

        // Badge di protezione in basso allo schermo
        const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\SystemProgram\\LOGIN.MOD - Logon & Security Service ]";
        int badge_len = (int)mod_strlen(badge);
        int32_t badge_x = (sw - (badge_len * 8)) / 2;
        if (api->gfx_draw_string) {
            api->gfx_draw_string(badge_x, sh - 30, badge, 0x88B0A8, 0, true);
        }

        // Cursore del mouse
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
}
