#include <apps/boot_menu.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <drivers/mouse.h>
#include <drivers/keyboard.h>
#include <drivers/timer.h>
#include <string.h>
#include <arch/x86_64/io.h>

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

int boot_menu_show(void) {
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int selected = 1; // 1 = Live, 2 = Install
    uint64_t start_tick = timer_get_ticks();
    int timeout_sec = 10;

    int32_t bw = 640;
    int32_t bh = 360;
    int32_t bx = (sw - bw) / 2;
    int32_t by = (sh - bh) / 2;

    bool prev_left = false;

    outb(0x3F8, 'B');
    serial_log("[BOOT_MENU] Pronto! In attesa selezione...\n");

    while (1) {
        uint64_t now_tick = timer_get_ticks();
        int elapsed_sec = (int)((now_tick - start_tick) / 100);
        int remaining = timeout_sec - elapsed_sec;
        if (remaining <= 0) {
            serial_log("[BOOT_MENU] Timeout scaduto, avvio Live (1)\n");
            return 1; // Default a Live
        }

        mouse_state_t mouse;
        mouse_get_state(&mouse);

        char key = 0;
        if (keyboard_has_char()) {
            key = keyboard_get_char();
        } else {
            uint8_t kstat = inb(0x64);
            if ((kstat & 0x01) && !(kstat & 0x20)) {
                uint8_t sc = inb(0x60);
                if (sc == 0x02) key = '1';
                else if (sc == 0x03) key = '2';
                else if (sc == 0x04) key = '3';
                else if (sc == 0x05) key = '4';
                else if (sc == 0x06) key = '5';
                else if (sc == 0x07) key = '6';
                else if (sc == 0x08) key = '7';
                else if (sc == 0x1C) key = '\n';
                else if (sc == 0x11) key = 'w';
                else if (sc == 0x1F) key = 's';
            }
        }
        if (key) {
            outb(0x3F8, 'K');
            outb(0x3F8, key);
        }

        // Tasti rapidi di selezione
        if (key == '1') { serial_log("[BOOT_MENU] Scelto 1\n"); return 1; }
        if (key == '2') { serial_log("[BOOT_MENU] Scelto 2\n"); return 2; }
        if (key == '3') { serial_log("[BOOT_MENU] Scelto 3\n"); return 3; }
        if (key == '4') { serial_log("[BOOT_MENU] Scelto 4\n"); return 4; }
        if (key == '5') { serial_log("[BOOT_MENU] Scelto 5\n"); return 5; }
        if (key == '6') { serial_log("[BOOT_MENU] Scelto 6\n"); return 6; }
        if (key == '7') { serial_log("[BOOT_MENU] Scelto 7 (Media Player)\n"); return 7; }
        if (key == '\n' || key == '\r') return selected;
        if (key == 'w' || key == 'W') selected = 1;
        if (key == 's' || key == 'S') selected = 2;

        // Clic del mouse
        bool clicked = mouse.left_button && !prev_left;
        prev_left = mouse.left_button;

        // 1. Sfondo a gradiente blu scuro industriale elegante
        gfx_draw_gradient_v(0, 0, sw, sh, 0x07152B, 0x1E3B66);

        // 2. Riquadro centrale finestra stile Windows 2000
        gfx_fill_rect(bx, by, bw, bh, COLOR_BTNFACE);
        gfx_draw_bevel_rect(bx, by, bw, bh, false);

        // Barra del titolo con gradiente
        gfx_draw_gradient_h(bx + 3, by + 3, bw - 6, 20, COLOR_ACTIVECAPTION_LEFT, COLOR_ACTIVECAPTION_RIGHT);
        gfx_draw_windows_logo(bx + 6, by + 5);
        gfx_draw_string(bx + 26, by + 5, "NewOS V1 64-bit Edition - Menu di Selezione Avvio", COLOR_CAPTIONTEXT, 0, true);

        // Titolo e istruzioni
        gfx_draw_string(bx + 24, by + 34, "Benvenuto in NewOS V1 64-bit!", 0x0A246A, 0, true);
        gfx_draw_string(bx + 24, by + 52, "Scegliere la modalita' desiderata con cui avviare il computer:", COLOR_WINDOWTEXT, 0, true);

        // ─── OPZIONE 1: MODALITA' LIVE ───
        int32_t op1_y = by + 76;
        bool hov1 = (mouse.x >= bx + 20 && mouse.x <= bx + bw - 20 && mouse.y >= op1_y && mouse.y <= op1_y + 64);
        bool sel1 = (selected == 1);

        gfx_fill_rect(bx + 20, op1_y, bw - 40, 64, sel1 ? 0xDCEBFA : (hov1 ? 0xF0F4FA : COLOR_BTNFACE));
        gfx_draw_bevel_rect(bx + 20, op1_y, bw - 40, 64, sel1);

        gfx_draw_icon_32(bx + 32, op1_y + 16, ICON_THIS_PC);
        gfx_draw_string(bx + 76, op1_y + 14, "[ 1 ]  Avvia NewOS V1 in modalita' Live (RAM / USB)", sel1 ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(bx + 76, op1_y + 34, "Esegue il sistema in memoria. Nessuna modifica al disco fisso.", 0x555555, 0, true);

        // ─── OPZIONE 2: INSTALLAZIONE SU PC ───
        int32_t op2_y = by + 152;
        bool hov2 = (mouse.x >= bx + 20 && mouse.x <= bx + bw - 20 && mouse.y >= op2_y && mouse.y <= op2_y + 64);
        bool sel2 = (selected == 2);

        gfx_fill_rect(bx + 20, op2_y, bw - 40, 64, sel2 ? 0xDCEBFA : (hov2 ? 0xF0F4FA : COLOR_BTNFACE));
        gfx_draw_bevel_rect(bx + 20, op2_y, bw - 40, 64, sel2);

        gfx_draw_icon_32(bx + 32, op2_y + 16, ICON_MAINTENANCE);
        gfx_draw_string(bx + 76, op2_y + 14, "[ 2 ]  Installa NewOS V1 fisicamente su questo PC", sel2 ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(bx + 76, op2_y + 34, "Configura partizione exFAT, utente e cartella C:\\USER\\[nome]", 0x555555, 0, true);

        if (clicked) {
            if (hov1) return 1;
            if (hov2) return 2;
        }

        // ─── BARRA E TESTO COUNTDOWN TIMER ───
        int32_t timer_y = by + 232;
        char timer_str[80];
        mini_snprintf(timer_str, sizeof(timer_str), "Avvio automatico Live tra: %d secondi  (Premi 1 o 2 per scegliere subito)", remaining);
        gfx_draw_string(bx + 24, timer_y, timer_str, 0x444444, 0, true);

        // Barra di avanzamento grafica
        int32_t bar_w = bw - 48;
        int32_t prog_w = (bar_w * (10 - remaining)) / 10;
        gfx_draw_bevel_rect(bx + 24, timer_y + 18, bar_w, 10, true);
        gfx_fill_rect(bx + 25, timer_y + 19, prog_w, 8, 0x0A246A);

        // ─── PULSANTI INFERIORI ───
        int32_t btn_y = by + bh - 42;

        // Pulsante [ Avvia Live ]
        int32_t b1_x = bx + bw - 260;
        gfx_fill_rect(b1_x, btn_y, 115, 26, COLOR_BTNFACE);
        gfx_draw_bevel_rect(b1_x, btn_y, 115, 26, false);
        gfx_draw_string(b1_x + 14, btn_y + 6, "Avvia Live", COLOR_WINDOWTEXT, 0, true);

        // Pulsante [ Installa Ora ]
        int32_t b2_x = bx + bw - 135;
        gfx_fill_rect(b2_x, btn_y, 115, 26, COLOR_BTNFACE);
        gfx_draw_bevel_rect(b2_x, btn_y, 115, 26, false);
        gfx_draw_string(b2_x + 14, btn_y + 6, "Installa Ora", 0x0A246A, 0, true);

        if (clicked) {
            if (mouse.x >= b1_x && mouse.x <= b1_x + 115 && mouse.y >= btn_y && mouse.y <= btn_y + 26) {
                return 1;
            }
            if (mouse.x >= b2_x && mouse.x <= b2_x + 115 && mouse.y >= btn_y && mouse.y <= btn_y + 26) {
                return 2;
            }
        }

        // Cursore del mouse
        gfx_draw_cursor(mouse.x, mouse.y);

        // Blit a schermo
        gfx_flip();
        timer_sleep(20);
    }
}
