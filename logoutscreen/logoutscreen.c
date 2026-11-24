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

static void render_logout_card(const kernel_api_t* api, const kernel_ext_api_t* ext,
                               const nsmotion_api_t* motion, image_t* bg_img,
                               uint32_t sw, uint32_t sh, const char* username,
                               const char* heading, const char* status_line,
                               const char* sub_detail, uint32_t accent_col,
                               int pct_from, int pct_to, int frames) {
    const int32_t dw = 500;
    const int32_t dh = 220;
    const int32_t dx = ((int32_t)sw - dw) / 2;
    const int32_t base_dy = ((int32_t)sh - dh) / 2 - 10;

    for (int f = 0; f <= frames; f++) {
        int32_t t_q16 = (frames > 0) ? ((f * NSM_ONE) / frames) : NSM_ONE;
        int32_t e_q16 = motion && motion->ease_q16
            ? motion->ease_q16(NSM_EASE_OUT_CUBIC, t_q16)
            : t_q16;

        int32_t anim_dy = (pct_from == 0) ? (((NSM_ONE - e_q16) * 28) >> 16) : 0;
        int32_t dy = base_dy + anim_dy;
        int pct = pct_from + (((pct_to - pct_from) * e_q16) >> 16);

        if (bg_img && api->gfx_draw_image) api->gfx_draw_image(0, 0, bg_img);
        else if (api->gfx_draw_gradient_v) api->gfx_draw_gradient_v(0, 0, sw, sh, 0x040B18, 0x0D1E38);
        else if (api->gfx_clear) api->gfx_clear(0x060E1A);

        if (api->gfx_fill_alpha) api->gfx_fill_alpha(0, 0, (int32_t)sw, (int32_t)sh, 0x040A18, 52);

        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(dx + 4, dy + 10, dw, dh, 18, 0x000000, 52);
            api->gfx_fill_rounded_alpha(dx + 2, dy + 5, dw, dh, 16, 0x000000, 72);
        }
        if (ext && ext->nscomp_apply_window_refraction) {
            ext->nscomp_apply_window_refraction(dx, dy, dw, dh, dw / 2, 16);
        }
        if (ext && ext->gfx_fill_rounded_glass) {
            ext->gfx_fill_rounded_glass(dx, dy, dw, dh, 14, 0xDCE8FA, 138);
        } else if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(dx, dy, dw, dh, 14, 0xE6EEF8, 145);
        }
        if (api->gfx_fill_alpha) api->gfx_fill_alpha(dx + 14, dy + 1, dw - 28, 2, 0xFFFFFF, 160);

        if (api->gfx_draw_windows_logo) api->gfx_draw_windows_logo(dx + 20, dy + 18);
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 44, dy + 20, heading, 0x0D1B36, 0, true);
            api->gfx_draw_string(dx + 22, dy + 52, "Sessione:", 0x334766, 0, true);
            api->gfx_draw_string(dx + 102, dy + 52, username, 0x0A1A38, 0, true);
        }
        if (api->gfx_fill_alpha) api->gfx_fill_alpha(dx + 20, dy + 76, dw - 40, 1, 0x4A658A, 80);

        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 24, dy + 96, status_line, 0x0A1A38, 0, true);
            api->gfx_draw_string(dx + 24, dy + 118, sub_detail, 0x334766, 0, true);
        }

        // Barra progresso arrotondata
        int32_t bar_x = dx + 24;
        int32_t bar_y = dy + 156;
        int32_t bar_w = dw - 48;
        int32_t bar_h = 12;
        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(bar_x, bar_y, bar_w, bar_h, 6, 0xFFFFFF, 160);
            int32_t fill_w = (bar_w * pct) / 100;
            if (fill_w > 6) {
                api->gfx_fill_rounded_alpha(bar_x, bar_y, fill_w, bar_h, 6, accent_col, 240);
            }
        }

        const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\SystemProgram\\LOGOUT.MOD - Session Lifecycle ]";
        int badge_len = (int)mod_strlen(badge);
        if (api->gfx_draw_string) {
            api->gfx_draw_string(((int32_t)sw - (badge_len * 8)) / 2, (int32_t)sh - 28, badge, 0xB6C8E2, 0, true);
        }

        if (api->gfx_flip) api->gfx_flip();
        if (api->timer_sleep) api->timer_sleep(18);
    }
}

__attribute__((section(".text.entry"), used))
int logoutscreen_main(const kernel_api_t* api, const logoutscreen_params_t* params) {
    if (!api) return LOGOUT_ACTION_CANCEL;

    logout_mode_t mode = (params && params->mode != 0) ? params->mode : LOGOUT_MODE_SHUTDOWN;
    if (api->serial_puts) {
        api->serial_puts("[LOGOUT.MOD] Avvio sequenza ereditata dalla sessione...\n");
    }

    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)api->ext_api;
    const nsmotion_api_t* motion = NULL;
    if (ext && ext->get_system_service) {
        motion = (const nsmotion_api_t*)ext->get_system_service("NSMOTION");
    } else if (ext && ext->nsmotion_get_api) {
        motion = (const nsmotion_api_t*)ext->nsmotion_get_api();
    }

    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    // Preserva lo sfondo ufficiale LOGOUT.BMP
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

    if (mode == LOGOUT_MODE_CHANGE_USER) {
        render_logout_card(api, ext, motion, bg_img, sw, sh, username,
                           "Cambio Utente - NewOS V1 (64-bit)",
                           "Preparazione cambio utente...",
                           "La sessione attiva resta aperta in background.",
                           0x2563EB, 0, 100, 14);
        if (bg_img && api->image_free) api->image_free(bg_img);
        return LOGOUT_ACTION_CHANGE_USER;
    }

    if (mode == LOGOUT_MODE_CLOSE_SESSION) {
        render_logout_card(api, ext, motion, bg_img, sw, sh, username,
                           "Chiusura Sessione Utente - NewOS V1",
                           "Salvataggio impostazioni e chiusura sessione...",
                           "Ritorno alla schermata di Login.",
                           0x7C3AED, 0, 100, 14);
        if (api->sound_play_wav_file) {
            if (!api->sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\RECYCLE.WAV")) {
                api->sound_play_wav_file("/NewOS/Resources/Sounds/RECYCLE.WAV");
            }
        }
        if (bg_img && api->image_free) api->image_free(bg_img);
        return LOGOUT_ACTION_DISCONNECT;
    }

    if (mode == LOGOUT_MODE_SUSPEND) {
        render_logout_card(api, ext, motion, bg_img, sw, sh, username,
                           "Sospensione - NewOS V1",
                           "Attivazione risparmio energia...",
                           "Stato sessione preservato.",
                           0xD97706, 0, 100, 12);
        if (bg_img && api->image_free) api->image_free(bg_img);
        return LOGOUT_ACTION_SUSPEND;
    }

    /* Se si eredita il segnale di riavvio: salva il trick in boot.ini, poi esegue la sequenza Shutdown e riavvia */
    bool is_reboot = (mode == LOGOUT_MODE_REBOOT);
    if (is_reboot) {
        render_logout_card(api, ext, motion, bg_img, sw, sh, username,
                           "Riavvio di NewOS V1 (64-bit)",
                           "Salvataggio trick di riavvio in boot.ini...",
                           "C:\\NewOS\\Boot\\boot.ini -> RebootTrick=1",
                           0x059669, 0, 20, 8);
        if (ext && ext->boot_set_reboot_trick) {
            ext->boot_set_reboot_trick();
        }
        mode = LOGOUT_MODE_SHUTDOWN;
    }

    /* LOGOUT_MODE_SHUTDOWN: sequenza completa di sicurezza e integrita' */
    const char* heading = is_reboot
        ? "Riavvio di NewOS V1 (64-bit)"
        : "Arresto di NewOS V1 (64-bit)";
    uint32_t accent = is_reboot ? 0x059669 : 0xDC2626;

    render_logout_card(api, ext, motion, bg_img, sw, sh, username, heading,
                       "1/4 Salvataggio impostazioni e controllo registro...",
                       "Sincronizzazione configurazione utente e C:\\NewOS",
                       accent, 0, 35, 10);

    char check_report[128] = "SafeCheck OK";
    render_logout_card(api, ext, motion, bg_img, sw, sh, username, heading,
                       "2/4 Controllo stabilita' file critici (SafeCheck)...",
                       "Verifica moduli sistema e repair da SYSTEM.PAK",
                       accent, 35, 75, 10);
    if (ext && ext->system_shutdown_integrity_check) {
        ext->system_shutdown_integrity_check(check_report, sizeof(check_report));
    }

    render_logout_card(api, ext, motion, bg_img, sw, sh, username, heading,
                       is_reboot
                           ? "4/4 Tutte le sessioni chiuse. Innesco riavvio..."
                           : "4/4 Tutte le sessioni chiuse. Innesco spegnimento...",
                       check_report, accent, 75, 100, 12);

    if (api->sound_play_wav_file) {
        if (!api->sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\RECYCLE.WAV")) {
            api->sound_play_wav_file("/NewOS/Resources/Sounds/RECYCLE.WAV");
        }
    }
    if (api->timer_sleep) api->timer_sleep(450);
    if (bg_img && api->image_free) api->image_free(bg_img);

    if (is_reboot) {
        if (api->power_reboot) api->power_reboot();
        else if (api->power_shutdown) api->power_shutdown();
        return LOGOUT_ACTION_REBOOT;
    }

    if (api->power_shutdown) api->power_shutdown();
    return LOGOUT_ACTION_SHUTDOWN;
}
