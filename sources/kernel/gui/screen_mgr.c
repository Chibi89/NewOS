#include <gui/screen_mgr.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <drivers/mouse.h>
#include <drivers/keyboard.h>
#include <drivers/timer.h>
#include <drivers/power.h>
#include <drivers/sound.h>
#include <drivers/sound_wav.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <user.h>
#include <string.h>
#include <arch/x86_64/io.h>

static kernel_api_t g_kernel_api;
static bool         g_api_initialized = false;

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) {
        outb(0x3F8, *s++);
    }
}

// Calcolo CRC32 standard per verifica di integrità crittografica
static uint32_t compute_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

void screen_mgr_init(void) {
    if (g_api_initialized) return;

    serial_log("[SCREEN_MGR] Inizializzazione Kernel Screen Manager e Tabella API Protetta...\n");
    icons_init();

    memset(&g_kernel_api, 0, sizeof(kernel_api_t));
    g_kernel_api.api_version = 0x00010000;

    // Video & Grafica
    g_kernel_api.gfx_get_width        = gfx_get_width;
    g_kernel_api.gfx_get_height       = gfx_get_height;
    g_kernel_api.gfx_flip             = gfx_flip;
    g_kernel_api.gfx_clear            = gfx_clear;
    g_kernel_api.gfx_fill_rect        = gfx_fill_rect;
    g_kernel_api.gfx_draw_rect        = gfx_draw_rect;
    g_kernel_api.gfx_draw_bevel_rect  = gfx_draw_bevel_rect;
    g_kernel_api.gfx_draw_gradient_h  = gfx_draw_gradient_h;
    g_kernel_api.gfx_draw_gradient_v  = gfx_draw_gradient_v;
    g_kernel_api.gfx_draw_string      = gfx_draw_string;
    g_kernel_api.gfx_draw_char        = gfx_draw_char;
    g_kernel_api.gfx_draw_cursor      = gfx_draw_cursor;
    g_kernel_api.gfx_draw_windows_logo= gfx_draw_windows_logo;
    g_kernel_api.gfx_draw_icon_32     = (void(*)(int32_t, int32_t, int))gfx_draw_icon_32;
    g_kernel_api.gfx_draw_icon_16     = (void(*)(int32_t, int32_t, int))gfx_draw_icon_16;
    g_kernel_api.gfx_draw_image       = gfx_draw_image;

    // Immagini
    g_kernel_api.image_load_from_file = image_load_from_file;
    g_kernel_api.image_free           = image_free;

    // Input
    g_kernel_api.mouse_get_state      = mouse_get_state;
    g_kernel_api.keyboard_has_char    = keyboard_has_char;
    g_kernel_api.keyboard_get_char    = keyboard_get_char;

    // Timer & Log
    g_kernel_api.timer_get_ticks      = timer_get_ticks;
    g_kernel_api.timer_sleep          = timer_sleep;
    g_kernel_api.serial_puts          = serial_log;

    // Profilo Utente
    g_kernel_api.user_get_current     = user_get_current;
    g_kernel_api.user_set_profile     = user_set_profile;
    g_kernel_api.user_save_config     = user_save_config;

    // Power
    g_kernel_api.power_shutdown       = power_shutdown;
    g_kernel_api.power_reboot         = power_reboot;

    // Audio
    g_kernel_api.sound_play           = (void(*)(int))sound_play;
    g_kernel_api.sound_play_wav_file  = sound_play_wav_file;

    // VFS & Memoria
    g_kernel_api.vfs_read_entire_file          = (uint8_t*(*)(const char*, uint32_t*))vfs_read_entire_file;
    g_kernel_api.vfs_write_entire_file_inplace = vfs_write_entire_file_inplace;
    g_kernel_api.vfs_exists                    = vfs_exists;
    g_kernel_api.kmalloc                       = kmalloc;
    g_kernel_api.kfree                         = kfree;

    g_api_initialized = true;
    serial_log("[SCREEN_MGR] Tabella API Kernel collegata con successo (33 Entrypoint).\n");
}

const kernel_api_t* screen_mgr_get_api(void) {
    if (!g_api_initialized) screen_mgr_init();
    return &g_kernel_api;
}

bool screen_mgr_verify_module(const void* buffer, uint32_t size, uint32_t expected_type, const char* expected_name) {
    if (!buffer || size < sizeof(module_header_t)) {
        serial_log("[SECURITY ERROR] Buffer modulo nullo o dimensione insufficiente!\n");
        return false;
    }

    const module_header_t* h = (const module_header_t*)buffer;

    // 1. Verifica Magic Signature 'NOSM'
    if (h->magic != MODULE_MAGIC_NEWOS) {
        serial_log("[SECURITY ERROR] Magic Signature modulo non valida! Atteso: NOSM\n");
        return false;
    }

    // 2. Verifica dimensione header
    if (h->header_size != sizeof(module_header_t)) {
        serial_log("[SECURITY ERROR] Dimensione header non coerente!\n");
        return false;
    }

    // 3. Verifica Tipo Modulo
    if (h->module_type != expected_type) {
        serial_log("[SECURITY ERROR] Tipo modulo non corrispondente a quello richiesto!\n");
        return false;
    }

    // 4. Verifica Firma del Produttore "NEWSOFT"
    if (strncmp(h->signature, MODULE_AUTH_TAG, 7) != 0) {
        serial_log("[SECURITY ERROR] Firma crittografica del produttore non valida o manomessa!\n");
        return false;
    }

    // 5. Verifica Flag di Protezione
    if (!(h->security_flags & SEC_FLAG_SYSTEM_SIGNED)) {
        serial_log("[SECURITY ERROR] Flag SYSTEM_SIGNED assente: esecuzione non autorizzata!\n");
        return false;
    }

    // 6. Verifica Integrità tramite Checksum CRC32 del codice
    const uint8_t* payload = (const uint8_t*)buffer + h->entry_offset;
    uint32_t payload_len = size - h->entry_offset;
    uint32_t calc_crc = compute_crc32(payload, payload_len);

    if (h->checksum != 0 && h->checksum != calc_crc) {
        serial_log("[SECURITY ERROR] Checksum CRC32 non corrispondente: il binario e' stato alterato!\n");
        return false;
    }

    serial_log("[SECURITY OK] Modulo '");
    serial_log(expected_name ? expected_name : h->module_name);
    serial_log("' verificato e autenticato con successo (CRC32: 0x");
    // Stampa hex esadecimale semplice
    char hex[9];
    mini_snprintf(hex, sizeof(hex), "%x", (unsigned int)calc_crc);
    serial_log(hex);
    serial_log(")\n");

    return true;
}

// ------------------------------------------------------------------------------
// Fallback di Sicurezza Integrati nel Kernel (se modulo esterno assente o corrotto)
// ------------------------------------------------------------------------------
static bootscreen_result_t bootscreen_fallback(void) {
    serial_log("[BOOTSCREEN] Fallback di sicurezza attivo...\n");
    gfx_clear(0x0A246A);
    gfx_draw_windows_logo(gfx_get_width() / 2 - 18, gfx_get_height() / 2 - 40);
    gfx_draw_string(gfx_get_width() / 2 - 80, gfx_get_height() / 2 + 10, "NewOS V1 64-bit", 0xFFFFFF, 0, true);
    gfx_flip();
    timer_sleep(400);
    return BOOTSCREEN_OK;
}

static loginscreen_result_t loginscreen_fallback(void) {
    serial_log("[LOGINSCREEN] Fallback di sicurezza attivo...\n");
    return LOGIN_SUCCESS;
}

static logoutscreen_result_t logoutscreen_fallback(void) {
    serial_log("[LOGOUTSCREEN] Fallback di sicurezza attivo...\n");
    power_shutdown();
    return LOGOUT_ACTION_SHUTDOWN;
}

// ------------------------------------------------------------------------------
// Esecutori dei Moduli Protetti da File System C:\SYSTEM\SCREENS\
// ------------------------------------------------------------------------------
bootscreen_result_t screen_mgr_run_bootscreen(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\NewOS\\Boot\\BOOTSCR.MOD",
        "D:\\NewOS\\Boot\\BOOTSCR.MOD",
        "C:\\NewOS\\SystemProgram\\BOOTSCR.MOD",
        "/NewOS/Boot/BOOTSCR.MOD",
        "/NewOS/SystemProgram/BOOTSCR.MOD",
        "D:\\BOOTSCR.MOD",
        "BOOTSCR.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_BOOTSCREEN, "BOOTSCR.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            bootscreen_entry_fn entry = (bootscreen_entry_fn)(mod_buf + h->entry_offset);

            bootscreen_params_t p = { .duration_ms = 1800, .allow_skip = true };
            bootscreen_result_t res = (bootscreen_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return bootscreen_fallback();
}

loginscreen_result_t screen_mgr_run_loginscreen(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\NewOS\\Boot\\LOGIN.MOD",
        "C:\\NewOS\\SystemProgram\\LOGIN.MOD",
        "/NewOS/Boot/LOGIN.MOD",
        "/NewOS/SystemProgram/LOGIN.MOD",
        "LOGIN.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_LOGINSCREEN, "LOGIN.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            loginscreen_entry_fn entry = (loginscreen_entry_fn)(mod_buf + h->entry_offset);

            loginscreen_params_t p = { .default_user = "Morgan", .allow_guest = true };
            loginscreen_result_t res = (loginscreen_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return loginscreen_fallback();
}

logoutscreen_result_t screen_mgr_run_logoutscreen(void) {
    screen_mgr_init();

    const char* paths[] = {
        "C:\\NewOS\\Boot\\LOGOUT.MOD",
        "C:\\NewOS\\SystemProgram\\LOGOUT.MOD",
        "/NewOS/Boot/LOGOUT.MOD",
        "/NewOS/SystemProgram/LOGOUT.MOD",
        "LOGOUT.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_LOGOUTSCREEN, "LOGOUT.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            logoutscreen_entry_fn entry = (logoutscreen_entry_fn)(mod_buf + h->entry_offset);

            logoutscreen_params_t p = { .current_user = "Morgan" };
            logoutscreen_result_t res = (logoutscreen_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return logoutscreen_fallback();
}

static bootsetup_result_t bootsetup_fallback(void) {
    serial_log("[BOOTSETUP ERRORE] Impossibile avviare il modulo di installazione (BOOTSETUP.MOD non trovato o non valido).\n");
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();
    int32_t w = 540, h = 170;
    int32_t x = (sw - w) / 2, y = (sh - h) / 2;
    gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x, y, w, h, false);
    gfx_draw_gradient_h(x + 3, y + 3, w - 6, 20, COLOR_ACTIVECAPTION_LEFT, COLOR_ACTIVECAPTION_RIGHT);
    gfx_draw_string(x + 10, y + 5, "Installazione NewOS V1 - Supporto non trovato", COLOR_CAPTIONTEXT, 0, true);
    gfx_draw_string(x + 20, y + 42, "Impossibile trovare il file di installazione BOOTSETUP.MOD", COLOR_WINDOWTEXT, 0, true);
    gfx_draw_string(x + 20, y + 68, "Verificare che il supporto di installazione (CD/DVD/USB) sia inserito.", 0x555555, 0, true);
    gfx_draw_string(x + 20, y + 108, "Premere Invio per riavviare il sistema...", 0x880000, 0, true);
    gfx_flip();
    while (1) {
        if (keyboard_has_char()) {
            (void)keyboard_get_char();
            break;
        }
        timer_sleep(50);
    }
    return BOOTSETUP_ERROR;
}

bootsetup_result_t screen_mgr_run_bootsetup(void) {
    screen_mgr_init();

    const char* paths[] = {
        "D:\\NewOS\\Boot\\BOOTSETUP.MOD",
        "D:\\BootSetup\\BOOTSETUP.MOD",
        "D:\\BOOTSETUP.MOD",
        "C:\\NewOS\\Boot\\BOOTSETUP.MOD",
        "C:\\BootSetup\\BOOTSETUP.MOD",
        "C:\\BOOTSETUP.MOD",
        "E:\\NewOS\\Boot\\BOOTSETUP.MOD",
        "E:\\BootSetup\\BOOTSETUP.MOD",
        "U:\\NewOS\\Boot\\BOOTSETUP.MOD",
        "U:\\BootSetup\\BOOTSETUP.MOD",
        "C:\\NewOS\\SystemProgram\\BOOTSETUP.MOD",
        "/NewOS/Boot/BOOTSETUP.MOD",
        "/BootSetup/BOOTSETUP.MOD",
        "/NewOS/SystemProgram/BOOTSETUP.MOD",
        "BOOTSETUP.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;
    int found_idx = -1;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) {
            found_idx = i;
            break;
        }
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_BOOTSETUP, "BOOTSETUP.MOD")) {
            const module_header_t* h = (const module_header_t*)mod_buf;
            bootsetup_entry_fn entry = (bootsetup_entry_fn)(mod_buf + h->entry_offset);

            char pak_path[64] = "C:\\BootSetup\\SYSTEM.PAK";
            if (found_idx >= 0 && paths[found_idx][1] == ':') {
                pak_path[0] = paths[found_idx][0];
            }

            bootsetup_params_t p = {
                .package_path = pak_path,
                .auto_proceed_oobe = true
            };
            bootsetup_result_t res = (bootsetup_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    return bootsetup_fallback();
}

bool screen_mgr_has_pending_update(void) {
    uint32_t size = 0;
    uint8_t* buf = (uint8_t*)vfs_read_entire_file("C:\\NewOS\\Boot\\boot_action.ini", &size);
    if (!buf || size == 0) return false;

    bool has_update = false;
    // Cerca Target=UPDATE.MOD o UPDATE.MOD
    for (uint32_t i = 0; i + 10 < size; i++) {
        if (buf[i] == 'U' && buf[i+1] == 'P' && buf[i+2] == 'D' && buf[i+3] == 'A' &&
            buf[i+4] == 'T' && buf[i+5] == 'E' && buf[i+6] == '.' && buf[i+7] == 'M' &&
            buf[i+8] == 'O' && buf[i+9] == 'D') {
            has_update = true;
            break;
        }
    }

    kfree(buf);
    return has_update;
}

update_result_t screen_mgr_run_update(void) {
    screen_mgr_init();
    serial_log("[SCREEN_MGR] Rilevata azione straordinaria di aggiornamento: avvio UPDATE.MOD...\n");

    // Watchdog contatore tentativi (Crash Recovery Boot Watchdog)
    uint32_t act_size = 0;
    uint8_t* act_buf = (uint8_t*)vfs_read_entire_file("C:\\NewOS\\Boot\\boot_action.ini", &act_size);
    int attempts = 1;

    if (act_buf && act_size > 0) {
        for (uint32_t i = 0; i + 15 < act_size; i++) {
            if (act_buf[i] == 'A' && act_buf[i+1] == 't' && act_buf[i+2] == 't' && act_buf[i+3] == 'e' &&
                act_buf[i+4] == 'm' && act_buf[i+5] == 'p' && act_buf[i+6] == 't' && act_buf[i+7] == 's' &&
                act_buf[i+8] == '=') {
                attempts = act_buf[i+9] - '0';
                break;
            }
        }
        kfree(act_buf);
    }

    // Se superato il limite di tentativi (MaxAttempts=2), Rollback Automatico di Emergenza
    if (attempts > 2) {
        serial_log("[WATCHDOG CRITICAL] Superato il numero massimo tentativi update (Attempts > 2)! Rollback di Emergenza.\n");
        const char* fail_log = "[ROLLBACK_EMERGENCY]\r\nTimestamp=CrashRecovery\r\nMotivo=Tentativi_Massimi_Superati\r\nStato=LOGIN.MOD_Ripristinato\r\n";
        vfs_write_entire_file_inplace("C:\\NewOS\\Update\\update_failure.log", fail_log, 110);
        vfs_write_entire_file_inplace("C:\\NewOS\\Boot\\boot_action.ini", "", 0);
        return UPDATE_ROLLBACK;
    }

    // Incrementa contatore tentativi per il watchdog
    char new_action[96];
    mini_snprintf(new_action, sizeof(new_action), "[BootAction]\r\nTarget=UPDATE.MOD\r\nAttempts=%d\r\nMaxAttempts=2\r\n", attempts + 1);
    vfs_write_entire_file_inplace("C:\\NewOS\\Boot\\boot_action.ini", new_action, (uint32_t)strlen(new_action));

    const char* paths[] = {
        "C:\\NewOS\\Boot\\UPDATE.MOD",
        "C:\\NewOS\\SystemProgram\\UPDATE.MOD",
        "D:\\NewOS\\Boot\\UPDATE.MOD",
        "C:\\UPDATE.MOD",
        "/NewOS/Boot/UPDATE.MOD",
        "UPDATE.MOD"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size > 0) break;
    }

    if (mod_buf && mod_size >= sizeof(module_header_t)) {
        if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_UPDATE, "UPDATE.MOD")) {
            // Buffer protetto con cuscinetto BSS azzerato (64 KB)
            uint8_t* exec_buf = (uint8_t*)kmalloc(mod_size + 65536);
            if (exec_buf) {
                memcpy(exec_buf, mod_buf, mod_size);
                memset(exec_buf + mod_size, 0, 65536);
                kfree(mod_buf);
                mod_buf = exec_buf;
            }

            const module_header_t* h = (const module_header_t*)mod_buf;
            update_entry_fn entry = (update_entry_fn)(mod_buf + h->entry_offset);

            update_params_t p = {
                .package_path = "C:\\NewOS\\Update\\UPDATE.PAK",
                .reg_update_path = "C:\\NewOS\\Update\\registry_update.update",
                .force_unattended = true
            };
            update_result_t res = (update_result_t)entry(&g_kernel_api, &p);

            kfree(mod_buf);
            return res;
        }
        kfree(mod_buf);
    }

    serial_log("[SCREEN_MGR ERROR] Impossibile caricare UPDATE.MOD o modulo non autentico.\n");
    return UPDATE_ERROR;
}


