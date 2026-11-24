#include "rec_basic.h"

__attribute__((section(".header"), used))
module_header_t g_recbasic_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_RECOVERY_BASIC,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "RECBASIC.MOD",
    .signature      = MODULE_AUTH_TAG
};

static int rb_strlen(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void rb_draw_card(const kernel_api_t* api, const kernel_ext_api_t* ext,
                         uint32_t sw, uint32_t sh, int pct,
                         const char* step_title, const char* detail_line,
                         bool done, int32_t mx, int32_t my) {
    if (api->gfx_draw_gradient_v) {
        api->gfx_draw_gradient_v(0, 0, sw, sh, 0x061020, 0x13294B);
    } else if (api->gfx_clear) {
        api->gfx_clear(0x0A192F);
    }

    int32_t dw = 560;
    int32_t dh = 270;
    int32_t dx = ((int32_t)sw - dw) / 2;
    int32_t dy = ((int32_t)sh - dh) / 2;

    if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(dx + 4, dy + 8, dw, dh, 16, 0x000000, 80);
    }
    if (ext && ext->gfx_fill_rounded_glass) {
        ext->gfx_fill_rounded_glass(dx, dy, dw, dh, 14, 0xDCE8FA, 155);
    } else if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(dx, dy, dw, dh, 14, 0xE2ECF8, 185);
    } else if (api->gfx_fill_rect) {
        api->gfx_fill_rect(dx, dy, dw, dh, 0xDCE8FA);
    }

    if (api->gfx_draw_windows_logo) api->gfx_draw_windows_logo(dx + 22, dy + 20);
    if (api->gfx_draw_string) {
        api->gfx_draw_string(dx + 48, dy + 22, "NewOS V1 - Ripristino Base (RECBASIC.MOD)", 0x0A1A38, 0, true);
        api->gfx_draw_string(dx + 22, dy + 54, "Riparazione automatica file critici e avvio", 0x334766, 0, true);
    }
    if (api->gfx_fill_alpha) api->gfx_fill_alpha(dx + 20, dy + 78, dw - 40, 1, 0x4A658A, 90);

    if (api->gfx_draw_string) {
        api->gfx_draw_string(dx + 24, dy + 98, step_title, 0x0D1B36, 0, true);
        api->gfx_draw_string(dx + 24, dy + 122, detail_line, 0x243B53, 0, true);
    }

    int32_t bar_x = dx + 24;
    int32_t bar_y = dy + 160;
    int32_t bar_w = dw - 48;
    int32_t bar_h = 14;
    if (api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(bar_x, bar_y, bar_w, bar_h, 7, 0xFFFFFF, 170);
        int32_t fw = (bar_w * pct) / 100;
        if (fw > 6) {
            api->gfx_fill_rounded_alpha(bar_x, bar_y, fw, bar_h, 7, done ? 0x059669 : 0x2563EB, 240);
        }
    }

    if (done) {
        int32_t b1x = dx + 24;
        int32_t b2x = dx + 210;
        int32_t b3x = dx + 404;
        int32_t by  = dy + 204;
        bool h1 = (mx >= b1x && mx <= b1x + 170 && my >= by && my <= by + 32);
        bool h2 = (mx >= b2x && mx <= b2x + 178 && my >= by && my <= by + 32);
        bool h3 = (mx >= b3x && mx <= b3x + 132 && my >= by && my <= by + 32);

        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(b1x, by, 170, 32, 8, h1 ? 0x047857 : 0x059669, 235);
            api->gfx_fill_rounded_alpha(b2x, by, 178, 32, 8, h2 ? 0x1D4ED8 : 0x2563EB, 235);
            api->gfx_fill_rounded_alpha(b3x, by, 132, 32, 8, h3 ? 0xB91C1C : 0xDC2626, 235);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(b1x + 16, by + 9, "Riavvia (Invio)", 0xFFFFFF, 0, true);
            api->gfx_draw_string(b2x + 16, by + 9, "Continua (ESC)", 0xFFFFFF, 0, true);
            api->gfx_draw_string(b3x + 22, by + 9, "Spegni (S)", 0xFFFFFF, 0, true);
        }
    }

    const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\Restore\\RECBASIC.MOD - Recovery ]";
    int32_t bx = ((int32_t)sw - rb_strlen(badge) * 8) / 2;
    if (api->gfx_draw_string) {
        api->gfx_draw_string(bx, (int32_t)sh - 28, badge, 0x9FB3C8, 0, true);
    }

    if (done && api->gfx_draw_cursor) {
        api->gfx_draw_cursor(mx, my);
    }
    if (api->gfx_flip) api->gfx_flip();
}

__attribute__((section(".text.entry"), used))
int rec_basic_main(const kernel_api_t* api, const recovery_params_t* params) {
    (void)params;
    if (!api) return RECOVERY_ERROR;
    if (api->serial_puts) api->serial_puts("[RECBASIC.MOD] Avvio Ripristino Base...\n");

    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)api->ext_api;
    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    rb_draw_card(api, ext, sw, sh, 20,
                 "1/3 Pulizia flag di avvio bloccanti...",
                 "Reset RebootTrick, RecoveryMode e boot_action.ini",
                 false, 0, 0);
    if (ext && ext->recovery_reset_boot_flags) {
        ext->recovery_reset_boot_flags();
    }
    if (api->timer_sleep) api->timer_sleep(350);

    char report[128] = "Verifica file critici completata.";
    rb_draw_card(api, ext, sw, sh, 60,
                 "2/3 Diagnosi da store.catalog e riparazione (SYSTEM.PAK)...",
                 "Controllo directory base, KERNEL.BIN, .MOD e SystemLibrary",
                 false, 0, 0);
    if (ext && ext->recovery_repair_system_pak) {
        ext->recovery_repair_system_pak(false, report, sizeof(report));
    }
    if (api->vfs_flush) api->vfs_flush();
    if (api->timer_sleep) api->timer_sleep(350);

    bool prev_left = false;
    while (1) {
        mouse_state_t ms = {0};
        if (api->mouse_get_state) api->mouse_get_state(&ms);

        rb_draw_card(api, ext, sw, sh, 100,
                     "3/3 Ripristino Base completato.",
                     report, true, ms.x, ms.y);

        if (api->keyboard_has_char && api->keyboard_has_char()) {
            char c = api->keyboard_get_char();
            if (c == '\n' || c == '\r' || c == 'r' || c == 'R') return RECOVERY_REBOOT;
            if (c == 27) return RECOVERY_CONTINUE;
            if (c == 's' || c == 'S') return RECOVERY_SHUTDOWN;
        }

        bool clicked = ms.left_button && !prev_left;
        prev_left = ms.left_button;
        if (clicked) {
            int32_t dx = ((int32_t)sw - 560) / 2;
            int32_t dy = ((int32_t)sh - 270) / 2;
            int32_t by = dy + 204;
            if (ms.y >= by && ms.y <= by + 32) {
                if (ms.x >= dx + 24 && ms.x <= dx + 194) return RECOVERY_REBOOT;
                if (ms.x >= dx + 210 && ms.x <= dx + 388) return RECOVERY_CONTINUE;
                if (ms.x >= dx + 404 && ms.x <= dx + 536) return RECOVERY_SHUTDOWN;
            }
        }

        if (api->timer_sleep) api->timer_sleep(25);
    }
}
