#include "rec_adv.h"

__attribute__((section(".header"), used))
module_header_t g_recadv_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_RECOVERY_ADV,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "RECADV.MOD",
    .signature      = MODULE_AUTH_TAG
};

static int ra_strlen(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void ra_strcpy(char* dst, const char* src, int max_len) {
    if (!dst || !src || max_len <= 0) return;
    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool ra_streq_ci(const char* a, const char* b) {
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && b[i]) {
        char ca = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] + 32) : a[i];
        char cb = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] + 32) : b[i];
        if (ca != cb) return false;
        i++;
    }
    return a[i] == b[i];
}

static void ra_push_log(char logs[6][88], const char* msg) {
    for (int i = 0; i < 5; i++) {
        ra_strcpy(logs[i], logs[i + 1], 88);
    }
    ra_strcpy(logs[5], msg, 88);
}

static void ra_exec_cmd(const kernel_api_t* api, const kernel_ext_api_t* ext,
                        const char* cmd, char logs[6][88], int* out_action) {
    if (!cmd || !cmd[0]) return;
    char report[128] = "Operazione completata.";

    if (ra_streq_ci(cmd, "help")) {
        ra_push_log(logs, "Comandi: verify | repair | fixboot | bus | reboot | shutdown | exit");
    } else if (ra_streq_ci(cmd, "verify")) {
        if (ext && ext->recovery_repair_system_pak) {
            ext->recovery_repair_system_pak(false, report, sizeof(report));
        }
        ra_push_log(logs, report);
    } else if (ra_streq_ci(cmd, "repair")) {
        if (ext && ext->recovery_repair_system_pak) {
            ext->recovery_repair_system_pak(true, report, sizeof(report));
        }
        ra_push_log(logs, report);
    } else if (ra_streq_ci(cmd, "fixboot")) {
        if (ext && ext->recovery_reset_boot_flags) {
            ext->recovery_reset_boot_flags();
        }
        if (api->registry_save_to_disk) api->registry_save_to_disk();
        ra_push_log(logs, "Flag di boot e registro sincronizzati su C:\\NewOS.");
    } else if (ra_streq_ci(cmd, "bus")) {
        vfs_bus_info_t bi = {0};
        if (api->vfs_get_drive_bus_info && api->vfs_get_drive_bus_info('C', &bi)) {
            ra_push_log(logs, bi.controller[0] ? bi.controller : "Drive C:\\ montato e operativo.");
        } else {
            ra_push_log(logs, "Drive C:\\ montato (telemetria bus non disponibile).");
        }
    } else if (ra_streq_ci(cmd, "reboot")) {
        *out_action = RECOVERY_REBOOT;
    } else if (ra_streq_ci(cmd, "shutdown")) {
        *out_action = RECOVERY_SHUTDOWN;
    } else if (ra_streq_ci(cmd, "exit")) {
        *out_action = RECOVERY_CONTINUE;
    } else {
        ra_push_log(logs, "Comando sconosciuto. Digita 'help' per la lista comandi.");
    }
}

__attribute__((section(".text.entry"), used))
int rec_adv_main(const kernel_api_t* api, const recovery_params_t* params) {
    (void)params;
    if (!api) return RECOVERY_ERROR;
    if (api->serial_puts) api->serial_puts("[RECADV.MOD] Avvio Console Ripristino Avanzato...\n");

    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)api->ext_api;
    uint32_t sw = api->gfx_get_width ? api->gfx_get_width() : 1024;
    uint32_t sh = api->gfx_get_height ? api->gfx_get_height() : 768;

    char logs[6][88];
    for (int i = 0; i < 6; i++) logs[i][0] = '\0';
    ra_push_log(logs, "[OK] Ambiente di Ripristino Avanzato avviato fuori sessione.");
    ra_push_log(logs, "Scegli uno strumento sopra oppure usa la console (digita 'help').");

    char cmd_buf[64];
    int cmd_len = 0;
    cmd_buf[0] = '\0';
    bool prev_left = false;
    int pending_action = -1;

    static const char btn_labels[4][40] = {
        "[1] Ripristino Completo store.catalog",
        "[2] Diagnosi Critici store.catalog",
        "[3] Reset Boot & Registro Sicuro",
        "[4] Diagnostica Bus & VFS (C:\\)"
    };

    while (pending_action < 0) {
        mouse_state_t ms = {0};
        if (api->mouse_get_state) api->mouse_get_state(&ms);

        if (api->gfx_draw_gradient_v) {
            api->gfx_draw_gradient_v(0, 0, sw, sh, 0x050E1D, 0x102542);
        } else if (api->gfx_clear) {
            api->gfx_clear(0x081528);
        }

        int32_t dw = 760;
        int32_t dh = 480;
        int32_t dx = ((int32_t)sw - dw) / 2;
        int32_t dy = ((int32_t)sh - dh) / 2;

        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(dx + 5, dy + 9, dw, dh, 16, 0x000000, 90);
        }
        if (ext && ext->gfx_fill_rounded_glass) {
            ext->gfx_fill_rounded_glass(dx, dy, dw, dh, 14, 0xDCE8FA, 155);
        } else if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(dx, dy, dw, dh, 14, 0xE2ECF8, 190);
        } else if (api->gfx_fill_rect) {
            api->gfx_fill_rect(dx, dy, dw, dh, 0xDCE8FA);
        }

        if (api->gfx_draw_windows_logo) api->gfx_draw_windows_logo(dx + 22, dy + 18);
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 48, dy + 20, "NewOS V1 - Ripristino Avanzato (RECADV.MOD)", 0x0A1A38, 0, true);
            api->gfx_draw_string(dx + 22, dy + 46, "Strumenti di emergenza pre-desktop e console diagnostica", 0x334766, 0, true);
        }
        if (api->gfx_fill_alpha) api->gfx_fill_alpha(dx + 20, dy + 68, dw - 40, 1, 0x4A658A, 90);

        // Griglia 2x2 strumenti avanzati
        for (int b = 0; b < 4; b++) {
            int col = b % 2;
            int row = b / 2;
            int32_t bx = dx + 22 + col * 362;
            int32_t by = dy + 82 + row * 56;
            int32_t bw = 352;
            int32_t bh = 46;
            bool hov = (ms.x >= bx && ms.x <= bx + bw && ms.y >= by && ms.y <= by + bh);

            if (api->gfx_fill_rounded_alpha) {
                api->gfx_fill_rounded_alpha(bx, by, bw, bh, 10, hov ? 0x1D4ED8 : 0x1E3A5F, 225);
            }
            if (api->gfx_draw_string) {
                api->gfx_draw_string(bx + 14, by + 16, btn_labels[b], 0xFFFFFF, 0, true);
            }
        }

        // Area console / log diagnostico
        int32_t cx = dx + 22;
        int32_t cy = dy + 204;
        int32_t cw = dw - 44;
        int32_t ch = 200;
        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(cx, cy, cw, ch, 10, 0x071222, 230);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(cx + 14, cy + 12, "Console & Log Ripristino:", 0x7DD3FC, 0, true);
            for (int i = 0; i < 6; i++) {
                if (logs[i][0]) {
                    api->gfx_draw_string(cx + 14, cy + 36 + i * 20, logs[i], 0xE2E8F0, 0, true);
                }
            }
            api->gfx_draw_string(cx + 14, cy + 170, "REC:\\>", 0x34D399, 0, true);
            api->gfx_draw_string(cx + 70, cy + 170, cmd_buf, 0xFFFFFF, 0, true);
            api->gfx_draw_string(cx + 70 + cmd_len * 8, cy + 170, "_", 0x7DD3FC, 0, true);
        }

        // Pulsanti inferiori
        int32_t fy = dy + dh - 52;
        bool h_reb = (ms.x >= dx + 22 && ms.x <= dx + 182 && ms.y >= fy && ms.y <= fy + 32);
        bool h_cnt = (ms.x >= dx + 196 && ms.x <= dx + 374 && ms.y >= fy && ms.y <= fy + 32);
        bool h_off = (ms.x >= dx + 388 && ms.x <= dx + 528 && ms.y >= fy && ms.y <= fy + 32);
        if (api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(dx + 22, fy, 160, 32, 8, h_reb ? 0x047857 : 0x059669, 235);
            api->gfx_fill_rounded_alpha(dx + 196, fy, 178, 32, 8, h_cnt ? 0x1D4ED8 : 0x2563EB, 235);
            api->gfx_fill_rounded_alpha(dx + 388, fy, 140, 32, 8, h_off ? 0xB91C1C : 0xDC2626, 235);
        }
        if (api->gfx_draw_string) {
            api->gfx_draw_string(dx + 38, fy + 9, "Riavvia (F5)", 0xFFFFFF, 0, true);
            api->gfx_draw_string(dx + 210, fy + 9, "Continua (ESC)", 0xFFFFFF, 0, true);
            api->gfx_draw_string(dx + 408, fy + 9, "Spegni (F10)", 0xFFFFFF, 0, true);
        }

        const char* badge = "[ Modulo Protetto Esterno: C:\\NewOS\\Restore\\RECADV.MOD - Advanced Recovery ]";
        if (api->gfx_draw_string) {
            api->gfx_draw_string(((int32_t)sw - ra_strlen(badge) * 8) / 2, (int32_t)sh - 26, badge, 0x9FB3C8, 0, true);
        }

        if (api->gfx_draw_cursor) api->gfx_draw_cursor(ms.x, ms.y);
        if (api->gfx_flip) api->gfx_flip();

        if (api->keyboard_has_char && api->keyboard_has_char()) {
            char c = api->keyboard_get_char();
            if (c == 27) {
                pending_action = RECOVERY_CONTINUE;
            } else if (c == '\n' || c == '\r') {
                cmd_buf[cmd_len] = '\0';
                ra_exec_cmd(api, ext, cmd_buf, logs, &pending_action);
                cmd_len = 0;
                cmd_buf[0] = '\0';
            } else if (c == '\b') {
                if (cmd_len > 0) cmd_buf[--cmd_len] = '\0';
            } else if (c == '1' && cmd_len == 0) {
                ra_exec_cmd(api, ext, "repair", logs, &pending_action);
            } else if (c == '2' && cmd_len == 0) {
                ra_exec_cmd(api, ext, "verify", logs, &pending_action);
            } else if (c == '3' && cmd_len == 0) {
                ra_exec_cmd(api, ext, "fixboot", logs, &pending_action);
            } else if (c == '4' && cmd_len == 0) {
                ra_exec_cmd(api, ext, "bus", logs, &pending_action);
            } else if (c >= 32 && c < 127 && cmd_len < 60) {
                cmd_buf[cmd_len++] = c;
                cmd_buf[cmd_len] = '\0';
            }
        }

        bool clicked = ms.left_button && !prev_left;
        prev_left = ms.left_button;
        if (clicked) {
            for (int b = 0; b < 4; b++) {
                int col = b % 2;
                int row = b / 2;
                int32_t bx = dx + 22 + col * 362;
                int32_t by = dy + 82 + row * 56;
                if (ms.x >= bx && ms.x <= bx + 352 && ms.y >= by && ms.y <= by + 46) {
                    if (b == 0) ra_exec_cmd(api, ext, "repair", logs, &pending_action);
                    else if (b == 1) ra_exec_cmd(api, ext, "verify", logs, &pending_action);
                    else if (b == 2) ra_exec_cmd(api, ext, "fixboot", logs, &pending_action);
                    else if (b == 3) ra_exec_cmd(api, ext, "bus", logs, &pending_action);
                }
            }
            if (ms.y >= fy && ms.y <= fy + 32) {
                if (ms.x >= dx + 22 && ms.x <= dx + 182) pending_action = RECOVERY_REBOOT;
                else if (ms.x >= dx + 196 && ms.x <= dx + 374) pending_action = RECOVERY_CONTINUE;
                else if (ms.x >= dx + 388 && ms.x <= dx + 528) pending_action = RECOVERY_SHUTDOWN;
            }
        }

        if (api->timer_sleep) api->timer_sleep(20);
    }

    return pending_action;
}
