#include <types.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/sched.h>
#include <arch/x86_64/syscall.h>
#include <ipc/ipc.h>
#include <drivers/timer.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/rtc.h>
#include <drivers/power.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <gui/graphics.h>
#include <gui/wm.h>
#include <gui/theme_mgr.h>
#include <gui/cursor_loader.h>
#include <gui/context_menu.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <fs/vfs.h>
#include <user.h>
#include <apps/boot_menu.h>
#include <apps/installer.h>
#include <arch/x86_64/cpu.h>
#include <drivers/ata.h>
#include <drivers/ahci.h>
#include <drivers/driver_mgr.h>
#include <drivers/vga.h>
#include <net/socket.h>
#include <fs/registry.h>
#include <gui/screen_mgr.h>
#include <apps/autoplay.h>
#include <apps/player.h>
#include <apps/whatsnew.h>
#include <drivers/usb/usb_msc.h>
#include <string.h>

static BootInfo g_boot_info;

const BootInfo* kernel_get_boot_info(void) {
    return &g_boot_info;
}

static void serial_puts(const char* s) {
    if (!s) return;
    while (*s) {
        outb(0x3F8, *s++);
    }
}

static void sys_monitor_task(void* arg) {
    (void)arg;
    while (1) {
        sched_sleep(1000);
    }
}

void kernel_main(BootInfo* binfo) {
    if (binfo) {
        memcpy(&g_boot_info, binfo, sizeof(BootInfo));
    }

    outb(0x3F8, 'K');
    serial_puts("\n=== NewOS V1 64-bit Kernel ===\nCopyright (C) @ Newspoint Software - NewSoft - NewOS V1\n");

    // 1. Heap protetto: posizionato rigorosamente DOPO la fine del kernel BSS (_kernel_end)
    extern uint8_t _kernel_end[];
    uint64_t heap_start = ((uint64_t)_kernel_end + 0x0FFFFFULL) & ~0x0FFFFFULL;
    if (heap_start < 0x400000ULL) heap_start = 0x400000ULL; // Cuscinetto di sicurezza minimo: 4 MB
    uint64_t total_ram = (binfo && binfo->total_memory_bytes > heap_start) ? binfo->total_memory_bytes : (256ULL * 1024 * 1024);
    uint64_t heap_size = 128ULL * 1024 * 1024;
    if (total_ram > heap_start && (total_ram - heap_start) < heap_size) {
        heap_size = total_ram - heap_start;
    }
    heap_init(heap_start, heap_size);
    paging_init();
    outb(0x3F8, '1');

    // Inizializza rilevazioni hardware reali (CPU e unità ATA)
    cpu_init();
    ata_init();

    // Inizializza VFS e monta volume exFAT C:\ (LBA 2048)
    if (vfs_init()) {
        outb(0x3F8, 'F');
    }

    // Inizializza Sottosistema Modulare Driver (C:\SYSTEM\DRIVERS\)
    driver_mgr_init();

    // 2. IDT
    idt_init();
    outb(0x3F8, '2');

    // 3. GFX e Driver Video VGA/BGA
    gfx_init(binfo);
    vga_init();
    outb(0x3F8, '3');

    // 4. Drivers
    timer_init(100);
    smp_init();
    sched_init();
    ipc_init();
    syscall_init();
    task_create("SysMonitor", sys_monitor_task, NULL, 2);
    keyboard_init();
    mouse_init(binfo->width, binfo->height);
    sound_init();
    power_init();
    outb(0x3F8, '4');

    // 5. Inizializzazione Profilo Utente e Registro di Sistema
    outb(0x3F8, 'U');
    user_init();
    outb(0x3F8, 'R');
    registry_init();
    outb(0x3F8, 'P');

    // 6. Abilita solo gli interrupt dei driver registrati nel PIC
    pic_clear_mask(0);  // Timer PIT
    pic_clear_mask(1);  // Tastiera PS/2
    pic_clear_mask(2);  // Cascade linea PIC secondario
    pic_clear_mask(12); // Mouse PS/2
    sti();
    outb(0x3F8, '6');

    // Abilita la modalita' asincrona ad interrupt per AHCI ora che IDT e PIC sono attivi
    ahci_enable_interrupts();

    // Inizializzazione Stack di Rete dopo abilitazione interrupt
    net_init();

    // 7. Esecuzione Modulo Bootscreen Esterno Protetto (Splash Screen ufficiale con logo NewOS)
    screen_mgr_init();
    screen_mgr_run_bootscreen();

    // 7b. Controllo Azione Straordinaria di Aggiornamento Fuori Sessione (UPDATE.MOD)
    bool was_updating = screen_mgr_has_pending_update();
    if (was_updating) {
        update_result_t up_res = screen_mgr_run_update();
        if (up_res == UPDATE_ROLLBACK) {
            serial_puts("[KERNEL] Aggiornamento terminato con rollback di emergenza.\n");
        } else if (up_res == UPDATE_SUCCESS) {
            serial_puts("[KERNEL] Aggiornamento completato con successo. Transizione diretta a LOGIN.MOD.\n");
        }
    }

    // 8. Controllo Bypass del Boot Manager (Post-Installazione OOBE / Registro Nativo NewOS)
    // Se NewOS V1 risulta già installato (SetupStage = Completed / OOBECompleted = 1),
    // l'interfaccia interattiva del Boot Menu viene bypassata automaticamente a favore di LOGIN.MOD.
    int boot_choice = 0;
    bool bypass_boot = !was_updating && boot_menu_should_bypass();

    // Controllo eventuale override manuale dell'utente (F8 o ESC premuto durante il boot per menu avanzato)
    bool force_boot_menu = false;
    if (keyboard_has_char()) {
        char k = keyboard_get_char();
        if (k == 27) force_boot_menu = true; // ESC
    } else {
        uint8_t kstat = inb(0x64);
        if ((kstat & 0x01) && !(kstat & 0x20)) {
            uint8_t sc = inb(0x60);
            if (sc == 0x42 || sc == 0x01) force_boot_menu = true; // F8 (0x42) o ESC (0x01)
        }
    }

    if (bypass_boot && !force_boot_menu) {
        boot_action_set_direct_login();
        loginscreen_result_t login_res = screen_mgr_run_loginscreen();
        if (login_res == LOGIN_SHUTDOWN) {
            power_shutdown();
        } else if (login_res == LOGIN_REBOOT) {
            power_reboot();
        }
    } else {
        // Schermata iniziale di Selezione Boot ("1. Live" o "2. Installa NewOS V1")
        boot_choice = was_updating ? 1 : boot_menu_show();

        // 9. Fase di Installazione / Accesso e Logon:
        // Se l'utente seleziona "2. Installa NewOS V1 fisicamente su questo PC":
        // 1° BootSetup (Installer ufficiale) decifra l'archivio AES-256 SYSTEM.PAK ed estrae tutti i file.
        // 2° Eredita UI classic Win2000 con sfondo LOGIN.BMP.
        // 3° Terminato il setup ed estratti i file, procede direttamente all'avvio dell'OOBE.
        if (boot_choice == 2) {
            bootsetup_result_t setup_res = screen_mgr_run_bootsetup();
            if (setup_res == BOOTSETUP_CANCELLED) {
                power_reboot();
            } else if (setup_res == BOOTSETUP_SUCCESS) {
                oobe_result_t oobe_res = screen_mgr_run_oobe();
                if (oobe_res == OOBE_SHUTDOWN) {
                    power_shutdown();
                } else if (oobe_res == OOBE_REBOOT) {
                    power_reboot();
                }
            } else {
                power_reboot();
            }
        } else if (boot_choice != 1 && boot_choice != 4 && boot_choice != 3 && boot_choice != 6 && boot_choice != 7 && boot_choice != 8 && boot_choice != 9 && boot_choice != 10) {
            bool first_time = !oobe_is_completed();
            if (first_time) {
                oobe_result_t oobe_res = screen_mgr_run_oobe();
                if (oobe_res == OOBE_SHUTDOWN) {
                    power_shutdown();
                } else if (oobe_res == OOBE_REBOOT) {
                    power_reboot();
                }
            } else {
                loginscreen_result_t login_res = screen_mgr_run_loginscreen();
                if (login_res == LOGIN_SHUTDOWN) {
                    power_shutdown();
                } else if (login_res == LOGIN_REBOOT) {
                    power_reboot();
                }
            }
        }
    }
    outb(0x3F8, 'A');

    // 10. Fase Post-Credenziali: Interrogazione del Registro con Privilegi di Sola Lettura
    // A valle del login, il caricamento delle preferenze interroga il registro in modalita' Sola Lettura
    registry_logon_load_readonly_preferences();
    outb(0x3F8, 'B');

    // 11. Window Manager e Avvio Ambiente Desktop
    // La modifica del registro e' consentita solo una volta entrati nell'ambiente desktop
    registry_set_read_only(false);
    outb(0x3F8, 'C');
    wm_init();
    context_menu_init();
    outb(0x3F8, 'D');
    theme_mgr_init("C:/NewOS/Resource/Themes");
    int g_idx = theme_mgr_find_by_filename("LIQUID_GLASS.THEMES");
    if (g_idx >= 0) theme_mgr_apply(g_idx);
    else theme_mgr_apply(1);
    cursor_loader_init();
    // Riproduce il suono di avvio reale .WAV da disco exFAT
    if (!sound_play_wav_file("C:\\NewOS\\Resources\\Sounds\\STARTUP.WAV") &&
        !sound_play_wav_file("/SYSTEM/SOUNDS/STARTUP.WAV")) {
        sound_play(SOUND_STARTUP);
    }

    // 11. Applicazioni iniziali sul Desktop
    bool show_whatsnew = false;
    uint32_t wn_sz = 0;
    char* wn_buf = (char*)vfs_read_entire_file("C:\\NewOS\\Update\\pending_whatsnew.ini", &wn_sz);
    if (!wn_buf) wn_buf = (char*)vfs_read_entire_file("/NewOS/Update/pending_whatsnew.ini", &wn_sz);
    if (wn_buf) {
        if (strstr(wn_buf, "ShowOnFirstLogin=1")) {
            show_whatsnew = true;
        }
        kfree(wn_buf);
    }

    if (show_whatsnew) {
        app_whatsnew_launch();
    } else if (boot_choice == 3) {
        app_display_props_launch();
    } else if (boot_choice == 4) {
        app_cmd_launch();
    } else if (boot_choice == 5) {
        app_autoplay_launch('U');
    } else if (boot_choice == 6) {
        app_explorer_open_this_pc();
    } else if (boot_choice == 7) {
        app_player_launch();
    } else if (boot_choice == 8) {
        app_control_panel_open();
    } else if (boot_choice == 9) {
        app_settings_open();
    } else if (boot_choice == 10) {
        app_mouse_props_launch();
    } else {
        app_explorer_open_this_pc();
    }

    // 8. Ciclo principale del Kernel con Power Management e Throttling dinamico
    int loop_cnt = 0;
    int32_t prev_mx = -1, prev_my = -1;
    bool prev_left = false, prev_right = false;

    while (1) {
        if ((loop_cnt++ % 60) == 0) {
            outb(0x3F8, '.');
        }
        mouse_state_t mouse;
        mouse_get_state(&mouse);

        char key = 0;
        if (keyboard_has_char()) {
            key = keyboard_get_char();
        }

        // Rilevamento attività per azzerare inattività e risvegliare dallo standby
        if (mouse.x != prev_mx || mouse.y != prev_my ||
            mouse.left_button != prev_left || mouse.right_button != prev_right ||
            key != 0) {
            power_notify_user_activity();
            char mbuf[48];
            mini_snprintf(mbuf, sizeof(mbuf), "[M] x=%d y=%d btn=%d\n", mouse.x, mouse.y, (int)mouse.left_button);
            const char* p = mbuf;
            while (*p) outb(0x3F8, *p++);
            prev_mx = mouse.x;
            prev_my = mouse.y;
            prev_left = mouse.left_button;
            prev_right = mouse.right_button;
        }

        // Calcola intervallo sleep e gestisce eventuale timeout standby schermo
        uint32_t sleep_ms = power_update();

        // Se lo schermo è attivo, aggiorna la logica e ridipinge
        if (!power_is_screen_standby()) {
            wm_update(&mouse, key);
            wm_render();
        }

        // Elaborazione pacchetti di rete in arrivo
        net_poll();

        // Pausa calibrata con HLT per risparmio energetico e riduzione temperature
        timer_sleep(sleep_ms);
    }
}
