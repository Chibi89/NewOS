#include <drivers/power.h>
#include <drivers/timer.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/idt.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/wm.h>
#include <string.h>

static power_status_t p_status;

void power_init(void) {
    p_status.ac_online = true;
    p_status.battery_present = true;
    p_status.is_charging = false;
    p_status.battery_percent = 100;
    p_status.profile = POWER_PROFILE_BALANCED;
    p_status.screen_timeout_sec = 180; // 3 minuti di default in bilanciato
    p_status.last_activity_ticks = timer_get_ticks();
    p_status.is_screen_standby = false;
    p_status.idle_seconds = 0;
}

void power_notify_user_activity(void) {
    p_status.last_activity_ticks = timer_get_ticks();
    p_status.idle_seconds = 0;

    // Se lo schermo è in standby, il tocco di un tasto o mouse lo risveglia istantaneamente
    if (p_status.is_screen_standby) {
        power_exit_screen_standby();
    }
}

void power_enter_screen_standby(void) {
    if (!p_status.is_screen_standby) {
        p_status.is_screen_standby = true;
        // Spegne il monitor simulando DPMS: schermo nero immediato
        gfx_clear(0x000000);
        gfx_flip();
    }
}

void power_exit_screen_standby(void) {
    if (p_status.is_screen_standby) {
        p_status.is_screen_standby = false;
        p_status.last_activity_ticks = timer_get_ticks();
        p_status.idle_seconds = 0;
        // Ridipinge immediatamente il desktop e tutte le finestre
        wm_render();
    }
}

bool power_is_screen_standby(void) {
    return p_status.is_screen_standby;
}

uint32_t power_get_idle_seconds(void) {
    return p_status.idle_seconds;
}

power_status_t power_get_status(void) {
    return p_status;
}

void power_set_profile(power_profile_t profile) {
    p_status.profile = profile;
    if (profile == POWER_PROFILE_SAVER) {
        p_status.screen_timeout_sec = 60; // 1 minuto in risparmio energia
    } else if (profile == POWER_PROFILE_BALANCED) {
        p_status.screen_timeout_sec = 180; // 3 minuti in bilanciato
    } else if (profile == POWER_PROFILE_PERFORMANCE) {
        p_status.screen_timeout_sec = 0; // Sempre attivo in prestazioni elevate
    }
}

void power_set_screen_timeout(uint32_t seconds) {
    p_status.screen_timeout_sec = seconds;
}

uint32_t power_update(void) {
    uint64_t now = timer_get_ticks();
    uint64_t elapsed = (now >= p_status.last_activity_ticks) ? (now - p_status.last_activity_ticks) : 0;
    
    // Il timer PIT è configurato a 100 Hz (100 ticks = 1 secondo)
    p_status.idle_seconds = (uint32_t)(elapsed / 100);

    // Gestione standby automatico dello schermo se abilitato
    if (p_status.screen_timeout_sec > 0 && p_status.idle_seconds >= p_status.screen_timeout_sec) {
        if (!p_status.is_screen_standby) {
            power_enter_screen_standby();
        }
    }

    // Se lo schermo è in standby, massimizza il risparmio dormendo fino a 60ms con HLT
    if (p_status.is_screen_standby) {
        return 60;
    }

    // Dynamic CPU Throttling per ridurre calore e consumo della CPU in base al profilo
    if (p_status.profile == POWER_PROFILE_PERFORMANCE) {
        return 10; // 100 FPS costanti, massima reattività
    } else if (p_status.profile == POWER_PROFILE_SAVER) {
        // Se inattivo da più di 2 secondi, scala a 50ms sleep (~20 FPS)
        if (p_status.idle_seconds >= 2) {
            return 50;
        }
        return 20; // 50 FPS standard
    } else { // POWER_PROFILE_BALANCED
        // Se inattivo da più di 5 secondi, scala a 30ms sleep (~33 FPS)
        if (p_status.idle_seconds >= 5) {
            return 30;
        }
        return 10; // 100 FPS fluido quando interattivo
    }
}

void power_shutdown(void) {
    outb(0x3F8, 'P'); // Serial log: Poweroff
    outb(0x3F8, 'O');
    outb(0x3F8, 'F');
    outb(0x3F8, 'F');

    // 1. Invio comando di spegnimento ACPI QEMU (porta standard 0x604)
    outw(0x604, 0x2000);

    // 2. Invio comando di spegnimento ACPI VirtualBox (porta standard 0x4004)
    outw(0x4004, 0x3400);
    outw(0x4004, 0x2000);

    // 3. Invio comando di spegnimento ACPI Bochs / versioni legacy QEMU (porta 0xB004)
    outw(0xB004, 0x2000);

    // 4. Magic port per emulatori specifici
    outw(0x501, 0x31);

    // Breve attesa per permettere all'hypervisor di completare lo spegnimento
    timer_sleep(100);

    // 5. Fallback protetto (se hardware fisico o hypervisor senza ACPI port trap):
    // Schermata vintage protetta di chiusura sistema (stile Windows 95/NT "È ora possibile spegnere il computer")
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();
    gfx_clear(0x000000);

    // Cornice centrale arancione e testo informativo
    int32_t box_w = 460;
    int32_t box_h = 160;
    int32_t box_x = (sw - box_w) / 2;
    int32_t box_y = (sh - box_h) / 2;

    gfx_fill_rect(box_x, box_y, box_w, box_h, 0x1A1A1A);
    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, false);

    gfx_draw_icon_32(box_x + 24, box_y + 36, ICON_ENERGY);
    gfx_draw_string(box_x + 72, box_y + 36, "ARRESTO DEL SISTEMA COMPLETATO", 0xFF9900, 0, true);
    gfx_draw_string(box_x + 72, box_y + 60, "Tutti i dati e i filesystem sono sincronizzati.", 0xCCCCCC, 0, true);
    gfx_draw_string(box_x + 72, box_y + 80, "Ora e' possibile spegnere il computer in sicurezza.", 0xFFFFFF, 0, true);
    gfx_draw_string(box_x + 72, box_y + 110, "Premi il pulsante di accensione per spegnere.", 0x888888, 0, true);

    gfx_flip();

    // Disabilita gli interrupt ed entra in halt loop a consumo zero
    cli();
    for (;;) {
        hlt();
    }
}

void power_reboot(void) {
    outb(0x3F8, 'R'); // Serial log: Reboot
    outb(0x3F8, 'B');
    outb(0x3F8, 'O');
    outb(0x3F8, 'T');

    // 1. Reset tramite porta 0x64 (8042 Keyboard Controller)
    uint8_t good = 0x02;
    for (int timeout = 0; timeout < 1000 && (good & 0x02); timeout++) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);

    // 2. Reset tramite porta PCI Reset (0xCF9) - Supportata da tutti i chipset moderni e VM
    timer_sleep(20);
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    // 3. Fallback: Triple fault provocando caricamento di una IDT nulla
    timer_sleep(20);
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(null_idt));

    // Se ancora vivo, halt
    cli();
    for (;;) {
        hlt();
    }
}
